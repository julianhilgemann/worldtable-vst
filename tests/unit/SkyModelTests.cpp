#include "AllocationCounter.h"
#include "SkyTestData.h"
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
#include <vector>

using namespace worldtable;
using testing::stormDay;

namespace
{
/** Bit-identical slow states. memcmp is safe here: every state is a copy of one initialSlowState(),
    so even the padding byte matches. */
bool sameState (const sky::SlowSkyState& a, const sky::SlowSkyState& b)
{
    return std::memcmp (&a, &b, sizeof (a)) == 0;
}

template <typename T>
bool sameBits (const T& a, const T& b)
{
    return std::memcmp (&a, &b, sizeof (T)) == 0;
}

/** Field by field: SkyEvent has padding, which memcmp would compare too. */
bool sameEvent (const sky::SkyEvent& a, const sky::SkyEvent& b)
{
    return sameBits (a.modelTimeUtc, b.modelTimeUtc) && sameBits (a.strength, b.strength)
        && sameBits (a.dhdtNt, b.dhdtNt) && a.node == b.node && a.type == b.type && a.phase == b.phase
        && (a.type != sky::SkyEvent::Type::onset || a.onsetKind == b.onsetKind) && a.falling == b.falling;
}

/** A model on the storm day at the track start, as the sketch's `new SkyModel(track, {seed: 1})`. */
std::unique_ptr<sky::SkyModel> stormModel (double speed)
{
    auto m = std::make_unique<sky::SkyModel>();
    m->setTimeline (&stormDay().timeline);
    m->seek (stormDay().track->startUtc);
    m->setSpeed (speed);
    return m;
}

/** The timeline's state at `minute`, stepped on to `target` (what a seek must reproduce). */
sky::SlowSkyState replay (const sky::SkyTimeline& tl, size_t minute, double target)
{
    const sky::Observer observer (data::stationInfo (tl.track->station), tl.params.poleLatDeg, tl.params.poleLonDeg);
    std::array<sky::SkyEvent, sky::kMaxEventsPerSlowStep> events {};
    auto s = tl.minutes[minute];
    while (s.timeUtc <= target)
        sky::slowStep (s, *tl.track, tl.params, observer, tl.seed, events);
    return s;
}
} // namespace

TEST_CASE ("A new SkyModel is a quiet sky that breathes without a timeline", "[sky][model]")
{
    auto m = std::make_unique<sky::SkyModel>();
    const auto& s0 = m->current();
    CHECK (s0.nodeCount == 64);
    CHECK (s0.phase == sky::SkyPhase::quiet);
    CHECK (s0.onsetCount == 0u);
    CHECK_THAT (m->speed(), Catch::Matchers::WithinULP (0.0, 0));
    CHECK_THAT (s0.observer.mlatDeg, Catch::Matchers::WithinAbs (65.66, 0.01));

    m->setSpeed (24.0);   // no timeline: model time can't move
    const auto before = testing::allocationsOnThisThread();
    for (int k = 0; k < 500; ++k)
        m->step();
    CHECK (testing::allocationsOnThisThread() == before);

    const auto& s = m->current();
    CHECK_THAT (m->modelTimeUtc(), Catch::Matchers::WithinULP (0.0, 0));
    CHECK (s.step == 500u);
    CHECK (s.nodes[10].brightness > 0.0f);
    CHECK (std::fabs (s.nodes[10].displacementKm) > 0.0f);
}

TEST_CASE ("A 24× Journey matches the goldens' node traces", "[sky][model][golden]")
{
    const auto& day = stormDay();
    const auto& nodes = day.golden["nodes"];
    const auto* index = nodes["index"].getArray();
    REQUIRE (index != nullptr);
    REQUIRE (static_cast<double> (day.golden["speed"]) > 23.0);

    auto m = stormModel (24.0);
    const int minutes = day.golden["length"];
    const int ticksPerMinute = 250;   // 60 s / (0.01 s × 24)

    double maxB = 0.0, maxY = 0.0, sumB = 0.0;
    int worstB = 0, worstY = 0, samples = 0;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int minute = 0; minute < minutes; ++minute)
    {
        for (int k = 0; k < ticksPerMinute; ++k)
            m->step();

        const auto& snap = m->current();
        for (int j = 0; j < index->size(); ++j)
        {
            const auto& node = snap.nodes[static_cast<size_t> (static_cast<int> ((*index)[j]))];
            const double eb = std::fabs (node.brightness - static_cast<double> (nodes["brightness"][j][minute]));
            const double ey = std::fabs (node.displacementKm - static_cast<double> (nodes["displacementKm"][j][minute]));
            sumB += eb;
            ++samples;
            if (eb > maxB) { maxB = eb; worstB = minute; }
            if (ey > maxY) { maxY = ey; worstY = minute; }
        }
    }
    std::printf ("24× Journey, 48 h (720,000 ticks): %.0f ms\n", juce::Time::getMillisecondCounterHiRes() - t0);
    std::printf ("  brightness   max |C++ − sketch| = %.3g (minute %d), mean %.3g\n", maxB, worstB, sumB / samples);
    std::printf ("  displacement max |C++ − sketch| = %.3g km (minute %d)\n", maxY, worstY);

    // The goldens are rounded to 1e-4: 5e-5 is the floor.
    CHECK (maxB <= 1e-4);
    CHECK (maxY <= 2e-4);
}

