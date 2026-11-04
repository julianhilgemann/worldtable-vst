#pragma once

#include "data/WorldData.h"
#include "dsp/WavetableSet.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>
#include <vector>

namespace worldtable::transform
{

/** User-facing "bend the data" controls (parameters wt_spread/tilt/contrast/phase). */
struct TransformSettings
{
    float spread = 0.45f;        ///< 0..1: spectra → how many partials the data spans; series → window length
    float tiltDbPerOct = -3.0f;  ///< spectral tilt applied to the harmonics
    float contrast = 1.0f;       ///< exponent on harmonic magnitudes (>1 sharpens peaks, <1 flattens)
    float phase = 0.6f;          ///< 0 = data/zero phase, 1 = fixed random phases (smoother crest)

    bool operator== (const TransformSettings& o) const noexcept
    {
        const auto same = [] (float a, float b) { return std::abs (a - b) < 1.0e-6f; };
        return same (spread, o.spread) && same (tiltDbPerOct, o.tiltDbPerOct)
            && same (contrast, o.contrast) && same (phase, o.phase);
    }
};

/** Small, display-oriented copy of a built table plus the data it came from (for the UI). */
struct TablePreview
{
    static constexpr int kWavePoints = 128;
    static constexpr int kHarmonics  = 48;
    static constexpr int kRawPoints  = 64;

    int numFrames = 0;
    std::vector<float> wave;        ///< numFrames × kWavePoints, −1..1
    std::vector<float> harmonics;   ///< numFrames × kHarmonics, 0..1 (after transform)
    std::vector<float> raw;         ///< numFrames × kRawPoints, 0..1 (input data, globally normalised)
    std::vector<double> frameTimesUtc;
    float rawMin = 0.0f, rawMax = 1.0f;
    juce::String rawUnit;           ///< e.g. "m²/Hz" or "nT"
    juce::String rawAxis;           ///< e.g. "0.03 – 0.49 Hz" or "2.5 h window"
    bool isSpectrum = true;
};

/** Turns world data into band-limited wavetables (spec/04-dsp-engine.md §2).

    Builder B (data-as-harmonics): each spectrum row → one frame; spectral energy
    is mapped onto harmonic partials. Builder A (series-as-cycle): successive windows
    of a scalar series → frames, each window made periodic and resampled to one cycle.
    Both then share one render stage (contrast, tilt, phase, mipmaps, RMS normalise).

    Runs on the builder thread; allocates freely. Deterministic for equal input. */
class WavetableBuilder
{
public:
    WavetableBuilder();

    bool build (const data::SpectrumSeries&, const TransformSettings&, dsp::WavetableSet&, TablePreview&);
    bool build (const data::ScalarSeries&, const TransformSettings&, dsp::WavetableSet&, TablePreview&);

    static constexpr float kTargetRms = 0.35f;

    /** Number of partials a spectrum is spread across for a given spread setting. */
    static int partialsForSpread (float spread) noexcept;
    /** Window length (samples) for a series at a given spread setting. */
    static int windowForSpread (float spread) noexcept;

private:
    static constexpr int kN = dsp::WavetableSet::kFrameSize;
    static constexpr int kMaxH = dsp::WavetableSet::kMaxHarmonic - 1;   // exclude the Nyquist bin

    struct HarmonicFrame
    {
        std::vector<float> mag, phase;   // index = harmonic number, size kMaxH + 1
        int maxHarmonic = 1;
        bool fundamentalFloor = false;
    };

    void render (std::vector<HarmonicFrame>&, const TransformSettings&, dsp::WavetableSet&, TablePreview&);

    juce::dsp::FFT fft { 11 };
    std::vector<float> fftBuffer;
    std::array<float, kMaxH + 1> randomPhase {};
};

} // namespace worldtable::transform
