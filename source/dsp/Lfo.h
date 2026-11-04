#pragma once

#include "DspCommon.h"
#include "params/Params.h"

namespace worldtable::dsp
{

/** Free-running LFO, bipolar output in [-1, 1]. Phase is double precision so very
    slow rates do not drift. Output is lightly smoothed so square and S&H never click. */
class Lfo
{
public:
    void prepare (double rate, uint32_t seed) noexcept
    {
        sampleRate = rate;
        rng.seed (seed);
        smoother.setTime (0.004f, rate);
        held = rng.nextBipolar();
        driftFrom = rng.nextBipolar();
        driftTo = rng.nextBipolar();
    }

    void setRate (float hz) noexcept   { increment = static_cast<double> (hz) / sampleRate; }
    void setShape (int s) noexcept     { shape = s; }
    double getPhase() const noexcept   { return phase; }
    float getValue() const noexcept    { return smoother.value; }

    float next() noexcept
    {
        phase += increment;
        if (phase >= 1.0)
        {
            phase -= 1.0;
            held = rng.nextBipolar();
            driftFrom = driftTo;
            driftTo = rng.nextBipolar();
        }

        const auto p = static_cast<float> (phase);
        float v = 0.0f;
        switch (shape)
        {
            case params::LfoShape::kSine:     v = std::sin (kTwoPi * p); break;
            case params::LfoShape::kTriangle:
            {
                float q = p + 0.25f;
                if (q >= 1.0f) q -= 1.0f;
                v = 1.0f - 4.0f * std::abs (q - 0.5f);
                break;
            }
            case params::LfoShape::kSaw:      v = 2.0f * p - 1.0f; break;
            case params::LfoShape::kSquare:   v = p < 0.5f ? 1.0f : -1.0f; break;
            case params::LfoShape::kSAndH:    v = held; break;
            case params::LfoShape::kDrift:
            default:
            {
                // smoothstep between random points: slow, organic wandering
                const float t = p * p * (3.0f - 2.0f * p);
                v = driftFrom + (driftTo - driftFrom) * t;
                break;
            }
        }
        return smoother.next (v);
    }

private:
    double sampleRate = 48000.0, phase = 0.0, increment = 0.0;
    int shape = 0;
    float held = 0.0f, driftFrom = 0.0f, driftTo = 0.0f;
    FastRandom rng;
    Smoother smoother;
};

} // namespace worldtable::dsp
