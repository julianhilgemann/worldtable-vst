#include "data/TrackBuilder.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <memory>

#ifndef WORLDTABLE_RESOURCES_DIR
 #error "WORLDTABLE_RESOURCES_DIR must be defined by tests/CMakeLists.txt"
#endif

using namespace worldtable::data;

namespace
{
RawTrack loadDay (const char* day)
{
    const auto file = juce::File (WORLDTABLE_RESOURCES_DIR).getChildFile ("sky").getChildFile (day).getChildFile ("track.json");
    REQUIRE (file.existsAsFile());
    RawTrack raw;
    REQUIRE (parseTrackJson (file.loadFileAsString(), raw));
    return raw;
}

float value (const WorldSnapshot& w, WorldChannel c, int i)
{
    return w.value[static_cast<size_t> (c)][static_cast<size_t> (i)];
}

bool valid (const WorldSnapshot& w, WorldChannel c, int i)
{
    return w.valid[static_cast<size_t> (c)][static_cast<size_t> (i)] != 0;
}
} // namespace

// Expected values: prototype/sky/data.js prepareTrack on the same file (Node 22).

TEST_CASE ("parseTrackJson reads a recorded sky day", "[data][track]")
{
    const auto raw = loadDay ("2024-05-10");
    CHECK (raw.length() == 2880);
    CHECK (raw.startUtc == 1715299200.0);   // 2024-05-10T00:00:00Z
    CHECK (raw.stepSeconds == 60.0);
    CHECK_THAT (raw.channels[0][100], Catch::Matchers::WithinULP (12752.65, 0));
    CHECK (std::isnan (raw.channels[2][0]));   // no solar wind at the start of the day

    RawTrack broken;
    CHECK_FALSE (parseTrackJson ("{\"start\": \"2024-05-10T00:00:00Z\"}", broken));
    CHECK_FALSE (parseTrackJson ("not json", broken));
}

TEST_CASE ("prepareTrack fills gaps and finds the quiet level like the sketch", "[data][track]")
{
    const auto raw = loadDay ("2024-05-10");
    CHECK_THAT (quietBaseline (raw.channels[0]), Catch::Matchers::WithinULP (12744.889999999999, 0));

    auto track = std::make_unique<WorldSnapshot>();
    REQUIRE (prepareTrack (raw, *track, 7));
    CHECK (track->length == 2880);
    CHECK (track->sequence == 7);
    CHECK (track->hQuietNt == Catch::Approx (12744.89f));
    CHECK (value (*track, WorldChannel::hDeviation, 100) == Catch::Approx (7.7600000000002183).epsilon (1e-5));

    SECTION ("a leading gap holds climatology")
    {
        CHECK (value (*track, WorldChannel::windSpeed, 0) == 400.0f);
        CHECK_FALSE (valid (*track, WorldChannel::windSpeed, 0));
    }

    SECTION ("a short gap (6 samples) is interpolated")
    {
        CHECK (value (*track, WorldChannel::windSpeed, 490) == Catch::Approx (412.7).epsilon (1e-7));
        CHECK (valid (*track, WorldChannel::windSpeed, 490));
        CHECK (value (*track, WorldChannel::windSpeed, 491) == Catch::Approx (412.85714285714283).epsilon (1e-7));
        CHECK (value (*track, WorldChannel::windSpeed, 494) == Catch::Approx (413.32857142857142).epsilon (1e-7));
        CHECK (value (*track, WorldChannel::windSpeed, 496) == Catch::Approx (413.64285714285717).epsilon (1e-7));
        CHECK_FALSE (valid (*track, WorldChannel::windSpeed, 496));
        CHECK (valid (*track, WorldChannel::windSpeed, 497));
    }

    SECTION ("a long gap (25 samples) decays toward climatology")
    {
        CHECK (value (*track, WorldChannel::windSpeed, 609) == Catch::Approx (422.22645485636860).epsilon (1e-7));
        CHECK (value (*track, WorldChannel::windSpeed, 621) == Catch::Approx (418.19748212280842).epsilon (1e-7));
        CHECK (value (*track, WorldChannel::windSpeed, 633) == Catch::Approx (414.89883824253002).epsilon (1e-7));
        CHECK (value (*track, WorldChannel::windSpeed, 634) == Catch::Approx (418.6).epsilon (1e-7));
    }
}

TEST_CASE ("prepareTrack rejects tracks that don't fit", "[data][track]")
{
    auto track = std::make_unique<WorldSnapshot>();
    RawTrack raw;
    for (auto& c : raw.channels)
        c.assign (1, 1.0);
    CHECK_FALSE (prepareTrack (raw, *track));

    for (auto& c : raw.channels)
        c.assign (WorldSnapshot::kCapacity + 1, 1.0);
    CHECK_FALSE (prepareTrack (raw, *track));
}
