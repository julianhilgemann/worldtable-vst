#pragma once

#include "data/TrackBuilder.h"
#include "sky/SkyModel.h"

#include <juce_core/juce_core.h>

#include <memory>

#ifndef WORLDTABLE_GOLDEN_DIR
 #error "WORLDTABLE_GOLDEN_DIR must be defined by tests/CMakeLists.txt"
#endif
#ifndef WORLDTABLE_RESOURCES_DIR
 #error "WORLDTABLE_RESOURCES_DIR must be defined by tests/CMakeLists.txt"
#endif

namespace testing
{
/** The 10 May 2024 storm (resources/sky), prepared, its slow sky built for seed 1 with default
    params, and its goldens (tests/golden/sky). Built once for the whole test binary. */
struct StormDay
{
    std::unique_ptr<worldtable::data::WorldSnapshot> track = std::make_unique<worldtable::data::WorldSnapshot>();
    worldtable::sky::SkyTimeline timeline;
    juce::var golden;
    double buildMs = 0.0;
};

inline const StormDay& stormDay()
{
    static const StormDay day = []
    {
        StormDay d;
        const auto trackFile = juce::File (WORLDTABLE_RESOURCES_DIR).getChildFile ("sky/2024-05-10/track.json");
        worldtable::data::RawTrack raw;
        const bool ok = worldtable::data::parseTrackJson (trackFile.loadFileAsString(), raw)
                     && worldtable::data::prepareTrack (raw, *d.track);
        jassert (ok);
        juce::ignoreUnused (ok);

        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        d.timeline.build (*d.track, 1, worldtable::sky::SkyParams {});
        d.buildMs = juce::Time::getMillisecondCounterHiRes() - t0;

        d.golden = juce::JSON::parse (juce::File (WORLDTABLE_GOLDEN_DIR).getChildFile ("sky/2024-05-10.json").loadFileAsString());
        return d;
    }();
    return day;
}
} // namespace testing
