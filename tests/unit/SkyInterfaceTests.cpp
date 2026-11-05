// T2.1: the Sky Model's interfaces. Only what the headers define inline is exercised here; the model
// itself is tested in T2.2–T2.5 (see spec/tasks/T2.1-sky-interfaces.md).

#include "data/WorldSnapshot.h"
#include "sky/SkyModel.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

#ifndef WORLDTABLE_GOLDEN_DIR
 #error "WORLDTABLE_GOLDEN_DIR must be defined by tests/CMakeLists.txt"
#endif

using namespace worldtable;
using Catch::Matchers::WithinULP;   // WithinULP (x, 0) = exactly equal, without -Wfloat-equal

TEST_CASE ("Sky hand-off types are plain fixed-size data", "[sky][interface]")
{
    STATIC_REQUIRE (std::is_trivially_copyable_v<sky::SkySnapshot>);
    STATIC_REQUIRE (std::is_trivially_copyable_v<sky::SlowSkyState>);
    STATIC_REQUIRE (std::is_trivially_copyable_v<data::WorldSnapshot>);
    STATIC_REQUIRE (std::is_trivially_copyable_v<sky::SkyEvent>);
    STATIC_REQUIRE (sizeof (sky::SkySnapshot) <= 8192);

    // 1440× runs 14.4 s of model time per tick: 14 or 15 slow steps.
    STATIC_REQUIRE (sky::SkyModel::kMaxSlowStepsPerTick == 15);
    STATIC_REQUIRE (sky::SkyModel::kMaxEventsPerStep >= 2 * 15 + 1);
    STATIC_REQUIRE (sky::SkyParams {}.nodes <= sky::SkySnapshot::kMaxNodes);

    // Sizes quoted in the task file. A change here means a protected interface changed: on purpose?
    INFO ("SkySnapshot " << sizeof (sky::SkySnapshot) << " B, SlowSkyState " << sizeof (sky::SlowSkyState)
                         << " B, WorldSnapshot " << sizeof (data::WorldSnapshot) << " B");
    CHECK (sizeof (sky::SlowSkyState) == 248);
    CHECK (sizeof (sky::SkySnapshot) == 4824);
}

namespace
{
/** Sample positions of every tick over `total` samples, fed in blocks of `block`. */
std::vector<int64_t> tickPositions (double sampleRate, int block, int64_t total)
{
    sky::SkyClock clock;
    clock.prepare (sampleRate);
    std::vector<int64_t> at;

    for (int64_t done = 0; done < total;)
    {
        const int blockLen = static_cast<int> (std::min<int64_t> (block, total - done));
        for (int pos = 0; pos < blockLen;)
        {
            if (clock.tickDue())
            {
                at.push_back (clock.samplePosition());
                clock.ticked();
            }
            const int n = std::min (blockLen - pos, clock.samplesToNextTick());
            clock.advance (n);
            pos += n;
        }
        done += blockLen;
    }
    return at;
}
} // namespace

TEST_CASE ("SkyClock ticks on the same samples whatever the block size", "[sky][interface][clock]")
{
    for (const double sr : { 48000.0, 44100.0, 22050.0 })
    {
        INFO ("sample rate " << sr);
        const auto total = static_cast<int64_t> (sr * 10.0) + 17;   // 10 s and a ragged tail
        const auto reference = tickPositions (sr, 4800, total);

        REQUIRE (reference.size() == 1001);   // ticks at 0 … 10 s inclusive
        for (size_t k = 0; k < reference.size(); ++k)
            CHECK (reference[k] == std::llround (static_cast<double> (k) * sr / 100.0));

        for (const int block : { 128, 37, 1 })
            CHECK (tickPositions (sr, block, total) == reference);
    }

    CHECK (tickPositions (48000.0, 512, 48000)[1] == 480);
    CHECK (tickPositions (44100.0, 512, 44100)[1] == 441);
}

