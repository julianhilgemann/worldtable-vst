#pragma once

#include "dsp/DspCommon.h"

#include <vector>

namespace worldtable::dsp
{

/** Tempo-synced stereo delay with optional ping-pong, a tone-controlled (low-pass)
    and high-passed feedback path, gentle tape-style wobble and soft saturation in the
    loop so high feedback stays musical. Delay-time changes glide (no clicks). */
class StereoDelay
{
public:
    void prepare (double rate)
    {
        sampleRate = rate;
        const auto size = static_cast<size_t> (rate * kMaxSeconds) + 8;
        bufL.assign (size, 0.0f);
        bufR.assign (size, 0.0f);
        timeSmoother.setTime (0.25f, rate);
        timeSmoother.reset (static_cast<float> (0.5 * rate));
        wobbleInc = 0.31 / rate;
        reset();
    }

    void reset() noexcept
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        lpL = lpR = hpL = hpR = 0.0f;
        writePos = 0;
    }

    struct Settings
    {
        float timeSeconds = 0.5f, feedback = 0.4f, tone = 0.5f, mix = 0.2f;
        bool pingPong = true;
    };

    /** Call once per chunk. */
    void setSettings (const Settings& s) noexcept
    {
        settings = s;
        targetSamples = std::clamp (s.timeSeconds * static_cast<float> (sampleRate),
                                    16.0f, static_cast<float> (bufL.size()) - 8.0f);
        const float cutoff = 900.0f * std::pow (20.0f, s.tone);    // 0.9 … 18 kHz
        lpCoef = 1.0f - std::exp (-kTwoPi * cutoff / static_cast<float> (sampleRate));
        hpCoef = 1.0f - std::exp (-kTwoPi * 90.0f / static_cast<float> (sampleRate));
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        if (bufL.empty())
            return;

        const auto size = static_cast<int> (bufL.size());
        const float fb = std::clamp (settings.feedback, 0.0f, 0.95f);
        const float wet = settings.mix;

        for (int i = 0; i < numSamples; ++i)
        {
            const float delaySamples = timeSmoother.next (targetSamples);
            wobblePhase += wobbleInc;
            if (wobblePhase >= 1.0)
                wobblePhase -= 1.0;
            const float wobble = 0.0004f * static_cast<float> (sampleRate)
                               * std::sin (kTwoPi * static_cast<float> (wobblePhase));

            const float outL = read (bufL, delaySamples + wobble, size);
            const float outR = read (bufR, delaySamples - wobble, size);

            // Feedback tone: low-pass then high-pass, soft-saturated.
            lpL += lpCoef * (outL - lpL);
            lpR += lpCoef * (outR - lpR);
            hpL += hpCoef * (lpL - hpL);
            hpR += hpCoef * (lpR - hpR);
            const float fbL = std::tanh ((lpL - hpL) * fb);
            const float fbR = std::tanh ((lpR - hpR) * fb);

            const float inL = left[i], inR = right[i];
            if (settings.pingPong)
            {
                bufL[static_cast<size_t> (writePos)] = 0.5f * (inL + inR) + fbR;
                bufR[static_cast<size_t> (writePos)] = fbL;
            }
            else
            {
                bufL[static_cast<size_t> (writePos)] = inL + fbL;
                bufR[static_cast<size_t> (writePos)] = inR + fbR;
            }

            if (++writePos >= size)
                writePos = 0;

            left[i]  = inL + wet * outL;
            right[i] = inR + wet * outR;
        }
    }

private:
    static constexpr double kMaxSeconds = 4.2;

    float read (const std::vector<float>& buf, float delay, int size) const noexcept
    {
        // Wrap integer indices only (a wrapped float can round to exactly `size`).
        // `delay` counts from the next write position, as before: writePos − delay.
        const auto whole = static_cast<int> (delay);
        const float frac = delay - static_cast<float> (whole);   // weight of the older sample
        int newer = writePos - whole;
        while (newer < 0)
            newer += size;
        if (newer >= size)
            newer -= size;
        const int older = newer == 0 ? size - 1 : newer - 1;
        const float a = buf[static_cast<size_t> (newer)];
        return a + frac * (buf[static_cast<size_t> (older)] - a);
    }

    double sampleRate = 48000.0;
    std::vector<float> bufL, bufR;
    int writePos = 0;
    Settings settings;
    float targetSamples = 24000.0f;
    Smoother timeSmoother;
    float lpCoef = 0.5f, hpCoef = 0.01f, lpL = 0, lpR = 0, hpL = 0, hpR = 0;
    double wobblePhase = 0.0, wobbleInc = 0.0;
};

} // namespace worldtable::dsp
