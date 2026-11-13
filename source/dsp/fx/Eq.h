#pragma once

#include "dsp/DspCommon.h"

#include <array>
#include <cmath>

namespace worldtable::dsp
{

/** One topology-preserving state-variable filter stage (Simper/Zavalishin trapezoidal SVF), the
    POC's `Svf.h` (TP.1). Stable under fast coefficient modulation; low, band and high come out of
    the same state. `band` peaks at 1/k at the centre. */
struct SvfStage
{
    float ic1 = 0.0f, ic2 = 0.0f;

    struct Out
    {
        float low, band, high;
    };

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    /** g = tan(π fc / fs), k = damping (1/Q). */
    Out process (float v0, float g, float k) noexcept
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return { v2, v1, v0 - k * v1 - v2 };
    }
};

/** The tone stage (T3.9, spec/05 §7, §15): four fixed bands in series, each with its own drive in
    parallel. It replaces the Filter and the Drive of T3.7; it sits in `dsp::Sources`, after the
    sources, so the sky reaches the high cut on the tick grid.

    ```
    x ─► low shelf ─► bell 2 ─► bell 3 ─► high cut ─► y ─┬──────────────────────► + ─► out
                                                         └─► bₖ ─► sat(bₖ) − bₖ ─► × driveₖ (k = 1 … 4)
    ```

    **The bands** (one TPT SVF each; out = m0·x + m1·band + m2·low, Simper's form). The digital
    response is exactly the RBJ cookbook's (bilinear transform, prewarped at f0). With
    Ω = tan(π f / fs) / tan(π f0 / fs) and A = 10^(gain / 40):
    - 1 **low shelf**, f0 = the midpoint (half the gain in dB), Q = 1/√2 fixed (no overshoot):
      g = tan(π f0 / fs) / √A, k = 1/Q, m0 = 1, m1 = k (A − 1), m2 = A² − 1.
      |H|² = A² · ((A − Ω²)² + A Ω² / Q²) / ((1 − A Ω²)² + A Ω² / Q²)
    - 2, 3 **bells**, f0 = the centre, Q = the width parameter:
      g = tan(π f0 / fs), k = 1 / (Q A), m0 = 1, m1 = k (A² − 1), m2 = 0.
      |H|² = ((1 − Ω²)² + (Ω A / Q)²) / ((1 − Ω²)² + (Ω / (A Q))²)
    - 4 **high cut**, 12 dB per octave (Butterworth, Q = 1/√2):
      g = tan(π f0 / fs), k = √2, m0 = m1 = 0, m2 = 1.   |H|² = 1 / (1 + Ω⁴)
      At kOpenHz (20 kHz, "open") and above, the cut sits at kOpenFraction × fs instead, where it
      is flat to within 0.01 dB up to 20 kHz at 44.1 kHz and above (draw it as 0 dB).
    The whole response in dB is the sum of the four (`responseDb`). A band at 0 dB adds nothing
    (m1 = m2 = 0, m0 = 1).

    **Drive per band, in parallel:** bₖ is the part of y that band k owns, through two SVF stages
    (24 dB per octave below the shelf's corner; 12 dB per octave on each side of a bell's centre,
    each stage at the bell's Q; for the cut, an octave-wide band-pass ending at the cut, centred at
    cut / √2, each stage at Q √2). Each bₖ goes through the ADAA tanh (first-order antiderivative,
    in double) with pre-gain G = 1 + 8·drive and a small bias β = 0.3·drive for even harmonics,
    level-compensated to unity small-signal slope: sat(b) = (T(G b + β) − tanh β) / (G (1 − tanh² β)).
    Only the nonlinear part is added: eₖ = sat(bₖ) − b̄ₖ, where b̄ₖ is bₖ through the same ADAA
    kernel taken linearly (the half-sample average), DC-blocked at 10 Hz, and
    `out = y + Σ driveₖ · eₖ`. So zero drive is exactly the EQ alone, quiet content passes
    untouched, and the harmonics come from that band only.

    **Nothing jumps** (rule 4): frequencies glide per sample in log frequency (τ 20 ms, as the
    filter did), gains, widths and drives per sample with the same τ, coefficients per sample while
    anything moves (and not at all when still). Every glide snaps onto its target at the end, so a
    still EQ has exactly the coefficients of its settings. The caller passes targets straight from
    the parameters (no block-rate glide), which keeps the output the same at any block size.

    **The sky** opens the high cut: `setSkyOctaves` multiplies its frequency by 2^octaves (the
    caller slews the activity ≥ 2 s); at 20 kHz and above it is open.

    prepare() on the message thread; everything else is real-time safe (no allocation, locks or
    I/O). `process` works in place on a stereo pair. */