TEST_CASE ("The slow sky inside SkyModel equals the timeline at any speed", "[sky][model]")
{
    const auto& day = stormDay();
    const auto& tl = day.timeline;
    const double start = day.track->startUtc;

    for (const double speed : { 6.0, 96.0 })
    {
        auto m = stormModel (speed);
        // 1 h at 6× (1,000 ticks a model minute), 12 h at 96× (62.5 ticks a model minute)
        const int ticks = speed < 10.0 ? 60 * 1000 : 720 * 625 / 10;
        const int minutesExpected = speed < 10.0 ? 60 : 720;
        int compared = 0, differing = 0;
        for (int k = 0; k < ticks; ++k)
        {
            m->step();
            const double t = m->slowState().timeUtc;
            if (std::fmod (t - start, 60.0) == 0.0)
            {
                ++compared;
                differing += sameState (m->slowState(), tl.minutes[static_cast<size_t> ((t - start) / 60.0)]) ? 0 : 1;
            }
        }
        INFO ("speed " << speed);
        CHECK (compared >= minutesExpected - 1);
        CHECK (differing == 0);
    }
}

TEST_CASE ("seek lands on the straight run in at most 59 slow steps", "[sky][model]")
{
    const auto& day = stormDay();
    const auto& tl = day.timeline;
    const double start = day.track->startUtc, end = day.track->endUtc();
    auto m = stormModel (0.0);

    SECTION ("a time inside a minute")
    {
        const double target = start + 1935 * 60.0 + 48.0;   // 11 May 08:15:48
        m->seek (target + 0.7);
        CHECK_THAT (m->modelTimeUtc(), Catch::Matchers::WithinULP (target, 0));
        CHECK (m->lastSeekSlowSteps() == 49);
        CHECK (sameState (m->slowState(), replay (tl, 1935, target)));
        CHECK (m->current().onsetCount == 15u);
    }

    SECTION ("the last second of a minute starts from the next checkpoint")
    {
        m->seek (start + 100 * 60.0 + 59.0);
        CHECK (m->lastSeekSlowSteps() == 0);
        CHECK (sameState (m->slowState(), tl.minutes[101]));
    }

    SECTION ("never more than 59 steps")
    {
        int most = 0;
        for (double t = start; t < start + 3 * 3600.0; t += 7.0)
        {
            m->seek (t);
            most = std::max (most, m->lastSeekSlowSteps());
        }
        CHECK (most == sky::SkyModel::kMaxSeekSlowSteps);
    }

    SECTION ("a round trip A → B → A comes back to the same sky")
    {
        m->seek (start + 20 * 3600.0 + 13.0);
        const auto a = m->slowState();
        const auto snapA = m->current();
        m->seek (start + 40 * 3600.0 + 31.0);
        m->seek (start + 20 * 3600.0 + 13.0);
        CHECK (sameState (m->slowState(), a));
        CHECK (std::memcmp (&m->current().energy, &snapA.energy, sizeof (float)) == 0);
    }

    SECTION ("it clamps to the track")
    {
        m->seek (start - 1e6);
        CHECK_THAT (m->modelTimeUtc(), Catch::Matchers::WithinULP (start, 0));
        m->seek (end + 1e6);
        CHECK_THAT (m->modelTimeUtc(), Catch::Matchers::WithinULP (end - 1.0, 0));
        m->seek (std::nan (""));
        CHECK_THAT (m->modelTimeUtc(), Catch::Matchers::WithinULP (start, 0));
    }

    SECTION ("it doesn't allocate")
    {
        const auto before = testing::allocationsOnThisThread();
        for (int k = 0; k < 200; ++k)
            m->seek (start + k * 611.0);
        CHECK (testing::allocationsOnThisThread() == before);
    }
}

