#include "ModMatrix.h"

#include <algorithm>
#include <cmath>

namespace worldtable::dsp::mod
{

namespace
{
float finiteOr0 (float v) noexcept { return std::isfinite (v) ? v : 0.0f; }

/** One route's factor on a mul column: never below 0 (a route pushing down silences at most). */
float factorOf (float depth, float source) noexcept { return std::max (0.0f, 1.0f + depth * source); }

/** A source's range for the rings: [lo, hi] of kSources, Breath's top being its knob. */
void rangeOf (Source s, const SourceValues& v, float& lo, float& hi) noexcept
{
    const auto& info = kSources[indexOf (s)];
    lo = info.lo;
    hi = s == Source::breath ? std::clamp (finiteOr0 (v.breathKnob), 0.0f, 1.0f) : info.hi;
}
} // namespace

//==================================================================================================
void ModMatrix::reset (const Depths& depths) noexcept
{
    heard = depths;
    wanted = depths;
}

void ModMatrix::setDepths (const Depths& depths) noexcept
{
    wanted = depths;
}

bool ModMatrix::gliding() const noexcept
{
    for (size_t s = 0; s < static_cast<size_t> (kNumSources); ++s)
        for (size_t d = 0; d < static_cast<size_t> (kNumDestinations); ++d)
            if (std::abs (heard.cells[s][d] - wanted.cells[s][d]) > 0.0f)
                return true;
    return false;
}

void ModMatrix::tick (const SourceValues& sources, const Bases& bases, Frame& out) noexcept
{
    // The depths move linearly toward the newest table, kDepthPerTick at most, and land on it
    // exactly (the last step snaps, so float rounding never leaves a residue).
    for (size_t s = 0; s < static_cast<size_t> (kNumSources); ++s)
        for (size_t d = 0; d < static_cast<size_t> (kNumDestinations); ++d)
        {
            float& h = heard.cells[s][d];
            const float w = wanted.cells[s][d];
            const float gap = w - h;
            if (std::abs (gap) <= kDepthPerTick + 1.0e-6f)
                h = w;
            else
                h += gap > 0.0f ? kDepthPerTick : -kDepthPerTick;
        }
    evaluate (heard, sources, bases, out);
}

//==================================================================================================
void ModMatrix::evaluate (const Depths& depths, const SourceValues& sources, const Bases& bases, Frame& out) noexcept
{
    for (size_t s = 0; s < static_cast<size_t> (kNumSources); ++s)
        out.source[s] = finiteOr0 (sources.value[s]);

    for (size_t d = 0; d < static_cast<size_t> (kNumDestinations); ++d)
    {
        const bool product = kDestinations[d].law == Law::mul;
        float now = product ? 1.0f : 0.0f, lo = now, hi = now;
        bool routed = false;
        for (size_t s = 0; s < static_cast<size_t> (kNumSources); ++s)
        {
            const float depth = depths.cells[s][d];
            if (! (std::abs (depth) > 0.0f))
                continue;
            routed = true;
            float sLo = 0.0f, sHi = 0.0f;
            rangeOf (static_cast<Source> (s), sources, sLo, sHi);
            const float v = out.source[s];
            if (product)
            {
                const float a = factorOf (depth, sLo), b = factorOf (depth, sHi);
                now *= factorOf (depth, v);
                lo *= std::min (a, b);   // every factor is ≥ 0: the product's extremes are the extremes' products
                hi *= std::max (a, b);
            }
            else
            {
                const float a = depth * sLo, b = depth * sHi;
                now += depth * v;
                lo += std::min (a, b);
                hi += std::max (a, b);
            }
        }
        if (product)
        {
            now -= 1.0f;   // exactly 0 when every factor is exactly 1
            lo -= 1.0f;
            hi -= 1.0f;
        }
        if (! routed)
            now = lo = hi = 0.0f;
        out.amount[d] = now;
        out.amountLo[d] = lo;
        out.amountHi[d] = hi;
    }

    for (size_t t = 0; t < static_cast<size_t> (kNumTargets); ++t)
    {
        const auto target = static_cast<Target> (t);
        const auto column = indexOf (kTargets[t].column);
        const float base = bases.value[t];
        out.value[t] = apply (target, base, out.amount[column]);
        const float a = apply (target, base, out.amountLo[column]);
        const float b = apply (target, base, out.amountHi[column]);
        out.lo[t] = std::min (a, b);
        out.hi[t] = std::max (a, b);
    }
}

float ModMatrix::apply (Target target, float base, float amount) noexcept
{
    if (! std::isfinite (amount) || ! (std::abs (amount) > 0.0f))
        return base;

    const auto& info = infoOf (target);
    float v = base;
    switch (lawOf (target))
    {
        case Law::mul: v = base * (1.0f + amount); break;
        case Law::add: v = base + amount * 0.5f * (info.max - info.min); break;
        case Law::oct: v = base * std::exp2 (kOctavesPerUnit * amount); break;
    }
    if (! std::isfinite (v))
        return base;
    return std::clamp (v, info.modMin, info.modMax);
}

} // namespace worldtable::dsp::mod
