#include "SlowLfo.h"

// T4.2 step 1: STUBS (see ModMatrix.cpp). Outputs are zeros; the engine PR implements them.

namespace worldtable::dsp::mod
{

void SlowLfo::prepare (uint32_t newSeed, uint32_t newStream) noexcept
{
    seed = newSeed;
    stream = newStream;
    position = 0.0;
    cycles = 0;
    previous = current;
    fade = 1.0;
    out = 0.0f;
}

void SlowLfo::setRate (double newHz) noexcept
{
    hz = newHz;
}

void SlowLfo::setShape (Shape shape, bool) noexcept
{
    current = shape;
}

float SlowLfo::tick() noexcept
{
    return out;
}

SlowLfo::View SlowLfo::view() const noexcept
{
    return {};
}

float SlowLfo::shapeAt (Shape, double, uint32_t, uint32_t, uint64_t, double) noexcept
{
    return 0.0f;
}

float SlowLfo::knot (uint32_t, uint32_t, uint64_t) noexcept
{
    return 0.0f;
}

} // namespace worldtable::dsp::mod
