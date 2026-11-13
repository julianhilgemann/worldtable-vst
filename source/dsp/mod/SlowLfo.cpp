#include "SlowLfo.h"

#include "sky/SkyRandom.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace worldtable::dsp::mod
{

namespace
{
constexpr uint32_t kKnotStream = 0x4C460000u;   ///< "LF": apart from the sound's other streams

double smoothstep (double t) noexcept { return t * t * (3.0 - 2.0 * t); }
} // namespace

void SlowLfo::prepare (uint32_t newSeed, uint32_t newStream) noexcept
{
    seed = newSeed;
    stream = newStream;
    position = 0.0;
    cycles = 0;
    fade = 1.0;
    out = shapeAt (current, hz, seed, stream, cycles, position);
    fadeFrom = out;
}

void SlowLfo::setRate (double newHz) noexcept
{
    hz = std::isfinite (newHz) ? std::clamp (newHz, kMinHz, kMaxHz) : kMinHz;
}

void SlowLfo::setShape (Shape shape, bool withFade) noexcept
{
    if (! withFade)
    {
        current = shape;
        fade = 1.0;
        out = shapeAt (current, hz, seed, stream, cycles, position);
        fadeFrom = out;
        return;
    }
    if (shape == current)
        return;
    current = shape;
    fadeFrom = out;   // from the value heard now, so a change mid-fade doesn't jump either
    fade = 0.0;
}

float SlowLfo::tick() noexcept
{
    position += hz * kTickSeconds;
    while (position >= 1.0)   // at most once: hz · kTickSeconds ≤ 0.02
    {
        position -= 1.0;
        ++cycles;
    }

    const float now = shapeAt (current, hz, seed, stream, cycles, position);
    if (fade < 1.0)
    {
        fade = std::min (1.0, fade + kTickSeconds / kShapeFadeSeconds);
        out = static_cast<float> (fadeFrom + (now - fadeFrom) * fade);
    }
    else
    {
        out = now;
    }
    return out;
}

SlowLfo::View SlowLfo::view() const noexcept
{
    View v;
    v.phase = position;
    v.cycle = cycles;
    v.rateHz = static_cast<float> (hz);
    v.value = out;
    v.shape = current;
    for (int i = 0; i < kViewKnots; ++i)
        v.knots[static_cast<size_t> (i)] = knot (seed, stream, cycles + static_cast<uint64_t> (i) - 2u);
    return v;
}

float SlowLfo::shapeAt (Shape shape, double rateHz, uint32_t s, uint32_t st, uint64_t cycle, double p) noexcept
{
    constexpr double kTwoPi = 2.0 * std::numbers::pi;
    switch (shape)
    {
        case Shape::sine:     return static_cast<float> (std::sin (kTwoPi * p));
        case Shape::triangle: return static_cast<float> (1.0 - 4.0 * std::abs (p - 0.5));
        case Shape::smoothRandom:
        {
            const double a = knot (s, st, cycle), b = knot (s, st, cycle + 1u);
            return static_cast<float> (a + (b - a) * smoothstep (p));
        }
        case Shape::steppedRandom:
        {
            const double k = knot (s, st, cycle);
            // the step glides from the last knot over kStepGlideSeconds (at most a quarter cycle)
            const double glide = std::min (0.25, kStepGlideSeconds * std::clamp (rateHz, kMinHz, kMaxHz));
            if (cycle == 0 || p >= glide)
                return static_cast<float> (k);
            const double prev = knot (s, st, cycle - 1u);
            return static_cast<float> (prev + (k - prev) * smoothstep (p / glide));
        }
    }
    return 0.0f;
}

float SlowLfo::knot (uint32_t s, uint32_t st, uint64_t index) noexcept
{
    const auto lo = static_cast<uint32_t> (index), hi = static_cast<uint32_t> (index >> 32);
    return static_cast<float> (sky::rng::randS (s, kKnotStream + st, lo ^ sky::rng::hash32 (hi)));
}

} // namespace worldtable::dsp::mod
