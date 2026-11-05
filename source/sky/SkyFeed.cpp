#include "SkyFeed.h"

#include "data/SkyDays.h"

namespace worldtable::sky
{

SkyFeed::SkyFeed (SkyRuntime& r) : runtime (r) {}

bool SkyFeed::load (const juce::String& dayId, uint32_t seed, const SkyParams& params)
{
    data::RawTrack raw;
    if (! data::loadBundledDay (dayId, raw))
        return false;

    auto& tracks = runtime.tracks();
    const int slot = tracks.acquireForWriting();
    if (slot < 0)
        return false;

    auto& bundle = tracks.slot (slot);
    if (! data::prepareTrack (raw, bundle.track, ++sequence))
        return false;

    bundle.timeline.build (bundle.track, seed, params);
    loadedDay = dayId;
    loadedStartUtc = bundle.track.startUtc;
    tracks.publish (slot);   // from here on the slot belongs to the audio side
    return true;
}

bool SkyFeed::startDefaultJourney()
{
    if (! load (data::kDefaultSkyDay, 1, SkyParams {}))
        return false;

    runtime.seek (loadedStartUtc);
    runtime.setSpeed (kJourneySpeed);
    return true;
}

} // namespace worldtable::sky
