#include "AllocationCounter.h"
#include "PluginProcessor.h"
#include "data/SkyDays.h"
#include "data/TrackBuilder.h"
#include "dsp/SlotExchange.h"
#include "sky/SkyFeed.h"
#include "sky/SkyHandoff.h"
#include "sky/SkyRuntime.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <thread>
#include <tuple>
#include <vector>

using namespace worldtable;

namespace
{
template <typename T>
bool sameBits (const T& a, const T& b) { return std::memcmp (&a, &b, sizeof (T)) == 0; }

/** The fields a reader sees, compared bit for bit (SkySnapshot has padding, so not memcmp). */
bool sameSky (const sky::SkySnapshot& a, const sky::SkySnapshot& b)
{
    return sameBits (a.modelTimeUtc, b.modelTimeUtc) && a.step == b.step && a.phase == b.phase
        && sameBits (a.phaseProgress, b.phaseProgress) && sameBits (a.energy, b.energy)
        && sameBits (a.activity, b.activity) && sameBits (a.unloading, b.unloading) && sameBits (a.gain, b.gain)
        && sameBits (a.kp, b.kp) && sameBits (a.dhModelNt, b.dhModelNt) && sameBits (a.mltHours, b.mltHours)
        && sameBits (a.ovalLatitudeDeg, b.ovalLatitudeDeg) && sameBits (a.recoveryPulse, b.recoveryPulse)
        && a.onsetCount == b.onsetCount && a.whistlerCount == b.whistlerCount
        && sameBits (a.astronomy.moonAzDeg, b.astronomy.moonAzDeg) && a.nodeCount == b.nodeCount
        && sameBits (a.nodes, b.nodes);   // CurtainNode is nine floats: no padding
}

bool sameEvent (const sky::SkyEvent& a, const sky::SkyEvent& b)
{
    return sameBits (a.modelTimeUtc, b.modelTimeUtc) && sameBits (a.strength, b.strength) && a.node == b.node
        && a.type == b.type && a.phase == b.phase && a.falling == b.falling
        && (a.type != sky::SkyEvent::Type::onset || a.onsetKind == b.onsetKind);
}

constexpr double kStormStart = 1715299200.0;   // 2024-05-10T00:00:00Z

/** A runtime playing the storm from 17:30 UT on 10 May (the storm arrives at 17:38) at `speed`. */
std::unique_ptr<sky::SkyRuntime> stormRuntime (double sampleRate, double speed)
{
    auto rt = std::make_unique<sky::SkyRuntime>();
    sky::SkyFeed feed (*rt);
    REQUIRE (feed.load (data::kDefaultSkyDay, 1, sky::SkyParams {}));
    rt->seek (kStormStart + (17 * 60 + 30) * 60.0);
    rt->setSpeed (speed);
    rt->prepare (sampleRate);
    return rt;
}

struct Run
{
    sky::SkySnapshot last {};
    std::vector<sky::SkyEvent> events;
};

/** Feeds `totalSamples` through process() in blocks from `blockSize(i)`, draining the OSC reader. */
template <typename BlockSize>
Run play (sky::SkyRuntime& rt, int totalSamples, BlockSize blockSize)
{
    Run run;
    std::array<sky::SkyEvent, 256> buffer {};
    for (int done = 0, i = 0; done < totalSamples; ++i)
    {
        const int n = std::min (blockSize (i), totalSamples - done);
        rt.process (n);
        done += n;
        const int got = rt.readEvents (sky::SkyRuntime::Reader::osc, buffer);
        run.events.insert (run.events.end(), buffer.begin(), buffer.begin() + got);
    }
    rt.readSnapshot (sky::SkyRuntime::Reader::osc, run.last);
    return run;
}
} // namespace