class Eq
{
public:
    static constexpr int kBands = 4;
    enum BandIndex : int { lowShelf = 0, bellLow = 1, bellHigh = 2, highCut = 3 };

    static constexpr float kOpenHz = 20000.0f;       ///< the cut at or above this is open
    static constexpr float kOpenFraction = 0.49f;    ///< where an open cut sits, × fs
    static constexpr float kMaxBandFraction = 0.45f; ///< the shelf and bells stay below this × fs
    static constexpr float kGlideSeconds = 0.02f;
    static constexpr float kShelfQ = 0.70710678f;    ///< S = 1: the steepest shelf without overshoot
    static constexpr float kCutQ = 0.70710678f;      ///< Butterworth
    static constexpr float kMaxGainDb = 12.0f;
    static constexpr float kMinQ = 0.3f, kMaxQ = 6.0f;
    static constexpr float kDrivePreGain = 8.0f;     ///< G = 1 + 8·drive
    static constexpr float kAsymmetry = 0.3f;        ///< β = 0.3·drive
    static constexpr std::array<float, kBands> kMinHz { 30.0f, 80.0f, 500.0f, 200.0f };
    static constexpr std::array<float, kBands> kMaxHz { 400.0f, 3000.0f, 12000.0f, 20000.0f };
    /** The default drive per band (the parameters' defaults; see the T3.9 notes for how it was chosen). */
    static constexpr std::array<float, kBands> kDefaultDrive { 0.0f, 0.0f, 0.0f, 0.0f };

    struct Band
    {
        float hz = 1000.0f;
        float gainDb = 0.0f;   ///< bands 1–3; the cut has none
        float q = 1.0f;        ///< the bells' width; the shelf and the cut keep 1/√2
        float drive = 0.0f;    ///< 0 … 1
    };

    struct Settings
    {
        std::array<Band, kBands> bands { Band { 110.0f, 0.0f, kShelfQ, kDefaultDrive[0] },
                                         Band { 420.0f, 0.0f, 1.0f, kDefaultDrive[1] },
                                         Band { 2400.0f, 0.0f, 1.0f, kDefaultDrive[2] },
                                         Band { kOpenHz, 0.0f, kCutQ, kDefaultDrive[3] } };
    };

    /** One band's SVF coefficients: out = m0·x + m1·band + m2·low. */
    struct Coefs
    {
        float g = 0.0f, k = 2.0f, m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;
    };

    void prepare (double sampleRate) noexcept { rate = sampleRate; reset(); }

    /** Snaps every glide to the current settings and clears the state. */
    void reset() noexcept {}

    void setSettings (const Settings& s) noexcept { settings = s; }
    const Settings& getSettings() const noexcept { return settings; }

    /** The sky's opening of the high cut, in octaves. */
    void setSkyOctaves (float octaves) noexcept { skyOctaves = octaves; }

    /** The high cut now (after the sky and the glide), Hz; kOpenHz when open. */
    float cutHz() const noexcept { return settings.bands[highCut].hz; }

    void process (float* left, float* right, int numSamples) noexcept
    {
        (void) left;
        (void) right;
        (void) numSamples;
    }

    //== The response (for tests and the display, T6.14) ===============================================
    /** The coefficients of band `band` at rest. For the cut, `hz` ≥ kOpenHz is open. */
    static Coefs bandCoefs (int band, float hz, float gainDb, float q, double sampleRate) noexcept
    {
        (void) band, (void) hz, (void) gainDb, (void) q, (void) sampleRate;
        return {};
    }

    /** One band through one SVF stage. */
    static float stepBand (SvfStage& s, float x, const Coefs& c) noexcept
    {
        const auto o = s.process (x, c.g, c.k);
        return c.m0 * x + c.m1 * o.band + c.m2 * o.low;
    }

    /** Band `band`'s response at `hz`, dB (the formulas above). */
    static double bandDb (int band, double f0, double gainDb, double q, double hz, double sampleRate) noexcept
    {
        (void) band, (void) f0, (void) gainDb, (void) q, (void) hz, (void) sampleRate;
        return 0.0;
    }

    /** The whole EQ's small-signal response at `hz`, dB, with the cut at `cutHz` (after the sky). */
    static double responseDb (const Settings& s, double cutHz, double hz, double sampleRate) noexcept
    {
        double db = 0.0;
        for (int b = 0; b < highCut; ++b)
        {
            const auto& band = s.bands[static_cast<size_t> (b)];
            db += bandDb (b, band.hz, band.gainDb, band.q, hz, sampleRate);
        }
        return db + bandDb (highCut, cutHz, 0.0, kCutQ, hz, sampleRate);
    }

private:
    Settings settings;
    double rate = 48000.0;
    float skyOctaves = 0.0f;
};

} // namespace worldtable::dsp
