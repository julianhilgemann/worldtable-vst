#include "SkyFeed.h"

#include "Observer.h"
#include "data/SkyDays.h"

#include <algorithm>

namespace worldtable::sky
{

namespace
{
/** The great-storm red glow, 0 at Kp 5 … 1 at Kp 9 (smoothed Kp): SkyModel::writeSnapshot's
    SkySnapshot::stormRed, which the dome's oval reads. */
double stormRedOf (const SlowSkyState& s) noexcept
{
    return std::clamp ((s.kpSmooth - 5.0) / 4.0, 0.0, 1.0);
}
} // namespace

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
    buildChart (dayId, bundle);
    tracks.publish (slot);   // from here on the slot belongs to the audio side
    return true;
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
    for (auto* v : { &c.dhMeasuredNt, &c.dhModelNt, &c.couplingMvM, &c.reservoir, &c.threshold, &c.eqLatitudeDeg,
                     &c.poleLatitudeDeg, &c.activity, &c.gain, &c.unloading, &c.stormRed, &c.mltHours, &c.kp })
        v->reserve (n);
    c.phase.reserve (n);

    const auto& params = bundle.timeline.params;
    c.stationMlatDeg = Observer (data::stationInfo (track.station), params.poleLatDeg, params.poleLonDeg).mlatDeg();
    c.dayShiftDeg = params.dayShiftDeg;
    c.arcPosition = params.arcPosition;

    // The timeline's minute k is the state *before* that minute's steps; minute k + 1 is the state
    // after them, which is what the sketch's `runSlow` trace records at k.
    const double unit = params.threshold > 0.0 ? params.threshold : 1.0;
    for (size_t k = 0; k < n; ++k)
    {
        const auto& s = minutes[std::min (k + 1, minutes.size() - 1)];
        c.dhMeasuredNt.push_back (static_cast<float> (s.dhNt));
        c.dhModelNt.push_back (static_cast<float> (s.dhModelNt));
        c.couplingMvM.push_back (static_cast<float> (s.couplingSmoothMvM));
        c.reservoir.push_back (static_cast<float> (s.reservoir / unit));
        c.threshold.push_back (static_cast<float> (s.cycleThreshold / unit));
        c.phase.push_back (static_cast<uint8_t> (s.phase));
        c.eqLatitudeDeg.push_back (static_cast<float> (s.eqLatDeg));
        c.poleLatitudeDeg.push_back (static_cast<float> (s.poleLatDeg));
        c.activity.push_back (static_cast<float> (s.activity));
        c.gain.push_back (static_cast<float> (s.gain));
        c.unloading.push_back (static_cast<float> (s.unloading));
        c.stormRed.push_back (static_cast<float> (stormRedOf (s)));
        c.mltHours.push_back (static_cast<float> (s.mltHours));
        c.kp.push_back (static_cast<float> (s.kp));
    }

    for (const auto& e : bundle.timeline.events)
        if (e.type == SkyEvent::Type::onset)
            c.onsets.push_back ({ e.modelTimeUtc, e.strength, e.onsetKind });

    c.sequence = chart.sequence + 1;
    chart = std::move (c);
}

bool SkyFeed::startDefaultJourney()
{
    if (! load (data::kDefaultSkyDay, 1, SkyParams {}))
        return false;

    runtime.seek (loadedStartUtc + kDefaultJourneyStart);
    runtime.setSpeed (kJourneySpeed);
    return true;
}

} // namespace worldtable::sky