//==================================================================================================
TEST_CASE ("TripleBuffer: the reader gets the newest value, never a torn one", "[sky][handoff]")
{
    SECTION ("one thread: newest wins, nothing new means false")
    {
        sky::TripleBuffer<int> tb;
        CHECK_FALSE (tb.update());
        tb.write (1);
        tb.write (2);
        CHECK (tb.update());
        CHECK (tb.front() == 2);
        CHECK_FALSE (tb.update());
        CHECK (tb.front() == 2);
        tb.write (3);
        CHECK (tb.update());
        CHECK (tb.front() == 3);
    }

    SECTION ("two threads: 200,000 writes of a 2 KB value")
    {
        struct Big
        {
            std::array<uint64_t, 256> words {};
        };
        auto tb = std::make_unique<sky::TripleBuffer<Big>>();
        constexpr uint64_t kWrites = 200000;
        std::atomic<bool> done { false };

        std::thread writer ([&]
        {
            for (uint64_t v = 1; v <= kWrites; ++v)
            {
                tb->back().words.fill (v);
                tb->publish();
            }
            done = true;
        });

        uint64_t lastSeen = 0, reads = 0, torn = 0, backwards = 0;
        for (;;)
        {
            const bool finished = done.load();   // before update(): every write is published by now
            if (! tb->update())
            {
                if (finished)
                    break;
                continue;
            }
            const auto& w = tb->front().words;
            for (auto x : w)
                torn += x != w[0] ? 1 : 0;
            backwards += w[0] < lastSeen ? 1 : 0;
            lastSeen = w[0];
            ++reads;
        }
        writer.join();

        CHECK (torn == 0);
        CHECK (backwards == 0);
        CHECK (reads > 0);
        CHECK (lastSeen == kWrites);   // the newest value always arrives
    }
}

TEST_CASE ("SkyEventFifo keeps order and counts what it drops", "[sky][handoff]")
{
    SECTION ("one thread")
    {
        sky::SkyEventFifo fifo;
        sky::SkyEvent e {};
        for (int i = 0; i < 300; ++i)
        {
            e.modelTimeUtc = i;
            fifo.push (e);
        }
        CHECK (fifo.droppedCount() == 300u - sky::SkyEventFifo::kCapacity);

        std::array<sky::SkyEvent, 400> out {};
        const int n = fifo.pop (out.data(), static_cast<int> (out.size()));
        REQUIRE (n == static_cast<int> (sky::SkyEventFifo::kCapacity));
        for (int i = 0; i < n; ++i)
            CHECK_THAT (out[static_cast<size_t> (i)].modelTimeUtc, Catch::Matchers::WithinULP (static_cast<double> (i), 0));
    }

    SECTION ("two threads: 500,000 values arrive in order")
    {
        auto q = std::make_unique<sky::SpscQueue<uint64_t, 256>>();
        constexpr uint64_t kCount = 500000;
        std::thread producer ([&]
        {
            for (uint64_t v = 0; v < kCount; ++v)
                while (! q->push (v)) {}
        });

        uint64_t expected = 0, outOfOrder = 0, v = 0;
        while (expected < kCount)
            if (q->pop (v))
                outOfOrder += v != expected++ ? 1 : 0;
        producer.join();
        CHECK (outOfOrder == 0);
    }
}

TEST_CASE ("SlotExchange hands over whole objects between two threads", "[sky][handoff]")
{
    struct Payload
    {
        std::array<uint32_t, 1024> values {};
    };
    auto exchange = std::make_unique<dsp::SlotExchange<Payload, 3>>();
    constexpr uint32_t kPublishes = 20000;
    std::atomic<bool> done { false };

    std::thread producer ([&]
    {
        for (uint32_t v = 1; v <= kPublishes;)
        {
            const int slot = exchange->acquireForWriting();
            if (slot < 0)
                continue;
            exchange->slot (slot).values.fill (v++);
            exchange->publish (slot);
        }
        done = true;
    });

    int current = -1;
    uint32_t taken = 0, torn = 0, backwards = 0, last = 0;
    while (! done.load() || exchange->hasPending())
    {
        const int next = exchange->take();
        if (next < 0)
            continue;
        const auto& v = exchange->slot (next).values;
        for (auto x : v)
            torn += x != v[0] ? 1 : 0;
        backwards += v[0] < last ? 1 : 0;
        last = v[0];
        if (current >= 0)
            exchange->release (current);
        current = next;
        ++taken;
    }
    producer.join();

    CHECK (torn == 0);
    CHECK (backwards == 0);
    CHECK (taken > 0);
    CHECK (last == kPublishes);
}

