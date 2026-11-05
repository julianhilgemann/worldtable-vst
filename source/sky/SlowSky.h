#pragma once

#include "Observer.h"
#include "SkyModel.h"
#include "SkyParams.h"
#include "data/WorldSnapshot.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>

namespace worldtable::sky
{

/** The slow sky (spec/04-sky-model.md §1, §2): one step is 1 s of model time.
    prototype/sky/model.js `initialSlow` and `slowStep`, line by line, in the same order of
    operations. Internal to source/sky: readers use SkySnapshot. */

/** A slow step emits at most a phase change and an onset. */
constexpr int kMaxEventsPerSlowStep = 2;

/** The slow sky at the start of a track (the sketch's initialSlow). */
SlowSkyState initialSlowState (const data::WorldSnapshot& track, const SkyParams& params, double startUtc) noexcept;

/** The same without a track: Kp `kp0` (climatology: 2) stands in for the track's first sample. */
SlowSkyState initialSlowState (const SkyParams& params, double startUtc, double kp0) noexcept;

/** Advances `state` by one slow step (1 s of model time) and writes its events (phase changes and
    onsets) into `events`. Returns how many it wrote. Real-time safe: no allocation, bounded work. */
int slowStep (SlowSkyState& state,
              const data::WorldSnapshot& track,
              const SkyParams& params,
              const Observer& observer,
              uint32_t seed,
              std::span<SkyEvent, kMaxEventsPerSlowStep> events) noexcept;

/** How much of the night side a magnetic local time sees: 1 at midnight, 0.25 at noon. */
inline double nightFactor (double mltHours) noexcept
{
    return 0.25 + 0.75 * (0.5 + 0.5 * std::cos ((2.0 * std::numbers::pi * mltHours) / 24.0));
}

/** The arc's geomagnetic latitude at magnetic midnight (between the oval's edges). */
inline double arcLatitudeDeg (const SlowSkyState& s, const SkyParams& p) noexcept
{
    return s.eqLatDeg + (s.poleLatDeg - s.eqLatDeg) * p.arcPosition;
}

} // namespace worldtable::sky
