#pragma once

#include "dsp/DspCommon.h"

#include <algorithm>
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

    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        glide = std::exp (-1.0 / (static_cast<double> (kGlideSeconds) * rate));
        for (auto& c : chans)
            for (auto& s : c.sat)
                s.dc.prepare (rate);
        retarget();
        reset();
    }

    /** Snaps every glide to the current settings and clears the state. */
    void reset() noexcept
    {
        for (int b = 0; b < kBands; ++b)
        {
            const auto i = static_cast<size_t> (b);
            const auto& t = targets[i];
            now[i] = { t.logHz, t.gainDb, t.q, t.drive };
            filterPending[i] = drivePending[i] = false;
            updateCoefs (b, false);
            updateShaper (b);
        }
        for (auto& c : chans)
        {
            for (auto& s : c.bands)
                s.reset();
            for (auto& o : c.owners)
                for (auto& s : o)
                    s.reset();
            for (auto& s : c.sat)
            {
                s.prevB = s.prevF = 0.0;
                s.fresh = false;
                s.dc.reset();
            }
        }
    }

    void setSettings (const Settings& s) noexcept
    {
        for (int b = 0; b < kBands; ++b)
        {
            const auto i = static_cast<size_t> (b);
            const auto& in = s.bands[i];
            auto& out = settings.bands[i];
            out.hz = std::clamp (in.hz, kMinHz[i], kMaxHz[i]);
            out.gainDb = b == highCut ? 0.0f : std::clamp (in.gainDb, -kMaxGainDb, kMaxGainDb);
            out.q = b == lowShelf ? kShelfQ : b == highCut ? kCutQ : std::clamp (in.q, kMinQ, kMaxQ);
            out.drive = std::clamp (in.drive, 0.0f, 1.0f);
        }
        retarget();
    }

    const Settings& getSettings() const noexcept { return settings; }

    /** The sky's opening of the high cut, in octaves (the caller slews it). */
    void setSkyOctaves (float octaves) noexcept
    {
        skyOctaves = octaves;
        retarget();
    }

    /** The high cut now (after the sky and the glide), Hz; kOpenHz when open. */
    float cutHz() const noexcept
    {
        const auto i = static_cast<size_t> (highCut);
        return std::min (kOpenHz, filterPending[i] ? static_cast<float> (std::exp2 (now[i].logHz)) : targets[i].hz);
    }

    /** True when any band drives (its drive, gliding, is above zero). */
    bool driving() const noexcept
    {
        return std::any_of (shapers.begin(), shapers.end(), [] (const Shaper& s) { return s.amount > 0.0f; });
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            for (int b = 0; b < kBands; ++b)
                advance (b);
            left[i] = processSample (chans[0], left[i]);
            right[i] = processSample (chans[1], right[i]);
        }
    }

    //== The response (for tests and the display, T6.14) ===============================================
    /** Where band `band` sits at rest for a setting `hz` (the SVF's frequency): the shelf and bells
        below kMaxBandFraction × fs; the cut at `hz`, or at kOpenFraction × fs when open. */
    static float restHz (int band, float hz, double sampleRate) noexcept
    {
        const auto fs = static_cast<float> (sampleRate);
        if (band != highCut)
            return std::min (hz, kMaxBandFraction * fs);
        return isOpen (hz) ? kOpenFraction * fs : std::min (hz, kOpenFraction * fs);
    }

    /** The cut is open from 19.98 kHz up (so a host's rounding of 20 kHz still opens it). */
    static bool isOpen (float hz) noexcept { return hz >= 0.999f * kOpenHz; }

    /** The coefficients of band `band` at rest. For the cut, `hz` ≥ kOpenHz is open. */
    static Coefs bandCoefs (int band, float hz, float gainDb, float q, double sampleRate) noexcept
    {
        return svfCoefs (band, restHz (band, hz, sampleRate), gainDb, q, sampleRate);
    }

    /** One band through one SVF stage. */
    static float stepBand (SvfStage& s, float x, const Coefs& c) noexcept
    {
        const auto o = s.process (x, c.g, c.k);
        return c.m0 * x + c.m1 * o.band + c.m2 * o.low;
    }

    /** Band `band`'s response at `hz`, dB (the formulas above; an open cut is its real, near-flat
        response at kOpenFraction × fs). */
    static double bandDb (int band, double f0, double gainDb, double q, double hz, double sampleRate) noexcept
    {
        const double fs = sampleRate;
        const double centre = restHz (band, static_cast<float> (f0), fs);
        const double w = std::tan (std::numbers::pi * std::min (hz, 0.4999 * fs) / fs) / std::tan (std::numbers::pi * centre / fs);
        const double w2 = w * w;
        const double A = std::pow (10.0, gainDb / 40.0);
        double num = 1.0, den = 1.0;
        if (band == lowShelf)
        {
            const double qq = static_cast<double> (kShelfQ) * kShelfQ;
            num = A * A * ((A - w2) * (A - w2) + A * w2 / qq);
            den = (1.0 - A * w2) * (1.0 - A * w2) + A * w2 / qq;
        }
        else if (band == highCut)
        {
            den = 1.0 + w2 * w2;
        }
        else
        {
            const double width = std::clamp (q, static_cast<double> (kMinQ), static_cast<double> (kMaxQ));
            const double a = w * A / width, b = w / (A * width);
            num = (1.0 - w2) * (1.0 - w2) + a * a;
            den = (1.0 - w2) * (1.0 - w2) + b * b;
        }
        return 10.0 * std::log10 (num / den);
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
    /** The glides run in double: a float one-pole stalls hundreds of ULPs short of its target
        (its step falls under half an ULP), so it would never land on the settings. */
    struct Target
    {
        float hz = 1000.0f;
        double logHz = 9.97, gainDb = 0.0, q = 1.0, drive = 0.0;
    };
    struct Now
    {
        double logHz = 9.97, gainDb = 0.0, q = 1.0, drive = 0.0;
    };
    /** The analysis filter that picks the part of the signal a band owns. */
    struct Owner
    {
        float g = 0.0f, k = 1.41421356f;
        bool lowPass = false;
    };
    struct Shaper
    {
        double gain = 1.0, bias = 0.0, tanhBias = 0.0, slope = 1.0;
        float amount = 0.0f;
    };
    struct SatState
    {
        double prevB = 0.0, prevF = 0.0;
        bool fresh = false;
        DcBlocker dc;
    };
    struct Channel
    {
        std::array<SvfStage, kBands> bands {};
        std::array<std::array<SvfStage, 2>, kBands> owners {};
        std::array<SatState, kBands> sat {};
    };

    /** SVF coefficients for band `band` with the SVF at `svfHz` (no open mapping: the glide passes
        through). */
    static Coefs svfCoefs (int band, float svfHz, float gainDb, float q, double sampleRate) noexcept
    {
        const double fs = sampleRate;
        const double t = std::tan (std::numbers::pi * std::clamp (static_cast<double> (svfHz), 1.0, 0.4999 * fs) / fs);
        const double A = std::pow (10.0, static_cast<double> (gainDb) / 40.0);
        Coefs c;
        if (band == lowShelf)
        {
            const double k = 1.0 / static_cast<double> (kShelfQ);
            c = { static_cast<float> (t / std::sqrt (A)), static_cast<float> (k), 1.0f,
                  static_cast<float> (k * (A - 1.0)), static_cast<float> (A * A - 1.0) };
        }
        else if (band == highCut)
        {
            c = { static_cast<float> (t), static_cast<float> (1.0 / static_cast<double> (kCutQ)), 0.0f, 0.0f, 1.0f };
        }
        else
        {
            const double k = 1.0 / (static_cast<double> (q) * A);
            c = { static_cast<float> (t), static_cast<float> (k), 1.0f, static_cast<float> (k * (A * A - 1.0)), 0.0f };
        }
        return c;
    }

    static double logCosh (double x) noexcept
    {
        const double a = std::abs (x);
        return a - std::numbers::ln2 + std::log1p (std::exp (-2.0 * a));   // stable for large |x|
    }

    /** Moves `v` one sample toward `target`; snaps onto it when within `eps`. True while moving. */
    static bool settle (double& v, double target, double coef, double eps) noexcept
    {
        v = target + (v - target) * coef;
        if (std::abs (v - target) < eps)
        {
            v = target;
            return false;
        }
        return true;
    }

    void retarget() noexcept
    {
        for (int b = 0; b < kBands; ++b)
        {
            const auto i = static_cast<size_t> (b);
            const auto& s = settings.bands[i];
            auto& t = targets[i];
            const float hz = b == highCut ? s.hz * std::exp2 (skyOctaves) : s.hz;
            t.hz = restHz (b, hz, rate);
            t.logHz = std::log2 (static_cast<double> (t.hz));
            t.gainDb = s.gainDb;
            t.q = s.q;
            t.drive = s.drive;
            filterPending[i] = drivePending[i] = true;   // a glide already there settles in one sample
        }
    }

    void advance (int b) noexcept
    {
        const auto i = static_cast<size_t> (b);
        auto& n = now[i];
        const auto& t = targets[i];
        if (filterPending[i])
        {
            bool moving = settle (n.logHz, t.logHz, glide, 1.0e-7);   // octaves
            moving |= settle (n.gainDb, t.gainDb, glide, 1.0e-6);      // dB
            moving |= settle (n.q, t.q, glide, 1.0e-7);
            updateCoefs (b, moving);
            filterPending[i] = moving;
        }
        driveMoved[i] = drivePending[i];
        if (drivePending[i])
        {
            drivePending[i] = settle (n.drive, t.drive, glide, 1.0e-7);
            updateShaper (b);
        }
    }

    void updateCoefs (int b, bool moving) noexcept
    {
        const auto i = static_cast<size_t> (b);
        const auto& n = now[i];
        const float hz = moving ? static_cast<float> (std::exp2 (n.logHz)) : targets[i].hz;   // at rest: exactly the settings
        coefs[i] = svfCoefs (b, hz, static_cast<float> (n.gainDb), static_cast<float> (n.q), rate);

        auto& o = owners[i];
        const auto fs = static_cast<float> (rate);
        if (b == lowShelf)
        {
            o = { static_cast<float> (std::tan (std::numbers::pi * static_cast<double> (hz) / rate)), 1.41421356f, true };
        }
        else
        {
            const float centre = b == highCut ? std::min (std::min (hz, kOpenHz) * 0.70710678f, kMaxBandFraction * fs) : hz;
            const float width = b == highCut ? 1.41421356f : static_cast<float> (n.q);   // an octave: Q √2
            o = { static_cast<float> (std::tan (std::numbers::pi * static_cast<double> (centre) / rate)), 1.0f / width, false };
        }
    }

    void updateShaper (int b) noexcept
    {
        auto& s = shapers[static_cast<size_t> (b)];
        const double d = now[static_cast<size_t> (b)].drive;
        s.amount = static_cast<float> (d);
        s.gain = 1.0 + static_cast<double> (kDrivePreGain) * d;
        s.bias = static_cast<double> (kAsymmetry) * d;
        s.tanhBias = std::tanh (s.bias);
        s.slope = 1.0 - s.tanhBias * s.tanhBias;
    }

    /** The part of `y` band `b` owns: two stages, low-pass for the shelf, unity-peak band-pass else. */
    float owned (int b, Channel& c, float y) noexcept
    {
        const auto i = static_cast<size_t> (b);
        const auto& o = owners[i];
        auto& st = c.owners[i];
        if (o.lowPass)
            return st[1].process (st[0].process (y, o.g, o.k).low, o.g, o.k).low;
        const float a = o.k * st[0].process (y, o.g, o.k).band;
        return o.k * st[1].process (a, o.g, o.k).band;
    }

    /** driveₖ · eₖ: what band `b`'s drive adds (0 when its drive is 0). */
    float saturate (int b, Channel& c, float part) noexcept
    {
        const auto i = static_cast<size_t> (b);
        const auto& sh = shapers[i];
        auto& st = c.sat[i];
        const double now0 = part;
        if (sh.amount <= 0.0f)
        {
            st.prevB = now0;   // ready to start from here
            st.fresh = false;
            st.dc.reset();
            return 0.0f;
        }
        const double x = sh.gain * now0 + sh.bias, xp = sh.gain * st.prevB + sh.bias;
        const double fx = logCosh (x);
        const double fxp = st.fresh && ! driveMoved[i] ? st.prevF : logCosh (xp);
        const double dx = x - xp;
        const double shaped = std::abs (dx) > 1.0e-7 ? (fx - fxp) / dx : std::tanh (0.5 * (x + xp));
        const double e = ((shaped - sh.tanhBias) / sh.slope - (0.5 * (x + xp) - sh.bias)) / sh.gain;
        st.prevB = now0;
        st.prevF = fx;
        st.fresh = true;
        return sh.amount * st.dc.process (static_cast<float> (e));
    }

    float processSample (Channel& c, float x) noexcept
    {
        float y = x;
        for (int b = 0; b < kBands; ++b)
            y = stepBand (c.bands[static_cast<size_t> (b)], y, coefs[static_cast<size_t> (b)]);

        bool any = false;
        float add = 0.0f;
        for (int b = 0; b < kBands; ++b)
        {
            const float part = owned (b, c, y);
            any = any || shapers[static_cast<size_t> (b)].amount > 0.0f;
            add += saturate (b, c, part);
        }
        return any ? y + add : y;
    }

    Settings settings;
    double rate = 48000.0;
    double glide = 0.0;
    float skyOctaves = 0.0f;
    std::array<Target, kBands> targets {};
    std::array<Now, kBands> now {};
    std::array<bool, kBands> filterPending {}, drivePending {}, driveMoved {};
    std::array<Coefs, kBands> coefs {};
    std::array<Owner, kBands> owners {};
    std::array<Shaper, kBands> shapers {};
    std::array<Channel, 2> chans {};
};

} // namespace worldtable::dsp