//==================================================================================================
TEST_CASE ("The sky doesn't depend on the block size", "[sky][runtime][determinism]")
{
    constexpr double sr = 48000.0;
    constexpr int total = 3000 * 480;   // 3,000 ticks at 1440×: 12 h of the storm from 17:30 UT

    auto reference = stormRuntime (sr, sky::SkyModel::kMaxSpeed);
    const auto expected = play (*reference, total, [] (int) { return 4800; });
    REQUIRE (expected.last.step == 3000u);
    REQUIRE (expected.last.onsetCount > 5u);
    REQUIRE (expected.events.size() > 20u);

    std::mt19937 rng (7);
    std::uniform_int_distribution<int> randomSize (1, 2000);
    const std::array<std::pair<const char*, std::function<int (int)>>, 4> variants { {
        { "128", [] (int) { return 128; } },
        { "37", [] (int) { return 37; } },
        { "1 then 4799", [] (int i) { return i % 2 == 0 ? 1 : 4799; } },
        { "random 1–2000", [&] (int) { return randomSize (rng); } },
    } };

    for (const auto& [name, blockSize] : variants)
    {
        INFO ("blocks of " << name);
        auto rt = stormRuntime (sr, sky::SkyModel::kMaxSpeed);
        const auto run = play (*rt, total, blockSize);
        CHECK (sameSky (run.last, expected.last));
        REQUIRE (run.events.size() == expected.events.size());
        bool allSame = true;
        for (size_t i = 0; i < run.events.size(); ++i)
            allSame = allSame && sameEvent (run.events[i], expected.events[i]);
        CHECK (allSame);
        CHECK (rt->droppedEvents (sky::SkyRuntime::Reader::osc) == 0u);
    }
}

TEST_CASE ("The sky only depends on ticks, not on the sample rate", "[sky][runtime][determinism]")
{
    auto at48 = stormRuntime (48000.0, 96.0);
    auto at441 = stormRuntime (44100.0, 96.0);
    const auto a = play (*at48, 2000 * 480, [] (int) { return 512; });
    const auto b = play (*at441, 2000 * 441, [] (int) { return 512; });

    CHECK (a.last.step == 2000u);
    CHECK (sameSky (a.last, b.last));
    REQUIRE (a.events.size() == b.events.size());
    for (size_t i = 0; i < a.events.size(); ++i)
        CHECK (sameEvent (a.events[i], b.events[i]));
}

TEST_CASE ("process() doesn't allocate, even when it picks up a new day", "[sky][runtime]")
{
    auto rt = std::make_unique<sky::SkyRuntime>();
    sky::SkyFeed feed (*rt);
    rt->prepare (48000.0);

    for (const auto& day : data::kSkyDays)
    {
        INFO ("day " << day.id);
        REQUIRE (feed.load (day.id, 1, sky::SkyParams {}));   // allocates: data side
        rt->seek (feed.dayStartUtc());
        rt->setSpeed (24.0);

        const auto before = testing::allocationsOnThisThread();
        for (int k = 0; k < 50; ++k)
            rt->process (512);
        CHECK (testing::allocationsOnThisThread() == before);

        const double t = rt->model().modelTimeUtc();
        CHECK (t >= feed.dayStartUtc());
        CHECK (t < feed.dayStartUtc() + 3600.0);
    }

    // Three days through a pool of three: the old slots came back.
    CHECK (rt->tracks().acquireForWriting() >= 0);
}

TEST_CASE ("The plug-in plays the storm at 24×", "[sky][runtime][processor]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;   // the world feed uses timers, as in PluginProcessorTests
    PluginProcessor p (false);
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    constexpr int blocks = 188;   // 96,256 samples: ticks at samples 0, 480, … 96,000
    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        p.processBlock (buffer, midi);
    }

    sky::SkySnapshot s {};
    REQUIRE (p.getSky().readSnapshot (sky::SkyRuntime::Reader::ui, s));
    CHECK (p.getSkyFeed().dayId() == "2024-05-10");
    CHECK_THAT (s.speed, Catch::Matchers::WithinULP (24.0f, 0));
    CHECK (s.step == 201u);
    // The default Journey starts at the T3.6 storm window (17:20 UT), then 0.24 s per tick.
    CHECK_THAT (s.modelTimeUtc, Catch::Matchers::WithinULP (
                    kStormStart + sky::SkyFeed::kDefaultJourneyStart + 0.24 * 201.0, 0));
    CHECK (s.nodeCount == 64);
    CHECK (s.phase == sky::SkyPhase::growth);   // 17:20 UT is the growth phase (T1.1)
    CHECK (s.onsetCount > 0u);                 // the storm window has already seen onsets
    CHECK (s.kp > 0.0f);
}