TEST_CASE ("At 1440× the events are the timeline's and fit the buffer", "[sky][model]")
{
    const auto& day = stormDay();
    const double start = day.track->startUtc, end = day.track->endUtc();
    auto m = stormModel (sky::SkyModel::kMaxSpeed);

    std::vector<sky::SkyEvent> slowEvents;
    slowEvents.reserve (4096);
    size_t most = 0;
    int whistlers = 0;
    const auto before = testing::allocationsOnThisThread();
    while (m->modelTimeUtc() < end - 1.0)
    {
        const auto events = m->step();
        most = std::max (most, events.size());
        for (const auto& e : events)
        {
            if (e.type == sky::SkyEvent::Type::whistler)
                ++whistlers;
            else
                slowEvents.push_back (e);
        }
    }
    CHECK (testing::allocationsOnThisThread() == before);   // reserve() above covers the vector

    CHECK (most <= static_cast<size_t> (sky::SkyModel::kMaxEventsPerStep));
    CHECK (whistlers > 0);
    CHECK (m->current().whistlerCount == static_cast<uint32_t> (whistlers));

    std::vector<sky::SkyEvent> expected;
    for (const auto& e : day.timeline.events)
        if (e.modelTimeUtc > start)   // seek(start) ran the first slow step without events
            expected.push_back (e);

    REQUIRE (slowEvents.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
        CHECK (sameEvent (slowEvents[i], expected[i]));
}

TEST_CASE ("Freeze holds the sky's story while the curtain keeps breathing", "[sky][model]")
{
    const auto& day = stormDay();
    auto m = stormModel (24.0);
    m->seek (day.track->startUtc + 1935 * 60.0);
    for (int k = 0; k < 200; ++k)
        m->step();

    m->setSpeed (0.0);
    const double held = m->modelTimeUtc();
    const auto slow = m->slowState();
    const float y = m->current().nodes[30].displacementKm;
    const float b = m->current().nodes[30].brightness;

    for (int k = 0; k < 300; ++k)
        CHECK (m->step().empty());

    CHECK_THAT (m->modelTimeUtc(), Catch::Matchers::WithinULP (held, 0));
    CHECK (sameState (m->slowState(), slow));
    CHECK (std::memcmp (&y, &m->current().nodes[30].displacementKm, sizeof (float)) != 0);
    CHECK (std::memcmp (&b, &m->current().nodes[30].brightness, sizeof (float)) != 0);
}

TEST_CASE ("At the end of the track model time holds", "[sky][model]")
{
    const auto& day = stormDay();
    const double end = day.track->endUtc();
    auto m = stormModel (24.0);
    m->seek (end - 30.0);
    for (int k = 0; k < 1000; ++k)
        m->step();

    const double held = m->modelTimeUtc();
    CHECK (held >= end - 1.0);
    CHECK (held < end - 1.0 + 0.24 + 1e-6);
    CHECK_THAT (m->slowState().timeUtc, Catch::Matchers::WithinULP (end, 0));
    m->step();
    CHECK_THAT (m->modelTimeUtc(), Catch::Matchers::WithinULP (held, 0));
}

TEST_CASE ("setTimeline re-seeks on a new seed and keeps the curtain", "[sky][model]")
{
    const auto& day = stormDay();
    const double t = day.track->startUtc + 30 * 3600.0 + 17.0;

    auto seed2 = std::make_unique<sky::SkyTimeline>();
    seed2->build (*day.track, 2, sky::SkyParams {});

    auto m = stormModel (24.0);
    m->seek (t);
    for (int k = 0; k < 100; ++k)
        m->step();
    const double now = m->modelTimeUtc();
    const float y = m->current().nodes[20].displacementKm;

    const auto before = testing::allocationsOnThisThread();
    m->setTimeline (seed2.get());
    CHECK (testing::allocationsOnThisThread() == before);

    auto reference = std::make_unique<sky::SkyModel>();
    reference->setTimeline (seed2.get());
    reference->seek (now);
    CHECK (sameState (m->slowState(), reference->slowState()));
    CHECK (std::memcmp (&y, &m->current().nodes[20].displacementKm, sizeof (float)) == 0);   // nothing jumps

    SECTION ("detached, model time holds and the curtain moves on")
    {
        m->setTimeline (nullptr);
        const double held = m->modelTimeUtc();
        for (int k = 0; k < 100; ++k)
            CHECK (m->step().size() <= 1u);   // whistlers only
        CHECK_THAT (m->modelTimeUtc(), Catch::Matchers::WithinULP (held, 0));
    }

    SECTION ("the same seed and params on a new track keep the slow state")
    {
        auto copy = std::make_unique<data::WorldSnapshot> (*day.track);
        copy->sequence += 1;
        auto sameSeed = std::make_unique<sky::SkyTimeline>();
        sameSeed->build (*copy, 2, sky::SkyParams {});

        const auto slow = m->slowState();
        m->setTimeline (sameSeed.get());
        CHECK (sameState (m->slowState(), slow));
    }
}
