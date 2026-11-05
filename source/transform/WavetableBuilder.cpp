#include "WavetableBuilder.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace worldtable::transform
{

namespace
{
constexpr float kPi = std::numbers::pi_v<float>;

float wrapPi (float x) noexcept
{
    x = std::fmod (x + kPi, 2.0f * kPi);
    if (x < 0.0f)
        x += 2.0f * kPi;
    return x - kPi;
}

float interpolate (const std::vector<float>& xs, const std::vector<float>& ys, float x) noexcept
{
    if (x <= xs.front()) return ys.front();
    if (x >= xs.back())  return ys.back();
    const auto it = std::upper_bound (xs.begin(), xs.end(), x);
    const auto i1 = static_cast<size_t> (it - xs.begin());
    const auto i0 = i1 - 1;
    const float t = (x - xs[i0]) / std::max (1.0e-9f, xs[i1] - xs[i0]);
    return ys[i0] + t * (ys[i1] - ys[i0]);
}

/** Periodic Catmull-Rom read of x (length L) at fractional index t. */
float periodicCubic (const std::vector<float>& x, int L, double t) noexcept
{
    const int i1 = static_cast<int> (std::floor (t));
    const float f = static_cast<float> (t - i1);
    const auto at = [&] (int i) { return x[static_cast<size_t> (((i % L) + L) % L)]; };
    const float p0 = at (i1 - 1), p1 = at (i1), p2 = at (i1 + 1), p3 = at (i1 + 2);
    return p1 + 0.5f * f * (p2 - p0 + f * (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3 + f * (3.0f * (p1 - p2) + p3 - p0)));
}
} // namespace

WavetableBuilder::WavetableBuilder()
    : fftBuffer (static_cast<size_t> (2 * kN), 0.0f)
{
    juce::Random rng (0x5EEDC0DE);
    for (auto& p : randomPhase)
        p = (rng.nextFloat() * 2.0f - 1.0f) * kPi;
}

int WavetableBuilder::partialsForSpread (float spread) noexcept
{
    return std::clamp (static_cast<int> (std::lround (8.0 * std::pow (32.0, std::clamp (spread, 0.0f, 1.0f)))), 4, 256);
}

int WavetableBuilder::windowForSpread (float spread) noexcept
{
    return static_cast<int> (std::lround (32.0 * std::pow (2.0, 5.0 * std::clamp (spread, 0.0f, 1.0f))));
}

//==============================================================================
bool WavetableBuilder::build (const data::SpectrumSeries& series, const TransformSettings& settings,
                              dsp::WavetableSet& out, TablePreview& preview)
{
    if (series.rows.empty())
        return false;

    const int numFrames = std::min (static_cast<int> (series.rows.size()), dsp::WavetableSet::kMaxFrames);
    const auto first = series.rows.size() - static_cast<size_t> (numFrames);
    const int partials = partialsForSpread (settings.spread);

    preview = {};
    preview.isSpectrum = true;
    preview.rawUnit = juce::String::fromUTF8 ("m\xc2\xb2/Hz");

    float rawMax = 1.0e-9f;
    for (size_t r = first; r < series.rows.size(); ++r)
        for (auto d : series.rows[r].density)
            rawMax = std::max (rawMax, d);
    preview.rawMin = 0.0f;
    preview.rawMax = rawMax;

    std::vector<HarmonicFrame> frames (static_cast<size_t> (numFrames));

    for (int f = 0; f < numFrames; ++f)
    {
        const auto& row = series.rows[first + static_cast<size_t> (f)];
        auto& hf = frames[static_cast<size_t> (f)];
        hf.mag.assign (kMaxH + 1, 0.0f);
        hf.phase.assign (kMaxH + 1, -0.5f * kPi);   // sine phase
        hf.maxHarmonic = std::min (partials, kMaxH);
        hf.fundamentalFloor = true;

        const float fmin = row.frequencyHz.front();
        const float fmax = row.frequencyHz.back();
        const float bandWidth = (fmax - fmin) / static_cast<float> (std::max (1, hf.maxHarmonic));

        for (int h = 1; h <= hf.maxHarmonic; ++h)
        {
            // average amplitude (sqrt of energy density) across this partial's band
            float sum = 0.0f;
            constexpr int kSub = 4;
            for (int s = 0; s < kSub; ++s)
            {
                const float fq = fmin + bandWidth * (static_cast<float> (h - 1) + (static_cast<float> (s) + 0.5f) / kSub);
                sum += std::sqrt (std::max (0.0f, interpolate (row.frequencyHz, row.density, fq)));
            }
            hf.mag[static_cast<size_t> (h)] = sum / kSub;
        }

        // preview: raw spectrum
        for (int i = 0; i < TablePreview::kRawPoints; ++i)
        {
            const float fq = fmin + (fmax - fmin) * static_cast<float> (i) / (TablePreview::kRawPoints - 1);
            preview.raw.push_back (interpolate (row.frequencyHz, row.density, fq) / rawMax);
        }
        preview.frameTimesUtc.push_back (row.timeUtc);

        if (f == numFrames - 1)
            preview.rawAxis = juce::String (fmin, 3) + juce::String::fromUTF8 (" \xe2\x80\x93 ") + juce::String (fmax, 2) + " Hz";
    }

    render (frames, settings, out, preview);
    return true;
}

//==============================================================================
bool WavetableBuilder::build (const data::ScalarSeries& series, const TransformSettings& settings,
                              dsp::WavetableSet& out, TablePreview& preview)
{
    const int total = static_cast<int> (series.values.size());
    if (total < 16)
        return false;

    const int window = std::clamp (windowForSpread (settings.spread), 16, total);
    const int numFrames = std::clamp (total - window + 1, 1, dsp::WavetableSet::kMaxFrames);
    const double hop = numFrames > 1 ? static_cast<double> (total - window) / (numFrames - 1) : 0.0;

    preview = {};
    preview.isSpectrum = false;
    preview.rawUnit = "nT";

    const auto [minIt, maxIt] = std::minmax_element (series.values.begin(), series.values.end());
    preview.rawMin = *minIt;
    preview.rawMax = *maxIt;
    const float range = std::max (1.0e-6f, *maxIt - *minIt);

    const double hours = window * series.periodSeconds / 3600.0;
    preview.rawAxis = juce::String (hours, hours < 10.0 ? 1 : 0) + " h window";

    std::vector<HarmonicFrame> frames (static_cast<size_t> (numFrames));
    std::vector<float> x (static_cast<size_t> (window));

    for (int f = 0; f < numFrames; ++f)
    {
        const int start = static_cast<int> (std::lround (f * hop));
        for (int j = 0; j < window; ++j)
            x[static_cast<size_t> (j)] = series.values[static_cast<size_t> (start + j)];

        // preview: the raw window, globally normalised
        for (int i = 0; i < TablePreview::kRawPoints; ++i)
        {
            const double t = static_cast<double> (i) * (window - 1) / (TablePreview::kRawPoints - 1);
            const int i0 = static_cast<int> (t);
            const int i1 = std::min (i0 + 1, window - 1);
            const float v = x[static_cast<size_t> (i0)] + static_cast<float> (t - i0) * (x[static_cast<size_t> (i1)] - x[static_cast<size_t> (i0)]);
            preview.raw.push_back ((v - *minIt) / range);
        }
        preview.frameTimesUtc.push_back (series.startUtc + (start + window * 0.5) * series.periodSeconds);

        // Make the window periodic: remove the line through its end points (so the
        // cycle wraps without a step), then the mean.
        const float a = x.front(), b = x.back();
        for (int j = 0; j < window; ++j)
            x[static_cast<size_t> (j)] -= a + (b - a) * static_cast<float> (j) / static_cast<float> (window - 1);
        const int L = window - 1;   // x[L] == x[0] now
        float mean = 0.0f;
        for (int j = 0; j < L; ++j)
            mean += x[static_cast<size_t> (j)];
        mean /= static_cast<float> (L);
        for (int j = 0; j < L; ++j)
            x[static_cast<size_t> (j)] -= mean;

        // resample one period to kN samples, then analyse
        std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);
        for (int n = 0; n < kN; ++n)
            fftBuffer[static_cast<size_t> (n)] = periodicCubic (x, L, static_cast<double> (n) * L / kN);
        fft.performRealOnlyForwardTransform (fftBuffer.data(), true);

        auto& hf = frames[static_cast<size_t> (f)];
        hf.mag.assign (kMaxH + 1, 0.0f);
        hf.phase.assign (kMaxH + 1, 0.0f);
        hf.maxHarmonic = std::clamp (L / 2, 1, kMaxH);   // above the source Nyquist is interpolation residue
        for (int h = 1; h <= hf.maxHarmonic; ++h)
        {
            const float re = fftBuffer[static_cast<size_t> (2 * h)];
            const float im = fftBuffer[static_cast<size_t> (2 * h + 1)];
            hf.mag[static_cast<size_t> (h)] = std::sqrt (re * re + im * im);
            hf.phase[static_cast<size_t> (h)] = std::atan2 (im, re);
        }
    }

    render (frames, settings, out, preview);
    return true;
}

