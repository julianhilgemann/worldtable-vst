#pragma once

#include "dsp/DspCommon.h"

namespace worldtable::dsp
{

/** Slow loudness normaliser (spec/05-sound-engine.md §8): a 3 s RMS estimate, gain toward a
    target RMS, clamped to ± range, slewed over 10 s so nothing is ever audible as a jump. Port of
    `prototype/sky/sound/fx.js`'s AutoGain; no K-weighting yet (the spec's ≈ −18 LUFS becomes
    −18 dBFS RMS for now). Unity until the first target is reached.

    `hold` (Play mode, T3.7): the gain glides back to unity over holdSeconds instead, so a played
    note keeps the level the player gave it (a normaliser would lift the first note after a pause
    by up to `range`). The measurement goes on, so leaving hold resumes smoothly. */
class AutoGain
{
public:
    struct Settings
    {
        float targetDb = -18.0f;       ///< RMS target, dBFS
        float rangeDb = 12.0f;         ///< ± clamp, dB
        float measureSeconds = 3.0f;   ///< RMS time constant
        float slewSeconds = 10.0f;     ///< gain time constant (≥ 10 s, rule 4)
        bool hold = false;             ///< glide to unity (Play mode)
        float holdSeconds = 2.0f;      ///< time constant of that glide
    };

    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate;
        settings = {};
        apply();
        reset();
    }

    /** Call once per chunk (or per render). */
    void setSettings (const Settings& s) noexcept
    {
        settings = s;
        apply();
    }

    void reset() noexcept
    {
        p = target * target;
        gain = 1.0f;
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = left[i], r = right[i];
            p += aP * (0.5f * (l * l + r * r) - p);
            const float want = settings.hold ? 1.0f : std::clamp (target / std::sqrt (p + 1.0e-12f), lo, hi);
            // double: a float gain stalls short of its target once the step drops below half an
            // ulp (+0.05 dB at a 2 s glide); the hold reaches unity, and stays there exactly
            gain += (settings.hold ? aHold : aG) * (static_cast<double> (want) - gain);
            if (settings.hold && std::abs (gain - 1.0) < 1.0e-9)
                gain = 1.0;
            const auto g = static_cast<float> (gain);
            left[i] = l * g;
            right[i] = r * g;
        }
    }

    float currentGain() const noexcept { return static_cast<float> (gain); }

private:
    void apply() noexcept
    {
        target = dbToGain (settings.targetDb);
        lo = dbToGain (-settings.rangeDb);
        hi = dbToGain (settings.rangeDb);
        const float measure = std::max (1.0e-3f, settings.measureSeconds * static_cast<float> (rate));
        const float slew = std::max (1.0e-3f, settings.slewSeconds * static_cast<float> (rate));
        aP = 1.0f - std::exp (-1.0f / measure);
        aG = 1.0 - std::exp (-1.0 / static_cast<double> (slew));
        aHold = 1.0 - std::exp (-1.0 / std::max (1.0e-3, static_cast<double> (settings.holdSeconds) * rate));
    }

    Settings settings;
    double rate = 48000.0;
    float target = 1.0f, lo = 0.25f, hi = 4.0f, aP = 0.0f;
    double aG = 0.0, aHold = 0.0;
    float p = 1.0f;
    double gain = 1.0;
};

} // namespace worldtable::dsp
