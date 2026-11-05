#pragma once

#include "WorldSnapshot.h"

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

namespace worldtable::data
{

/** A track as recorded (resources/sky/<day>/track.json): one value per step and channel, NaN where
    nothing was measured. Channel 0 holds absolute H in nT; prepareTrack() subtracts the quiet level. */
struct RawTrack
{
    double    startUtc    = 0.0;
    double    stepSeconds = 60.0;
    StationId station     = StationId::CMO;
    std::array<std::vector<double>, WorldSnapshot::kChannels> channels;

    int length() const noexcept { return static_cast<int> (channels[0].size()); }
};

/** Reads a track.json (`start`, `step_s`, `channels.{h, kp, v, n, bz, by}` with nulls for gaps).
    Returns false if it is malformed or the channels differ in length. Data or message thread. */
bool parseTrackJson (const juce::String& json, RawTrack& out);

/** The data thread's preparation of a track (prototype/sky/data.js `prepareTrack`):
    - the quiet level of H: median of the calmest 6 h (quietBaseline);
    - gaps of up to 10 samples interpolated; longer ones hold the last value and decay toward
      climatology (Kp 2, 400 km/s, 5 cm⁻³, Bz and By 0, H its quiet level) with τ 60 samples;
    - valid flags for measured samples.
    Writes into a pre-allocated `out` (it is 130 KB). Returns false if the track has fewer than 2 or
    more than WorldSnapshot::kCapacity samples. Assumes 1-minute steps, as the sketch does. */
bool prepareTrack (const RawTrack& raw, WorldSnapshot& out, uint64_t sequence = 0);

/** Median H of the calmest 6 h stretch (lowest standard deviation), NaN = missing. */
double quietBaseline (const std::vector<double>& h);

} // namespace worldtable::data
