#include "SkyFeed.h"

#include "data/SkyDays.h"

#include <algorithm>
#include <cmath>

namespace worldtable::sky
{

SkyFeed::SkyFeed (SkyRuntime& r) : runtime (r) {}

int SkyFeed::build (const juce::String& dayId, uint32_t seed, const SkyParams& params)
{
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    data::RawTrack raw;
    if (! data::loadBundledDay (dayId, raw))
        return -1;

    auto& tracks = runtime.tracks();
    const int slot = tracks.acquireForWriting();
    if (slot < 0)
        return -1;

    auto& bundle = tracks.slot (slot);
    if (! data::prepareTrack (raw, bundle.track, ++sequence))
        return -1;

    bundle.timeline.build (bundle.track, seed, params);
    bundle.landingUtc = std::numeric_limits<double>::quiet_NaN();
    bundle.landingTicket = 0;
    loadedDay = dayId;
    loadedStartUtc = bundle.track.startUtc;
    loadedEndUtc = bundle.track.endUtc();
    buildChart (dayId, bundle);
    loadMs = juce::Time::getMillisecondCounterHiRes() - t0;
    return slot;
}

bool SkyFeed::load (const juce::String& dayId, uint32_t seed, const SkyParams& params)
{
    const int slot = build (dayId, seed, params);
    if (slot < 0)
        return false;
    runtime.tracks().publish (slot);   // from here on the slot belongs to the audio side
    return true;
}

bool SkyFeed::loadJourney (const juce::String& dayId, double utc)
{
    const int slot = build (dayId, 1, SkyParams {});
    if (slot < 0)
        return false;
    runtime.land (slot, std::isfinite (utc) ? utc : loadedStartUtc + journeyStart (dayId));
    return true;
}

double SkyFeed::journeyStart (const juce::String& dayId) noexcept
{
    if (dayId == "2024-05-10") return kDefaultJourneyStart;     // 17:20 UT, the storm's onset window
    if (dayId == "2025-01-19") return 9.0 * 3600.0 + 30.0 * 60.0;   // 09:30 UT
    if (dayId == "2025-01-25") return 9.0 * 3600.0;                 // 09:00 UT
    return 0.0;
}

void SkyFeed::buildChart (const juce::String& dayId, const TrackBundle& bundle)
{
    const auto& track = bundle.track;
    const auto& minutes = bundle.timeline.minutes;
    const auto n = static_cast<size_t> (std::min (static_cast<int> (minutes.size()), track.length));

    DayChart c;
    c.dayId = dayId;
    c.station = track.station;
    c.startUtc = track.startUtc;
    c.stepSeconds = track.stepSeconds;
    c.dhMeasuredNt.reserve (n);
    c.dhModelNt.reserve (n);
    c.couplingMvM.reserve (n);
    c.reservoir.reserve (n);
    c.threshold.reserve (n);
    c.phase.reserve (n);

    // The timeline's minute k is the state *before* that minute's steps; minute k + 1 is the state
    // after them, which is what the sketch's `runSlow` trace records at k.
    const double unit = bundle.timeline.params.threshold > 0.0 ? bundle.timeline.params.threshold : 1.0;
    for (size_t k = 0; k < n; ++k)
    {
        const auto& s = minutes[std::min (k + 1, minutes.size() - 1)];
        c.dhMeasuredNt.push_back (static_cast<float> (s.dhNt));
        c.dhModelNt.push_back (static_cast<float> (s.dhModelNt));
        c.couplingMvM.push_back (static_cast<float> (s.couplingSmoothMvM));
        c.reservoir.push_back (static_cast<float> (s.reservoir / unit));
        c.threshold.push_back (static_cast<float> (s.cycleThreshold / unit));
        c.phase.push_back (static_cast<uint8_t> (s.phase));
    }

    for (const auto& e : bundle.timeline.events)
        if (e.type == SkyEvent::Type::onset)
            c.onsets.push_back ({ e.modelTimeUtc, e.strength, e.onsetKind });

    c.sequence = chart.sequence + 1;
    chart = std::move (c);
}

bool SkyFeed::startDefaultJourney()
{
    if (! loadJourney (data::kDefaultSkyDay))
        return false;

    runtime.setSpeed (kJourneySpeed);
    return true;
}

} // namespace worldtable::sky
