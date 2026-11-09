#pragma once

#include "dsp/DspCommon.h"

#include <array>
#include <cmath>

namespace worldtable::dsp
{

/** One topology-preserving state-variable filter stage (Simper/Zavalishin trapezoidal SVF), the
    POC's `Svf.h` (TP.1). Stable under fast cutoff modulation; low, band and high come out of the
    same state. */
struct SvfStage
{
    float ic1 = 0.0f, ic2 = 0.0f;

    struct Out
    {
        float low, band, high;
    };

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    /** g = tan(π fc / fs), k = damping (2: no resonance … → 0: self-oscillation). */
    Out process (float v0, float g, float k) noexcept
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return { v2, v1, v0 - k * v1 - v2 };
    }
};

/** The filter between the sources and Drive (T3.7, spec/05 §7): two SVF stages per channel.
    LP12 is stage 1's low output, LP24 a second stage (no resonance) on it, BP stage 1's band
    (normalised to unity peak), HP stage 1's high.

    Nothing jumps (rule 4): the cutoff glides per sample in log frequency (τ 20 ms) with its
    coefficients computed per sample, and a type change crossfades the outputs over ~20 ms. The
    sky opens it: `setSkyOctaves` adds octaves to the cutoff (the caller slews it ≥ 2 s).
    Allocation-free; `process` works in place. */
class Filter
{
public:
    enum Type : int { lp12 = 0, lp24 = 1, bp = 2, hp = 3 };
    static constexpr int kTypes = 4;

    static constexpr float kMinHz = 20.0f;
    static constexpr float kMaxHz = 20000.0f;
    static constexpr float kMinDamping = 0.25f;     ///< resonance 1: Q 4, +12 dB at the cutoff
    static constexpr float kGlideSeconds = 0.02f;

    struct Settings
    {
        int type = lp12;
        float cutoffHz = kMaxHz;
        float resonance = 0.1f;   ///< 0 … 1
    };

    /** Resonance 0 … 1 → SVF damping 2 … kMinDamping. */
    static float damping (float resonance) noexcept
    {
        return 2.0f - (2.0f - kMinDamping) * std::clamp (resonance, 0.0f, 1.0f);
    }

    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate;
        glide = onePoleCoef (kGlideSeconds, sampleRate);
        maxHz = std::min (kMaxHz, 0.45f * static_cast<float> (sampleRate));
        reset();
    }

    /** Snaps every glide to the current settings and clears the state. */
    void reset() noexcept
    {
        for (auto& c : stages)
            for (auto& s : c)
                s.reset();
        logHz = targetLogHz();
        k = damping (settings.resonance);
        weights.fill (0.0f);
        weights[static_cast<size_t> (settings.type)] = 1.0f;
    }

    void setSettings (const Settings& s) noexcept
    {
        settings = s;
        settings.type = std::clamp (s.type, 0, kTypes - 1);
        settings.cutoffHz = std::clamp (s.cutoffHz, kMinHz, kMaxHz);
        settings.resonance = std::clamp (s.resonance, 0.0f, 1.0f);
    }

    /** The sky's opening in octaves (signed), added to the cutoff. */
    void setSkyOctaves (float octaves) noexcept { skyOctaves = octaves; }

    /** Opening direction for the type: LP and BP open upwards, HP downwards. */
    static float openDirection (int type) noexcept { return type == hp ? -1.0f : 1.0f; }

    /** The cutoff the filter is at now (after the sky and the glide), Hz. */
    float currentHz() const noexcept { return std::exp2 (logHz); }
    const Settings& getSettings() const noexcept { return settings; }

    void process (float* left, float* right, int numSamples) noexcept
    {
        const float target = targetLogHz();
        const float targetK = damping (settings.resonance);
        const float piOverRate = kPi / static_cast<float> (rate);
        const auto t = static_cast<size_t> (settings.type);

        for (int i = 0; i < numSamples; ++i)
        {
            logHz = target + (logHz - target) * glide;
            k = targetK + (k - targetK) * glide;
            for (size_t w = 0; w < weights.size(); ++w)
                weights[w] = (w == t ? 1.0f : 0.0f) + (weights[w] - (w == t ? 1.0f : 0.0f)) * glide;

            const float hz = std::clamp (std::exp2 (logHz), kMinHz, maxHz);
            const float g = std::tan (piOverRate * hz);
            left[i] = step (stages[0], left[i], g);
            right[i] = step (stages[1], right[i], g);
        }
    }

private:
    float targetLogHz() const noexcept
    {
        const float hz = std::clamp (settings.cutoffHz * std::exp2 (skyOctaves), kMinHz, maxHz);
        return std::log2 (hz);
    }

    float step (std::array<SvfStage, 2>& s, float x, float g) noexcept
    {
        const auto a = s[0].process (x, g, k);
        const auto b = s[1].process (a.low, g, 1.41421356f);
        return weights[lp12] * a.low + weights[lp24] * b.low + weights[bp] * (k * a.band) + weights[hp] * a.high;
    }

    Settings settings;
    double rate = 48000.0;
    float glide = 0.0f, maxHz = kMaxHz;
    float logHz = 14.29f, k = 2.0f, skyOctaves = 0.0f;
    std::array<float, kTypes> weights { 1.0f, 0.0f, 0.0f, 0.0f };
    std::array<std::array<SvfStage, 2>, 2> stages {};   ///< [channel][stage]
};

} // namespace worldtable::dsp
