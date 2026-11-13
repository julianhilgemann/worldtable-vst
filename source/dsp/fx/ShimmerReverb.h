#pragma once

#include "dsp/DspCommon.h"

#include <array>
#include <vector>

namespace worldtable::dsp
{

/** Large ambient reverb: pre-delay → input diffusion (allpass chain) → 8-line
    feedback delay network (Hadamard mixing, per-line damping, slow delay
    modulation). Shimmer feeds the network output through a +12 st pitch shifter
    back into its input; the shimmer path is band-limited and soft-saturated so it
    blooms upward without running away.

    All memory is allocated in prepare(). */
class ShimmerReverb
{
public:
    struct Settings
    {
        float size = 0.75f;       // 0..1 → line lengths
        float decaySeconds = 8.f; // RT60
        float damping = 0.4f;     // 0..1 → high-frequency loss per pass
        float predelayMs = 20.f;
        float shimmer = 0.3f;     // 0..1
        float modulation = 0.4f;  // 0..1
        float mix = 0.4f;         // 0..1
    };

    void prepare (double sampleRate);
    void reset() noexcept;

    /** Call once per chunk. Size glides per sample, so the length change never clicks and the
        result does not depend on how often this is called (T3.6 determinism). */
    void setSettings (const Settings&) noexcept;

    void process (float* left, float* right, int numSamples) noexcept;

    /** Smoothed wet energy, for the UI (0..~1): updated every kMeterSamples, however the calls are
        cut (T4.2: the instrument calls it per tick segment). */
    float getWetLevel() const noexcept { return wetLevel; }

private:
    static constexpr int kLines = 8;
    static constexpr int kDiffusers = 4;

    struct DelayLine
    {
        std::vector<float> buf;
        int pos = 0;

        void allocate (size_t n) { buf.assign (n, 0.0f); pos = 0; }
        void clear() noexcept    { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
        int size() const noexcept { return static_cast<int> (buf.size()); }

        void write (float x) noexcept
        {
            buf[static_cast<size_t> (pos)] = x;
            if (++pos >= size())
                pos = 0;
        }

        /** Delay measured from the most recent write. */
        float read (float delay) const noexcept
        {
            // Wrap integer indices, never a float position: wrapping a float near 0⁻
            // by += size can round to exactly size and read one past the end.
            const auto whole = static_cast<int> (delay);           // delay ≥ 0
            const float frac = delay - static_cast<float> (whole);  // weight of the older sample
            int newer = pos - 1 - whole;
            while (newer < 0)
                newer += size();
            const int older = newer == 0 ? size() - 1 : newer - 1;
            const float a = buf[static_cast<size_t> (newer)];
            return a + frac * (buf[static_cast<size_t> (older)] - a);
        }

        float readInt (int delay) const noexcept
        {
            int p = pos - 1 - delay;
            while (p < 0)
                p += size();
            return buf[static_cast<size_t> (p)];
        }
    };

    struct Allpass
    {
        DelayLine line;
        int delay = 100;
        float g = 0.6f;

        float process (float x) noexcept
        {
            const float d = line.readInt (delay - 1);
            const float v = x + g * d;
            line.write (v);
            return d - g * v;
        }
    };

    /** Two-tap delay-line pitch shifter, Hann-windowed crossfade. `ratio` 2 is +12 st,
        3 is +19 st (an octave and a fifth): the delay shrinks by ratio − 1 samples per sample. */
    struct PitchShifter
    {
        DelayLine line;
        float windowSamples = 2048.0f;
        float phase = 0.0f;
        float ratio = 2.0f;

        float process (float x) noexcept
        {
            line.write (x);
            phase += (ratio - 1.0f) / windowSamples;
            if (phase >= 1.0f)
                phase -= 1.0f;
            float p2 = phase + 0.5f;
            if (p2 >= 1.0f)
                p2 -= 1.0f;
            const float d1 = (1.0f - phase) * windowSamples + 2.0f;
            const float d2 = (1.0f - p2) * windowSamples + 2.0f;
            const float w1 = std::sin (kPi * phase);
            const float w2 = std::sin (kPi * p2);
            return line.read (d1) * w1 * w1 + line.read (d2) * w2 * w2;
        }
    };

    double sampleRate = 48000.0;
    Settings settings;

    DelayLine predelayL, predelayR;
    std::array<Allpass, kDiffusers> diffL, diffR;
    std::array<DelayLine, kLines> lines;
    std::array<float, kLines> lineDelay {}, lineGain {}, lineLp {};
    std::array<double, kLines> modPhase {}, modInc {};
    float lpCoef = 0.5f, modDepth = 0.0f;

    PitchShifter shifter, shifterFifth;
    float shimmerGain = 0.0f, shimmerHp = 0.0f, shimmerLp = 0.0f, shimmerHpCoef = 0.0f, shimmerLpCoef = 0.0f;
    float shimmerFeedback = 0.0f;

    void updateLineCoefficients() noexcept;

    float targetSize = 0.75f, currentSize = 0.75f, sizeCoef = 0.0f;
    float predelayTarget = 0.0f, predelayCurrent = 0.0f, predelayCoef = 0.0f;   // glides: the index never jumps
    int coefCount = 0;
    float wetLevel = 0.0f;
    static constexpr int kMeterSamples = 512;
    float meterEnergy = 0.0f;
    int meterCount = 0;
};

} // namespace worldtable::dsp
