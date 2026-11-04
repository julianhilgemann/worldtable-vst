#pragma once

#include "dsp/DspCommon.h"

#include <vector>

namespace worldtable::dsp
{

/** Stereo-linked lookahead brickwall limiter.

    Gain computer: the required gain for each incoming sample is min-held over the
    lookahead window, released exponentially, then box-filtered over the same window.
    Because the box average of the min-held curve can never exceed the required gain
    when the peak leaves the delay line, the output stays at or below the ceiling
    without hard clipping (a final clamp only guards float rounding). */
class Limiter
{
public:
    void prepare (double rate)
    {
        sampleRate = rate;
        window = std::max (8, static_cast<int> (std::round (rate * kLookaheadSeconds)));
        delayL.assign (static_cast<size_t> (window), 0.0f);
        delayR.assign (static_cast<size_t> (window), 0.0f);
        boxBuf.assign (static_cast<size_t> (window), 1.0f);
        dequeVal.assign (static_cast<size_t> (window) + 1, 1.0f);
        dequeIdx.assign (static_cast<size_t> (window) + 1, 0);
        reset();
    }

    void reset() noexcept
    {
        std::fill (delayL.begin(), delayL.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
        std::fill (boxBuf.begin(), boxBuf.end(), 1.0f);
        boxSum = static_cast<double> (window);
        env = 1.0f;
        pos = 0;
        sampleIndex = 0;
        dqHead = dqTail = 0;
        lastGain = 1.0f;
    }

    /** Latency in samples (the lookahead delay). */
    int getLatency() const noexcept { return window - 1; }

    void setSettings (float inputGainDb, float ceilingDb, float releaseMs) noexcept
    {
        inputGain = dbToGain (inputGainDb);
        ceiling   = dbToGain (std::min (ceilingDb, 0.0f));
        releaseCoef = onePoleCoef (std::max (releaseMs, 1.0f) * 0.001f, sampleRate);
    }

    /** Returns the minimum gain applied in this call (for metering). */
    float process (float* left, float* right, int numSamples) noexcept
    {
        float minGain = 1.0f;
        const int delay = window - 1;

        for (int i = 0; i < numSamples; ++i)
        {
            const float inL = left[i] * inputGain;
            const float inR = right[i] * inputGain;

            const float peak = std::max (std::abs (inL), std::abs (inR));
            const float required = peak > ceiling ? ceiling / peak : 1.0f;

            // sliding-window minimum (monotonic deque over the last `window` samples)
            pushMin (required);
            const float held = dequeVal[static_cast<size_t> (dqHead)];

            env = held < env ? held : held + (env - held) * releaseCoef;

            // box filter
            boxSum += static_cast<double> (env) - static_cast<double> (boxBuf[static_cast<size_t> (pos)]);
            boxBuf[static_cast<size_t> (pos)] = env;
            const float gain = std::min (1.0f, static_cast<float> (boxSum / static_cast<double> (window)));

            // delay line (length window − 1 → read the sample written `delay` steps ago)
            delayL[static_cast<size_t> (pos)] = inL;
            delayR[static_cast<size_t> (pos)] = inR;
            int readPos = pos - delay;
            if (readPos < 0)
                readPos += window;

            const float outL = delayL[static_cast<size_t> (readPos)] * gain;
            const float outR = delayR[static_cast<size_t> (readPos)] * gain;

            left[i]  = std::clamp (outL, -ceiling, ceiling);
            right[i] = std::clamp (outR, -ceiling, ceiling);

            if (++pos >= window)
                pos = 0;
            ++sampleIndex;
            minGain = std::min (minGain, gain);
        }

        // occasional exact recompute keeps the running sum from drifting
        if ((sampleIndex & 0xFFFF) < numSamples)
        {
            double s = 0.0;
            for (auto v : boxBuf)
                s += static_cast<double> (v);
            boxSum = s;
        }

        lastGain = minGain;
        return minGain;
    }

private:
    static constexpr double kLookaheadSeconds = 0.002;

    void pushMin (float value) noexcept
    {
        const int cap = window + 1;
        while (dqTail != dqHead)
        {
            const int last = dqTail == 0 ? cap - 1 : dqTail - 1;
            if (dequeVal[static_cast<size_t> (last)] >= value)
                dqTail = last;
            else
                break;
        }
        dequeVal[static_cast<size_t> (dqTail)] = value;
        dequeIdx[static_cast<size_t> (dqTail)] = sampleIndex;
        dqTail = dqTail + 1 == cap ? 0 : dqTail + 1;

        while (dequeIdx[static_cast<size_t> (dqHead)] <= sampleIndex - window)
            dqHead = dqHead + 1 == cap ? 0 : dqHead + 1;
    }

    double sampleRate = 48000.0;
    int window = 96, pos = 0;
    int64_t sampleIndex = 0;
    std::vector<float> delayL, delayR, boxBuf, dequeVal;
    std::vector<int64_t> dequeIdx;
    int dqHead = 0, dqTail = 0;
    double boxSum = 0.0;
    float env = 1.0f, inputGain = 1.0f, ceiling = 0.891f, releaseCoef = 0.999f, lastGain = 1.0f;
};

} // namespace worldtable::dsp