//==============================================================================
void WavetableBuilder::render (std::vector<HarmonicFrame>& frames, const TransformSettings& s,
                               dsp::WavetableSet& out, TablePreview& preview)
{
    const int numFrames = static_cast<int> (frames.size());
    const float tiltExp = s.tiltDbPerOct / 6.0206f;
    const float contrast = std::clamp (s.contrast, 0.05f, 8.0f);
    const float phaseAmt = std::clamp (s.phase, 0.0f, 1.0f);

    preview.numFrames = numFrames;
    preview.wave.assign (static_cast<size_t> (numFrames * TablePreview::kWavePoints), 0.0f);
    preview.harmonics.assign (static_cast<size_t> (numFrames * TablePreview::kHarmonics), 0.0f);

    std::vector<float> level0 (kN);

    for (int f = 0; f < numFrames; ++f)
    {
        auto& hf = frames[static_cast<size_t> (f)];

        float peakMag = 0.0f;
        for (int h = 1; h <= hf.maxHarmonic; ++h)
            peakMag = std::max (peakMag, hf.mag[static_cast<size_t> (h)]);

        if (peakMag < 1.0e-9f)
        {
            // No data energy at all: fall back to a soft sine rather than silence.
            hf.mag[1] = 1.0f;
            peakMag = 1.0f;
        }

        for (int h = 1; h <= hf.maxHarmonic; ++h)
        {
            auto& m = hf.mag[static_cast<size_t> (h)];
            m = std::pow (m / peakMag, contrast);
            if (h == 1 && hf.fundamentalFloor)
                m = std::max (m, 0.3f);   // keeps the pitch anchored when the data has no low partials
            m *= std::pow (static_cast<float> (h), tiltExp);

            auto& ph = hf.phase[static_cast<size_t> (h)];
            ph += phaseAmt * wrapPi (randomPhase[static_cast<size_t> (h)] - ph);
        }

        // preview harmonics (normalised to this frame's maximum)
        float hmax = 1.0e-9f;
        for (int h = 1; h <= std::min (hf.maxHarmonic, TablePreview::kHarmonics); ++h)
            hmax = std::max (hmax, hf.mag[static_cast<size_t> (h)]);
        for (int h = 1; h <= std::min (hf.maxHarmonic, TablePreview::kHarmonics); ++h)
            preview.harmonics[static_cast<size_t> (f * TablePreview::kHarmonics + h - 1)] = hf.mag[static_cast<size_t> (h)] / hmax;

        float gain = 1.0f;

        for (int mip = 0; mip < dsp::WavetableSet::kNumMips; ++mip)
        {
            const int limit = std::min ({ hf.maxHarmonic, dsp::WavetableSet::harmonicsAtMip (mip), kMaxH });

            std::fill (fftBuffer.begin(), fftBuffer.end(), 0.0f);
            for (int h = 1; h <= limit; ++h)
            {
                const float m = hf.mag[static_cast<size_t> (h)];
                const float ph = hf.phase[static_cast<size_t> (h)];
                fftBuffer[static_cast<size_t> (2 * h)] = m * std::cos (ph);
                fftBuffer[static_cast<size_t> (2 * h + 1)] = m * std::sin (ph);
            }
            fft.performRealOnlyInverseTransform (fftBuffer.data());

            if (mip == 0)
            {
                // One gain per frame, shared by all mips: RMS-normalise, but never exceed −0.4 dBFS.
                double sumSq = 0.0;
                float peak = 0.0f;
                for (int n = 0; n < kN; ++n)
                {
                    const float v = fftBuffer[static_cast<size_t> (n)];
                    sumSq += static_cast<double> (v) * v;
                    peak = std::max (peak, std::abs (v));
                }
                const auto rms = static_cast<float> (std::sqrt (sumSq / kN));
                gain = rms > 1.0e-12f ? std::min (kTargetRms / rms, 0.95f / std::max (peak, 1.0e-12f)) : 0.0f;
            }

            float* dest = out.frame (mip, f);
            for (int n = 0; n < kN; ++n)
                dest[n] = fftBuffer[static_cast<size_t> (n)] * gain;
            dest[kN] = dest[0];

            if (mip == 0)
                std::copy (dest, dest + kN, level0.begin());
        }

        for (int i = 0; i < TablePreview::kWavePoints; ++i)
            preview.wave[static_cast<size_t> (f * TablePreview::kWavePoints + i)] = level0[static_cast<size_t> (i * (kN / TablePreview::kWavePoints))];
    }

    out.numFrames = numFrames;
}

} // namespace worldtable::transform
