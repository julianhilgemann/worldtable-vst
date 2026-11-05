#pragma once

#include "DspCommon.h"

namespace worldtable::dsp
{

/** Topology-preserving state-variable filter (Simper/Zavalishin trapezoidal SVF).
    Stable under fast cutoff modulation. Coefficients are shared by callers that
    run several channels at the same cutoff. */
struct SvfCoefs
{
    float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, k = 2.0f;

    void set (float cutoffHz, float damping, double sampleRate) noexcept
    {
        const float fc = std::clamp (cutoffHz, 10.0f, 0.47f * static_cast<float> (sampleRate));
        const float g = std::tan (kPi * fc / static_cast<float> (sampleRate));
        k  = damping;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
};

struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f;

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    struct Out { float low, band, high; };

    Out process (float v0, const SvfCoefs& c) noexcept
    {
        const float v3 = v0 - ic2;
        const float v1 = c.a1 * ic1 + c.a2 * v3;
        const float v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return { v2, v1, v0 - c.k * v1 - v2 };
    }
};

/** Resonance 0..1 → SVF damping k (2 = no resonance, → 0 = self-oscillation). */
inline float resonanceToDamping (float res) noexcept
{
    return 2.0f - 1.94f * std::clamp (res, 0.0f, 1.0f);
}

} // namespace worldtable::dsp
