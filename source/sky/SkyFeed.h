#pragma once

#include "SkyRuntime.h"

#include <juce_core/juce_core.h>

namespace worldtable::sky
{

/** The data side of the Sky Model (ADR-0003, hand-off A): turns a recorded day into a TrackBundle
    (prepared track + slow-sky timeline) in a free slot and publishes it to the SkyRuntime.

    M2: one bundled day, loaded once when the plug-in is created. Day choice, Live data and a
    background thread come with M5. Message or data thread only: it allocates. */
class SkyFeed
{
public:
    static constexpr double kJourneySpeed = 24.0;   ///< spec/01 §3: 24 h → 1 h

    /** Where the default Journey starts: 17:20 UT on the storm day, the onset window every M3
        render uses. Starting at 00:00 would open 43 real minutes before the storm. The autopilot
        (T4.4) owns the mode and the start from here. */
    static constexpr double kDefaultJourneyStart = 17.0 * 3600.0 + 20.0 * 60.0;

    explicit SkyFeed (SkyRuntime&);

    /** Builds `dayId` for `seed` and `params` and publishes it. Returns false for an unknown day or
        when no slot is free. */
    bool load (const juce::String& dayId, uint32_t seed, const SkyParams& params);

    /** The default Journey: the 10–11 May 2024 storm, seed 1, default params, 24× from the start
        of the day. */
    bool startDefaultJourney();

    const juce::String& dayId() const noexcept { return loadedDay; }
    double dayStartUtc() const noexcept { return loadedStartUtc; }

private:
    SkyRuntime& runtime;
    juce::String loadedDay;
    double loadedStartUtc = 0.0;
    uint64_t sequence = 0;
};

} // namespace worldtable::sky
