#pragma once

#include "dsp/DspCommon.h"

namespace worldtable::dsp
{

/** Slow loudness normaliser (spec/05-sound-engine.md §8): a 3 s RMS estimate, gain toward a
    target RMS, clamped to ± range, slewed over 10 s so nothing is ever audible as a jump. Port of
    `prototype/sky/sound/fx.js`'s AutoGain; no K-weighting yet (the spec's ≈ −18 LUFS becomes
    −18 dBFS RMS for now). Unity until the first target is reached. */
class AutoGain
{
public:
    struct Settings
    {
        float targetDb = -18.0f;       ///< RMS target, dBFS
        float rangeDb = 12.0f;         ///< ± clamp, dB
        float measureSeconds = 3.0f;   ///< RMS time constant
        float slewSeconds = 10.0f;     ///< gain time constant (≥ 10 s, rule 4)
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
            const float want = std::clamp (target / std::sqrt (p + 1.0e-12f), lo, hi);
            gain += aG * (want - gain);
            left[i] = l * gain;
            right[i] = r * gain;
        }
    }

    float currentGain() const noexcept { return gain; }

private:
    void apply() noexcept
    {
        target = dbToGain (settings.targetDb);
        lo = dbToGain (-settings.rangeDb);
        hi = dbToGain (settings.rangeDb);
        const float measure = std::max (1.0e-3f, settings.measureSeconds * static_cast<float> (rate));
        const float slew = std::max (1.0e-3f, settings.slewSeconds * static_cast<float> (rate));
        aP = 1.0f - std::exp (-1.0f / measure);
        aG = 1.0f - std::exp (-1.0f / slew);
    }

    Settings settings;
    double rate = 48000.0;
    float target = 1.0f, lo = 0.25f, hi = 4.0f, aP = 0.0f, aG = 0.0f;
    float p = 1.0f, gain = 1.0f;
};

} // namespace worldtable::dsp
