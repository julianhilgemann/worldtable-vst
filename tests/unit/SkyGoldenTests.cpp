#include "data/TrackBuilder.h"
#include "sky/SkyModel.h"
#include "sky/SlowSky.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifndef WORLDTABLE_GOLDEN_DIR
 #error "WORLDTABLE_GOLDEN_DIR must be defined by tests/CMakeLists.txt"
#endif
#ifndef WORLDTABLE_RESOURCES_DIR
 #error "WORLDTABLE_RESOURCES_DIR must be defined by tests/CMakeLists.txt"
#endif

using namespace worldtable;

namespace
{
struct Day
{
    std::unique_ptr<data::WorldSnapshot> track = std::make_unique<data::WorldSnapshot>();
    sky::SkyTimeline timeline;
    juce::var golden;
    double buildMs = 0.0;
};

/** The 10 May 2024 storm, prepared and run through the slow sky once for all tests here. */
const Day& stormDay()
{
    static const Day day = []
    {
        Day d;
        const auto trackFile = juce::File (WORLDTABLE_RESOURCES_DIR).getChildFile ("sky/2024-05-10/track.json");
        data::RawTrack raw;
        const bool parsed = data::parseTrackJson (trackFile.loadFileAsString(), raw);
        const bool prepared = parsed && data::prepareTrack (raw, *d.track);
        jassert (prepared);
        juce::ignoreUnused (prepared);

        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        d.timeline.build (*d.track, 1, sky::SkyParams {});
        d.buildMs = juce::Time::getMillisecondCounterHiRes() - t0;

        d.golden = juce::JSON::parse (juce::File (WORLDTABLE_GOLDEN_DIR).getChildFile ("sky/2024-05-10.json").loadFileAsString());
        return d;
    }();
    return day;
}

const char* kindName (sky::OnsetKind k)
{
    switch (k)
    {
        case sky::OnsetKind::loaded: return "loaded";
        case sky::OnsetKind::forced: return "forced";
        case sky::OnsetKind::pseudo: return "pseudo";
    }
    return "?";
}
} // namespace

TEST_CASE ("SkyTimeline reproduces the slow goldens of 10 May 2024", "[sky][slow][golden]")
{
    const auto& day = stormDay();
    const auto& tl = day.timeline;
    const auto& slow = day.golden["slow"];
    const sky::SkyParams p;
    const int minutes = day.golden["length"];

    REQUIRE (minutes == 2880);
    REQUIRE (tl.minutes.size() == static_cast<size_t> (minutes) + 1);
    std::printf ("SkyTimeline::build, 48 h, 172,800 slow steps: %.0f ms\n", day.buildMs);

    // The goldens record each minute after its 60 slow steps: the timeline's next checkpoint.
    // They are rounded to 1e-4, so 5e-5 is the floor; the rest is the float track (T2.1 notes).
    struct Channel
    {
        const char* name;
        double (*of) (const sky::SlowSkyState&, const sky::SkyParams&);
        double tolerance;
        double maxError = 0.0;
        int worstMinute = 0;
    };
    std::array<Channel, 7> channels { {
        { "energy",      [] (const sky::SlowSkyState& s, const sky::SkyParams& q) { return s.reservoir / q.threshold; }, 1e-4 },
        { "activity",    [] (const sky::SlowSkyState& s, const sky::SkyParams&) { return s.activity; }, 1e-4 },
        { "gain",        [] (const sky::SlowSkyState& s, const sky::SkyParams&) { return s.gain; }, 1e-4 },
        { "unloading",   [] (const sky::SlowSkyState& s, const sky::SkyParams&) { return s.unloading; }, 1e-4 },
        { "coupling",    [] (const sky::SlowSkyState& s, const sky::SkyParams&) { return s.couplingSmoothMvM; }, 1e-4 },
        { "dhModel",     [] (const sky::SlowSkyState& s, const sky::SkyParams&) { return s.dhModelNt; }, 1e-3 },
        { "arcLatitude", [] (const sky::SlowSkyState& s, const sky::SkyParams& q) { return sky::arcLatitudeDeg (s, q); }, 1e-4 },
    } };

    int phaseMismatches = 0;
    for (int m = 0; m < minutes; ++m)
    {
        const auto& s = tl.minutes[static_cast<size_t> (m) + 1];
        phaseMismatches += static_cast<int> (s.phase) != static_cast<int> (slow["phase"][m]) ? 1 : 0;
        for (auto& c : channels)
        {
            const double err = std::fabs (c.of (s, p) - static_cast<double> (slow[c.name][m]));
            if (err > c.maxError)
            {
                c.maxError = err;
                c.worstMinute = m;
            }
        }
    }

    for (const auto& c : channels)
    {
        std::printf ("  %-12s max |C++ − sketch| = %.3g (minute %d), tolerance %.3g\n", c.name, c.maxError, c.worstMinute, c.tolerance);
        INFO (c.name << ": max error " << c.maxError << " at minute " << c.worstMinute);
        CHECK (c.maxError <= c.tolerance);
    }
    std::printf ("  phase differs in %d of %d minutes\n", phaseMismatches, minutes);
    CHECK (phaseMismatches * 1000 <= minutes);   // ≥ 99.9 % agree (0 on 2026-11-05)
}