TEST_CASE ("WorldSnapshot samples like the sketch", "[sky][interface][data]")
{
    auto track = std::make_unique<data::WorldSnapshot>();   // ~130 KB: never on the stack
    track->startUtc = 1715299200.0;                         // 2024-05-10 00:00 UTC
    track->length = 4;
    const float h[] = { 0.0f, 10.0f, 20.0f, 40.0f };
    const uint8_t ok[] = { 1, 0, 1, 1 };
    for (size_t i = 0; i < 4; ++i)
    {
        track->value[0][i] = h[i];
        track->valid[0][i] = ok[i];
    }

    using C = data::WorldChannel;
    const double t0 = track->startUtc;
    CHECK_THAT (track->endUtc(), WithinULP (t0 + 240.0, 0));

    CHECK (track->sample (C::hDeviation, t0 - 100.0) == 0.0);     // clamped before the start
    CHECK (track->sample (C::hDeviation, t0) == 0.0);
    CHECK (track->sample (C::hDeviation, t0 + 30.0) == 5.0);
    CHECK (track->sample (C::hDeviation, t0 + 150.0) == 30.0);   // halfway from 20 to 40
    CHECK (track->sample (C::hDeviation, t0 + 180.0) == 40.0);    // last sample
    CHECK (track->sample (C::hDeviation, t0 + 1.0e6) == 40.0);    // clamped after the end

    CHECK (track->validAt (C::hDeviation, t0 - 100.0));           // clamps to sample 0
    CHECK_FALSE (track->validAt (C::hDeviation, t0 + 60.0));
    CHECK_FALSE (track->validAt (C::hDeviation, t0 + 119.9));     // still sample 1
    CHECK (track->validAt (C::hDeviation, t0 + 120.0));
    CHECK (track->validAt (C::hDeviation, t0 + 1.0e6));

    CHECK (track->sample (C::kp, t0) == 0.0);                     // an unset channel reads 0

    const auto empty = std::make_unique<data::WorldSnapshot>();   // no samples: harmless
    CHECK (empty->sample (C::hDeviation, t0) == 0.0);
    CHECK_FALSE (empty->validAt (C::hDeviation, t0));
}

TEST_CASE ("SkyParams defaults equal the sketch constants stored with the goldens", "[sky][interface][golden]")
{
    const auto file = juce::File (WORLDTABLE_GOLDEN_DIR).getChildFile ("sky/2024-05-10.json");
    REQUIRE (file.existsAsFile());
    const auto golden = juce::JSON::parse (file.loadFileAsString());
    const auto* params = golden["params"].getDynamicObject();
    REQUIRE (params != nullptr);

    const sky::SkyParams p;
    const std::map<std::string, double> scalars {
        { "nodes", p.nodes }, { "spanDeg", p.spanDeg },
        { "eTauMin", p.eTauMin }, { "kpTauMin", p.kpTauMin }, { "loadPerHour", p.loadPerHour },
        { "baseLoadPerHour", p.baseLoadPerHour }, { "leakHours", p.leakHours }, { "threshold", p.threshold },
        { "thresholdJitter", p.thresholdJitter }, { "maxRelease", p.maxRelease },
        { "releaseFraction", p.releaseFraction }, { "releaseTauMin", p.releaseTauMin },
        { "refractoryMin", p.refractoryMin }, { "growthE", p.growthE }, { "forcedBayNt", p.forcedBayNt },
        { "forcedDeviationNt", p.forcedDeviationNt }, { "forcedRefractoryMin", p.forcedRefractoryMin },
        { "pseudoPerHour", p.pseudoPerHour }, { "pseudoAfterMin", p.pseudoAfterMin },
        { "pseudoStrength", p.pseudoStrength }, { "gainTauMin", p.gainTauMin },
        { "gainDeadZoneNt", p.gainDeadZoneNt }, { "dhScaleNt", p.dhScaleNt },
        { "activityTauMin", p.activityTauMin }, { "eqBase", p.eqBase }, { "eqPerKp", p.eqPerKp },
        { "poleBase", p.poleBase }, { "polePerKp", p.polePerKp }, { "dayShiftDeg", p.dayShiftDeg },
        { "arcPosition", p.arcPosition }, { "growthDriftDeg", p.growthDriftDeg },
        { "expansionPoleDeg", p.expansionPoleDeg }, { "waveC", p.waveC }, { "damping", p.damping },
        { "stiffness", p.stiffness }, { "forceKm", p.forceKm }, { "surgeKm", p.surgeKm },
        { "displacementLimitKm", p.displacementLimitKm }, { "riseTauS", p.riseTauS },
        { "fallTauS", p.fallTauS }, { "pcPeriodS", p.pcPeriodS }, { "whistlerPerS", p.whistlerPerS },
    };
    const std::map<std::string, sky::Range> ranges {
        { "expansionMin", p.expansionMin }, { "recoveryMin", p.recoveryMin },
        { "gainRange", p.gainRange }, { "pulsePeriodS", p.pulsePeriodS },
    };

    // Every sketch constant has a C++ twin with the same value …
    for (const auto& prop : params->getProperties())
    {
        const auto name = prop.name.toString().toStdString();
        INFO (name);
        if (prop.value.isArray())
        {
            REQUIRE (ranges.count (name) == 1);
            REQUIRE (prop.value.size() == 2);
            CHECK_THAT (static_cast<double> (prop.value[0]), WithinULP (ranges.at (name).lo, 0));
            CHECK_THAT (static_cast<double> (prop.value[1]), WithinULP (ranges.at (name).hi, 0));
        }
        else
        {
            REQUIRE (scalars.count (name) == 1);
            CHECK_THAT (static_cast<double> (prop.value), WithinULP (scalars.at (name), 0));
        }
    }

    // … and the C++ side has no constant the sketch doesn't (the pole is not in the sketch's DEFAULTS).
    CHECK (static_cast<size_t> (params->getProperties().size()) == scalars.size() + ranges.size());
}
