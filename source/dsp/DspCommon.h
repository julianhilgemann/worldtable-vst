#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace worldtable::dsp
{

inline constexpr float kPi    = std::numbers::pi_v<float>;
inline constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

/** Internal control-rate chunk: parameters, modulation and events update every kChunk samples. */
inline constexpr int kChunk = 64;

inline float dbToGain (float db) noexcept       { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept        { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float midiToHz (float note) noexcept     { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }

/** One-pole coefficient for a time constant in seconds. */
inline float onePoleCoef (float seconds, double sampleRate) noexcept
{
    if (seconds <= 0.0f)
        return 0.0f;
    return std::exp (-1.0f / (seconds * static_cast<float> (sampleRate)));
}

/** One-pole smoother. Call setTime() in prepare, then next() per sample (or per chunk). */
struct Smoother
{
    float value = 0.0f;
    float coef  = 0.0f;

    void setTime (float seconds, double rate) noexcept { coef = onePoleCoef (seconds, rate); }
    void reset (float v) noexcept                      { value = v; }
    float next (float target) noexcept                 { value = target + (value - target) * coef; return value; }
};

/** Tiny deterministic RNG for the audio thread (xorshift32). */
struct FastRandom
{
    uint32_t state = 0x9E3779B9u;

    void seed (uint32_t s) noexcept { state = s != 0 ? s : 0x9E3779B9u; }

    uint32_t nextUint() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /** Uniform in [0, 1). */
    float nextFloat() noexcept { return static_cast<float> (nextUint() >> 8) * (1.0f / 16777216.0f); }

    /** Uniform in [-1, 1). */
    float nextBipolar() noexcept { return nextFloat() * 2.0f - 1.0f; }
};

/** First-order DC blocker. */
struct DcBlocker
{
    float x1 = 0.0f, y1 = 0.0f, r = 0.999f;

    void prepare (double sampleRate) noexcept { r = 1.0f - (kTwoPi * 10.0f / static_cast<float> (sampleRate)); }
    void reset() noexcept                     { x1 = y1 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

/** Equal-power pan, p in [-1, 1]. */
inline void panGains (float p, float& l, float& r) noexcept
{
    const float a = (std::clamp (p, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    l = std::cos (a);
    r = std::sin (a);
}

} // namespace worldtable::dsp
