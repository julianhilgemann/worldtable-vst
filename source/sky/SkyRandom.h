#pragma once

#include <cmath>
#include <cstdint>

namespace worldtable::sky::rng
{

/** Counter-based, seeded randomness: prototype/sky/rng.js, bit for bit for the integer part.
    Every value is a pure function of (seed, stream, index): nothing to drift, identical on every
    run (CLAUDE.md rule 5), and safe on the audio thread. */

/** lowbias32 integer hash (C. Wellons). */
constexpr uint32_t hash32 (uint32_t x) noexcept
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/** JavaScript's ToUint32 for the integer-valued doubles the sketch passes as indices
    (floor of a time, a cycle count), including negative ones. */
inline uint32_t indexOf (double integral) noexcept
{
    return static_cast<uint32_t> (static_cast<int64_t> (integral));
}

/** Uniform [0, 1). */
constexpr double rand01 (uint32_t seed, uint32_t stream, uint32_t index) noexcept
{
    return static_cast<double> (hash32 (seed ^ hash32 (stream ^ hash32 (index)))) / 4294967296.0;
}

/** Uniform [−1, 1). */
constexpr double randS (uint32_t seed, uint32_t stream, uint32_t index) noexcept
{
    return rand01 (seed, stream, index) * 2.0 - 1.0;
}

constexpr double smooth (double t) noexcept { return t * t * (3.0 - 2.0 * t); }

/** Smooth 1-D value noise in [−1, 1]: knots at integer x, smoothstep between. */
inline double noise1 (uint32_t seed, uint32_t stream, double x) noexcept
{
    const double i = std::floor (x);
    const double f = x - i;
    const auto k = static_cast<int32_t> (i);
    const double a = randS (seed, stream, static_cast<uint32_t> (k));
    const double b = randS (seed, stream, static_cast<uint32_t> (k + 1));
    return a + (b - a) * smooth (f);
}

/** Smooth 2-D value noise in [−1, 1] (x: along the curtain, y: time). */
inline double noise2 (uint32_t seed, uint32_t stream, double x, double y) noexcept
{
    const double fx0 = std::floor (x), fy0 = std::floor (y);
    const auto ix = static_cast<uint32_t> (static_cast<int32_t> (fx0));
    const auto iy = static_cast<uint32_t> (static_cast<int32_t> (fy0));
    const double fx = smooth (x - fx0), fy = smooth (y - fy0);
    const auto k = [seed, stream] (uint32_t a, uint32_t b) noexcept
    {
        return randS (seed, stream, (a * 73856093u) ^ (b * 19349663u));
    };
    const double v00 = k (ix, iy), v10 = k (ix + 1u, iy), v01 = k (ix, iy + 1u), v11 = k (ix + 1u, iy + 1u);
    const double top = v00 + (v10 - v00) * fx;
    const double bot = v01 + (v11 - v01) * fx;
    return top + (bot - top) * fy;
}

/** Two octaves of noise2, roughly in [−1, 1]. */
inline double fbm2 (uint32_t seed, uint32_t stream, double x, double y) noexcept
{
    return 0.67 * noise2 (seed, stream, x, y) + 0.33 * noise2 (seed, stream + 101u, x * 2.13, y * 2.07);
}

} // namespace worldtable::sky::rng