//==================================================================================================
TEST_CASE ("SkyFeed charts the loaded day from its timeline (T6.1 magnetogram)", "[sky][feed][ui]")
{
    auto rt = std::make_unique<sky::SkyRuntime>();
    sky::SkyFeed feed (*rt);
    CHECK (feed.dayChart().sequence == 0u);
    REQUIRE (feed.load (data::kDefaultSkyDay, 1, sky::SkyParams {}));

    // The same day built by hand: the chart must be the timeline's state after each minute.
    data::RawTrack raw;
    REQUIRE (data::loadBundledDay (data::kDefaultSkyDay, raw));
    auto track = std::make_unique<data::WorldSnapshot>();
    REQUIRE (data::prepareTrack (raw, *track, 1));
    sky::SkyTimeline timeline;
    timeline.build (*track, 1, sky::SkyParams {});

    const auto& c = feed.dayChart();
    CHECK (c.sequence == 1u);
    CHECK (c.dayId == "2024-05-10");
    CHECK (c.station == data::StationId::CMO);
    CHECK_THAT (c.startUtc, Catch::Matchers::WithinULP (kStormStart, 0));
    REQUIRE (c.size() == track->length);
    for (const auto* v : { &c.dhMeasuredNt, &c.dhModelNt, &c.couplingMvM, &c.reservoir, &c.threshold })
        CHECK (static_cast<int> (v->size()) == c.size());

    const double unit = sky::SkyParams {}.threshold;
    for (const int k : { 0, 600, 1060, 1300, c.size() - 1 })
    {
        const auto& s = timeline.minutes[static_cast<size_t> (k + 1)];
        INFO ("minute " << k);
        CHECK_THAT (c.dhModelNt[static_cast<size_t> (k)], Catch::Matchers::WithinULP (static_cast<float> (s.dhModelNt), 0));
        CHECK_THAT (c.dhMeasuredNt[static_cast<size_t> (k)], Catch::Matchers::WithinULP (static_cast<float> (s.dhNt), 0));
        CHECK_THAT (c.reservoir[static_cast<size_t> (k)], Catch::Matchers::WithinULP (static_cast<float> (s.reservoir / unit), 0));
        CHECK (c.phase[static_cast<size_t> (k)] == static_cast<uint8_t> (s.phase));
    }

    // every phase appears on the storm day, and the chart's onsets are the timeline's
    std::array<int, 4> phases {};
    for (auto p : c.phase)
    {
        REQUIRE (p < 4);
        ++phases[p];
    }
    for (int n : phases)
        CHECK (n > 0);
    size_t onsets = 0;
    for (const auto& e : timeline.events)
        onsets += e.type == sky::SkyEvent::Type::onset ? 1u : 0u;
    CHECK (c.onsets.size() == onsets);
    CHECK (onsets > 10u);

    REQUIRE (feed.load (data::kDefaultSkyDay, 1, sky::SkyParams {}));   // a new day: a new chart
    CHECK (feed.dayChart().sequence == 2u);
}

TEST_CASE ("The telemetry carries the Breath cycle for the UI", "[processor][ui]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    PluginProcessor p (false);
    p.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    float lo = 1.0f, hi = 0.0f;
    for (int b = 0; b < 48000 * 8 / 512; ++b)   // 8 s: two thirds of a 12 s breath
    {
        buffer.clear();
        p.processBlock (buffer, midi);
        const float v = p.getInstrument().getTelemetry().breath.load();
        REQUIRE (v >= 0.0f);
        REQUIRE (v <= 1.0f);
        lo = std::min (lo, v);
        hi = std::max (hi, v);
    }
    CHECK (lo < 0.05f);   // it starts out
    CHECK (hi > 0.8f);    // and breathes in
}