TEST_CASE ("SkyTimeline fires the goldens' onsets", "[sky][slow][golden]")
{
    const auto& day = stormDay();
    const auto* expected = day.golden["onsets"].getArray();
    REQUIRE (expected != nullptr);

    std::vector<sky::SkyEvent> onsets;
    for (const auto& e : day.timeline.events)
        if (e.type == sky::SkyEvent::Type::onset)
            onsets.push_back (e);

    REQUIRE (onsets.size() == static_cast<size_t> (expected->size()));
    for (size_t i = 0; i < onsets.size(); ++i)
    {
        const auto& want = (*expected)[static_cast<int> (i)];
        const double wantUtc = static_cast<double> (juce::Time::fromISO8601 (want["utc"].toString()).toMilliseconds()) / 1000.0;
        const double wantStrength = want["strength"];
        INFO ("onset " << i << " at " << want["utc"].toString());
        CHECK (std::string (kindName (onsets[i].onsetKind)) == want["kind"].toString().toStdString());
        CHECK (std::fabs (onsets[i].modelTimeUtc - wantUtc) <= 60.0);
        CHECK (std::fabs (onsets[i].strength - wantStrength) <= 0.01 * wantStrength + 1e-4);
        CHECK (onsets[i].node >= 4);
        CHECK (onsets[i].node <= sky::SkyParams {}.nodes - 5);
    }

    // Each onset enters the onset phase in the same slow step, just before it.
    for (size_t i = 1; i < day.timeline.events.size(); ++i)
        if (day.timeline.events[i].type == sky::SkyEvent::Type::onset)
        {
            CHECK (day.timeline.events[i - 1].type == sky::SkyEvent::Type::phase);
            CHECK (day.timeline.events[i - 1].phase == sky::SkyPhase::onset);
            CHECK_THAT (day.timeline.events[i - 1].modelTimeUtc, Catch::Matchers::WithinULP (day.timeline.events[i].modelTimeUtc, 0));
        }
}

TEST_CASE ("SkyTimeline checkpoints sit on model minutes and clamp at both ends", "[sky][slow]")
{
    const auto& day = stormDay();
    const auto& tl = day.timeline;
    const double start = day.track->startUtc;

    REQUIRE (tl.track == day.track.get());
    CHECK (tl.seed == 1u);
    for (size_t k : { size_t { 0 }, size_t { 1 }, size_t { 1935 }, tl.minutes.size() - 1 })
        CHECK_THAT (tl.minutes[k].timeUtc, Catch::Matchers::WithinULP (start + 60.0 * static_cast<double> (k), 0));

    CHECK (tl.checkpointAtOrBefore (start - 3600.0) == &tl.minutes.front());
    CHECK (tl.checkpointAtOrBefore (start) == &tl.minutes[0]);
    CHECK (tl.checkpointAtOrBefore (start + 59.9) == &tl.minutes[0]);
    CHECK (tl.checkpointAtOrBefore (start + 60.0) == &tl.minutes[1]);
    CHECK (tl.checkpointAtOrBefore (start + 1935 * 60.0 + 48.0) == &tl.minutes[1935]);   // 11 May 08:15:48
    CHECK (tl.checkpointAtOrBefore (start + 1e9) == &tl.minutes.back());
    CHECK (tl.checkpointAtOrBefore (std::nan ("")) == &tl.minutes.front());

    sky::SkyTimeline empty;
    CHECK (empty.checkpointAtOrBefore (start) == nullptr);
}

TEST_CASE ("A replay from a checkpoint matches the straight run", "[sky][slow]")
{
    // What seek() will do (T2.3): copy a minute's state and replay up to 59 slow steps.
    const auto& day = stormDay();
    const auto& tl = day.timeline;
    const sky::Observer observer (data::stationInfo (day.track->station), tl.params.poleLatDeg, tl.params.poleLonDeg);
    std::array<sky::SkyEvent, sky::kMaxEventsPerSlowStep> events {};

    for (size_t minute : { size_t { 0 }, size_t { 1934 }, size_t { 2879 } })
    {
        auto state = tl.minutes[minute];
        for (int k = 0; k < 60; ++k)
            sky::slowStep (state, *day.track, tl.params, observer, tl.seed, events);

        const auto& next = tl.minutes[minute + 1];
        CHECK (std::memcmp (&state, &next, sizeof (state)) == 0);
    }
}
