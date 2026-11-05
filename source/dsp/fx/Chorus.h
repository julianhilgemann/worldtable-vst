#pragma once

#include "dsp/DspCommon.h"

#include <array>
#include <vector>

namespace worldtable::dsp
{

/** Slow 3-voice ensemble chorus (spec/05-sound-engine.md §7): one delay line per channel, three
    modulated taps at 12 / 17 / 23 ms, 3 ms depth, slow rates. The right channel's LFOs sit half a
    cycle behind, so the voices spread across the field. No feedback; dry/wet. All memory is
    allocated in prepare(). */
class Chorus
{
public:
    struct Settings
    {
        float rateHz = 0.1f;      ///< base LFO rate; the three voices run at 1.0×, 1.22×, 1.44×
        float depthMs = 3.0f;     ///< modulation depth, ms
        float mix = 0.25f;        ///< 0 (bypass) … 1 (wet)
    };

    void prepare (double sampleRate)
    {
        rate = sampleRate;
        size = static_cast<int> (0.05 * rate) + 8;
        bufferL.assign (static_cast<size_t> (size), 0.0f);
        bufferR.assign (static_cast<size_t> (size), 0.0f);
        reset();
    }

    void setSettings (const Settings& s) noexcept
    {
        settings = s;
        settings.rateHz = std::max (s.rateHz, 0.0f);
        settings.depthMs = std::clamp (s.depthMs, 0.0f, 10.0f);
        settings.mix = std::clamp (s.mix, 0.0f, 1.0f);
    }

    void reset() noexcept
    {
        std::fill (bufferL.begin(), bufferL.end(), 0.0f);
        std::fill (bufferR.begin(), bufferR.end(), 0.0f);
        writePos = 0;
        phaseL = 0.0f;
        phaseR = 0.5f;
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        if (bufferL.empty() || settings.mix <= 0.0f)
            return;

        const float increment = static_cast<float> (settings.rateHz / rate);
        const float mix = settings.mix;
        // The wet is the average of three taps (≈ 1/3 of the dry power); compensate the sum.
        const float makeup = 1.0f / std::sqrt (std::max (1.0e-6f, (1.0f - mix) * (1.0f - mix)
                                                      + mix * mix / static_cast<float> (kVoices)));

        for (int i = 0; i < numSamples; ++i)
        {
            bufferL[static_cast<size_t> (writePos)] = left[i];
            bufferR[static_cast<size_t> (writePos)] = right[i];

            float wetL = 0.0f, wetR = 0.0f;
            for (int v = 0; v < kVoices; ++v)
            {
                const auto vi = static_cast<size_t> (v);
                const float rateScale = 1.0f + 0.22f * static_cast<float> (v);
                const float lfoL = std::sin (kTwoPi * (phaseL * rateScale + 0.3333333f * static_cast<float> (v)));
                const float lfoR = std::sin (kTwoPi * (phaseR * rateScale + 0.3333333f * static_cast<float> (v)));
                const float delayL = (kBaseMs[vi] + settings.depthMs * lfoL) * static_cast<float> (rate) * 0.001f;
                const float delayR = (kBaseMs[vi] + settings.depthMs * lfoR) * static_cast<float> (rate) * 0.001f;
                wetL += read (bufferL, delayL);
                wetR += read (bufferR, delayR);
            }

            left[i] = (left[i] * (1.0f - mix) + (wetL / static_cast<float> (kVoices)) * mix) * makeup;
            right[i] = (right[i] * (1.0f - mix) + (wetR / static_cast<float> (kVoices)) * mix) * makeup;

            if (++writePos >= size)
                writePos = 0;
            phaseL += increment;
            phaseR += increment;
            if (phaseL >= 1.0f) phaseL -= 1.0f;
            if (phaseR >= 1.0f) phaseR -= 1.0f;
        }
    }

private:
    static constexpr int kVoices = 3;
    static constexpr std::array<float, kVoices> kBaseMs { 12.0f, 17.0f, 23.0f };

    float read (const std::vector<float>& buffer, float delay) const noexcept
    {
        float pos = static_cast<float> (writePos) - delay;
        while (pos < 0.0f)
            pos += static_cast<float> (size);
        while (pos >= static_cast<float> (size))
            pos -= static_cast<float> (size);
        const auto i0 = static_cast<int> (pos);
        const int i1 = i0 + 1 >= size ? 0 : i0 + 1;
        const float f = pos - static_cast<float> (i0);
        return buffer[static_cast<size_t> (i0)] + f * (buffer[static_cast<size_t> (i1)] - buffer[static_cast<size_t> (i0)]);
    }

    Settings settings;
    double rate = 48000.0;
    int size = 0, writePos = 0;
    std::vector<float> bufferL, bufferR;
    float phaseL = 0.0f, phaseR = 0.5f;
};

} // namespace worldtable::dsp
