#pragma once

#include "SkyRuntime.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace worldtable::sky
{

/** The whole loaded day at a glance, for the UI's day strip (spec/06, T6.1; the keogram since
    T6.10): one entry per model minute of the track, taken from the timeline the feed publishes
    (the sketch's `runSlow` trace). Built on the feed's thread when a day loads, so the editor never
    reads the audio side's bundle. Entry k is the slow sky after minute k's steps, at
    startUtc + stepSeconds × (k + 1). */
struct DayChart
{
    struct Onset
    {
        double    utc;
        float     strength;
        OnsetKind kind;
    };

    juce::String dayId;
    data::StationId station = data::StationId::CMO;
    double startUtc = 0.0;
    double stepSeconds = 60.0;
    std::vector<float>   dhMeasuredNt;   ///< station H deviation, measured
    std::vector<float>   dhModelNt;      ///< … as the model predicts it
    std::vector<float>   couplingMvM;    ///< solar-wind coupling v·max(0, −Bz), smoothed (τ of the model)
    std::vector<float>   reservoir;      ///< stored energy W, in units of SkyParams::threshold
    std::vector<float>   threshold;      ///< the cycle's jittered threshold, same unit
    std::vector<uint8_t> phase;          ///< SkyPhase
    std::vector<Onset>   onsets;

    // T6.10: what the page's keogram needs, the oval as the dome's shader (FS_OVAL) reads it
    std::vector<float>   eqLatitudeDeg;     ///< the oval's equatorward edge at magnetic midnight
    std::vector<float>   poleLatitudeDeg;   ///< … its poleward edge
    std::vector<float>   activity;          ///< 0 … 1
    std::vector<float>   gain;              ///< the assimilation gain
    std::vector<float>   unloading;         ///< 0 … 2.5
    std::vector<float>   stormRed;          ///< 0 … 1, as SkySnapshot::stormRed (Kp 5 → 9, smoothed)
    std::vector<float>   mltHours;          ///< the station's magnetic local time
    std::vector<float>   kp;                ///< Kp as the model sampled it
    double stationMlatDeg = 0.0;            ///< the station's geomagnetic latitude (centred dipole)
    double dayShiftDeg = 0.0;               ///< SkyParams::dayShiftDeg of the timeline
    double arcPosition = 0.5;               ///< SkyParams::arcPosition of the timeline

    uint64_t sequence = 0;               ///< grows with every loaded day (0 = none yet)

    int size() const noexcept { return static_cast<int> (phase.size()); }
};

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

    /** The loaded day's chart; same thread as load(). Empty (sequence 0) before the first day. */
    const DayChart& dayChart() const noexcept { return chart; }
    double dayStartUtc() const noexcept { return loadedStartUtc; }

private:
    void buildChart (const juce::String& dayId, const TrackBundle& bundle);

    SkyRuntime& runtime;
    juce::String loadedDay;
    double loadedStartUtc = 0.0;
    uint64_t sequence = 0;
    DayChart chart;
};

} // namespace worldtable::sky