//==================================================================================================
// T6.9: the Journey transport's engine side — a day lands with its track, Loop wraps on the tick grid
TEST_CASE ("A new day lands at its Journey start with the track, as one seek; older seeks are dropped", "[sky][runtime][journey]")
{
    auto rt = std::make_unique<sky::SkyRuntime>();
    sky::SkyFeed feed (*rt);
    rt->prepare (48000.0);
    rt->setSpeed (24.0);
    REQUIRE (feed.loadJourney (data::kDefaultSkyDay));
    // before any block the landing is the time a saved state would carry
    CHECK_THAT (rt->modelTimeNow(), Catch::Matchers::WithinULP (kStormStart + sky::SkyFeed::kDefaultJourneyStart, 0));
    for (int k = 0; k < 20; ++k)
        rt->process (512);
    const auto seeksBefore = rt->model().seeks();

    rt->seek (kStormStart + 20 * 3600.0);   // meant for the storm day, superseded by the new day
    REQUIRE (feed.loadJourney ("2025-01-19"));
    std::printf ("SkyFeed::load of 2025-01-19: %.1f ms on this thread\n", feed.lastLoadMs());
    const double landing = feed.dayStartUtc() + sky::SkyFeed::journeyStart ("2025-01-19");
    CHECK_THAT (rt->modelTimeNow(), Catch::Matchers::WithinULP (landing, 0));

    const auto allocations = testing::allocationsOnThisThread();
    rt->process (512);   // picks the day up: one block, no allocation
    CHECK (testing::allocationsOnThisThread() == allocations);
    // landed (the 512 samples hold one or two ticks at 0.24 s each), not at the stale seek's time
    CHECK (rt->model().modelTimeUtc() >= landing);
    CHECK (rt->model().modelTimeUtc() < landing + 1.0);
    CHECK (rt->model().seeks() > seeksBefore);

    // a seek after the landing applies on the new day
    rt->seek (landing + 3600.0);
    rt->process (512);
    CHECK (rt->model().modelTimeUtc() >= landing + 3600.0);
    CHECK (rt->model().modelTimeUtc() < landing + 3601.0);
}

TEST_CASE ("Loop wraps to the track's start on the tick grid; without it the sky holds at the end", "[sky][runtime][journey][determinism]")
{
    // From 30 s before the end at 1440× (14.4 model seconds per tick): the end comes after three
    // ticks and, with Loop, the next tick lands at the start.
    const auto run = [] (bool loop, int block)
    {
        auto rt = std::make_unique<sky::SkyRuntime>();
        sky::SkyFeed feed (*rt);
        REQUIRE (feed.loadJourney (data::kDefaultSkyDay));
        rt->prepare (48000.0);
        rt->process (0);
        rt->seek (feed.dayEndUtc() - 30.0);
        rt->setSpeed (sky::SkyModel::kMaxSpeed);
        rt->setLoop (loop);
        std::array<sky::SkyEvent, 256> ev {};
        for (int done = 0; done < 40 * 480;)   // 40 ticks
        {
            const int n = std::min (block, 40 * 480 - done);
            rt->process (n);
            done += n;
            rt->readEvents (sky::SkyRuntime::Reader::osc, ev);
        }
        sky::SkySnapshot s {};
        rt->readSnapshot (sky::SkyRuntime::Reader::osc, s);
        return std::make_tuple (s, rt->atTrackEnd(), feed.dayStartUtc(), feed.dayEndUtc(), rt->model().seeks());
    };

    const auto [held, heldAtEnd, start, end, heldSeeks] = run (false, 512);
    CHECK (heldAtEnd);
    CHECK (held.modelTimeUtc >= end - 1.0);

    const auto [looped, loopedAtEnd, start2, end2, loopSeeks] = run (true, 512);
    CHECK_FALSE (loopedAtEnd);
    CHECK (loopSeeks == heldSeeks + 1);                // the wrap is one seek
    CHECK (looped.modelTimeUtc >= start);
    CHECK (looped.modelTimeUtc < start + 40 * 14.4);   // back at the start, moving on

    const auto [odd, oddAtEnd, start3, end3, oddSeeks] = run (true, 37);
    CHECK (sameSky (odd, looped));                     // the wrap doesn't depend on the block size
    CHECK (oddSeeks == loopSeeks);
}
