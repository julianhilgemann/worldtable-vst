#pragma once

#include "dsp/DspCommon.h"

#include <array>

namespace worldtable::dsp
{

/** Slow stereo phaser (spec/05-sound-engine.md §7): four to eight first-order all-pass
    stages per channel, one LFO sweeping the break frequency, feedback and dry/wet. The right
    channel's LFO is a quarter cycle behind, so the notches travel across the field.
    Allocation-free; call setSettings between chunks. */
class Phaser
{
public:
    struct Settings
    {
        float rateHz = 0.05f;     ///< LFO rate, Hz (slow)
        float depth = 0.6f;       ///< 0 … 1 sweep width
        float feedback = 0.3f;    ///< 0 … 0.8
        float mix = 0.4f;         ///< 0 (bypass) … 1 (wet)
        int stages = 6;           ///< 2 … 8
    };

    static constexpr int kMaxStages = 8;

    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate;
        reset();
    }

    void setSettings (const Settings& s) noexcept
    {
        settings = s;
        settings.rateHz = std::max (s.rateHz, 0.0f);
        settings.depth = std::clamp (s.depth, 0.0f, 1.0f);
        settings.feedback = std::clamp (s.feedback, 0.0f, 0.8f);
        settings.mix = std::clamp (s.mix, 0.0f, 1.0f);
        settings.stages = std::clamp (s.stages, 2, kMaxStages);
        // Dry + phase-shifted wet lose level as they sum; keep the loudness roughly neutral.
        makeup = 1.0f / std::sqrt (std::max (1.0e-6f, (1.0f - settings.mix) * (1.0f - settings.mix)
                                                        + settings.mix * settings.mix));
    }

    void reset() noexcept
    {
        for (auto& c : channels)
        {
            c.x1.fill (0.0f);
            c.y1.fill (0.0f);
            c.feedbackState = 0.0f;
        }
        phaseL = 0.0f;
        phaseR = 0.25f;
    }

    /** The left LFO's phase, 0 … 1 (T6.12: the editor draws the notches where the sound has them). */
    float lfoPhase() const noexcept { return phaseL; }

    /** The break frequency for an LFO phase, as step() sweeps it (200 Hz … 2 kHz at depth 1). */
    static double breakHz (double depth, double phase) noexcept
    {
        const double d = std::clamp (depth, 0.0, 1.0);
        return 200.0 * std::pow (10.0, 0.5 + 0.5 * d * std::sin (2.0 * std::numbers::pi * phase));
    }

    /** The phaser's gain at `hz` with its break frequency at `breakHz`, for a signal small enough
        that the feedback's tanh is linear. It is this filter's exact response, which the editor's
        picture draws (source/ui/views.js `phaserResponse` is this function line for line):
        each stage is the bilinear all-pass (a + z⁻¹) / (1 + a z⁻¹), phase −2·atan(tan(πf/fs) /
        tan(πf_c/fs)); N stages give θ = N × that; the feedback returns one sample late, so
        wet = e^{jθ} / (1 − fb·e^{−jω}·e^{jθ}); out = ((1 − mix) + mix·wet) × the make-up gain. */
    static double magnitude (double hz, double breakHz, double sampleRate, int stages, double feedback, double mix) noexcept
    {
        constexpr double pi = std::numbers::pi;
        const int n = std::clamp (stages, 2, kMaxStages);
        const double fb = std::clamp (feedback, 0.0, 0.8), m = std::clamp (mix, 0.0, 1.0);
        const double w = 2.0 * pi * std::clamp (hz, 0.0, 0.4999 * sampleRate) / sampleRate;
        const double theta = -2.0 * n * std::atan (std::tan (0.5 * w) / std::tan (pi * breakHz / sampleRate));
        const double ar = std::cos (theta), ai = std::sin (theta);
        const double dr = 1.0 - fb * std::cos (theta - w), di = -fb * std::sin (theta - w);
        const double dd = dr * dr + di * di;
        const double wr = (ar * dr + ai * di) / dd, wi = (ai * dr - ar * di) / dd;
        const double outR = (1.0 - m) + m * wr, outI = m * wi;
        return std::sqrt (outR * outR + outI * outI) / std::sqrt (std::max (1.0e-6, (1.0 - m) * (1.0 - m) + m * m));
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        if (settings.mix <= 0.0f)
            return;
        const float increment = static_cast<float> (settings.rateHz / rate);
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] = step (channels[0], left[i], phaseL);
            right[i] = step (channels[1], right[i], phaseR);
            phaseL += increment;
            phaseR += increment;
            if (phaseL >= 1.0f) phaseL -= 1.0f;
            if (phaseR >= 1.0f) phaseR -= 1.0f;
        }
    }

private:
    struct Channel
    {
        std::array<float, kMaxStages> x1 {}, y1 {};
        float feedbackState = 0.0f;
    };

    float step (Channel& c, float x, float phase) noexcept
    {
        const float lfo = std::sin (kTwoPi * phase);
        const float sweep = 0.5f + 0.5f * settings.depth * lfo;       // 0 … 1
        const float hz = 200.0f * std::pow (10.0f, sweep);            // 200 Hz … 2 kHz
        const float t = std::tan (kPi * hz / static_cast<float> (rate));
        const float a = (t - 1.0f) / (t + 1.0f);                       // stable, |a| < 1

        float v = x + settings.feedback * c.feedbackState;
        for (int s = 0; s < settings.stages; ++s)
        {
            const auto i = static_cast<size_t> (s);
            const float y = a * v + c.x1[i] - a * c.y1[i];            // first-order all-pass
            c.x1[i] = v;
            c.y1[i] = y;
            v = y;
        }
        c.feedbackState = std::tanh (v);                               // soft-limit the feedback
        return (x * (1.0f - settings.mix) + v * settings.mix) * makeup;
    }

    Settings settings;
    float makeup = 1.0f;
    double rate = 48000.0;
    std::array<Channel, 2> channels {};
    float phaseL = 0.0f, phaseR = 0.25f;
};

} // namespace worldtable::dsp
