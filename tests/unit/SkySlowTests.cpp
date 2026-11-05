#include "AllocationCounter.h"

#include "data/WorldSnapshot.h"
#include "sky/Astronomy.h"
#include "sky/Observer.h"
#include "sky/SkyRandom.h"
#include "sky/SlowSky.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <cmath>
#include <memory>

using namespace worldtable;

// Expected values come from the T1.2 sketch (prototype/sky, Node 22), printed with 17 digits.

TEST_CASE ("SkyRandom matches the sketch's rng.js", "[sky][slow]")
{
    using namespace sky::rng;

    SECTION ("integer hash and uniform draws are bit-identical")
    {
        CHECK (hash32 (0) == 0u);
        CHECK (hash32 (1) == 1753845952u);
        CHECK (hash32 (42) == 388445122u);
        CHECK (hash32 (2858833) == 1688198695u);
        CHECK (hash32 (4294967295u) == 1734902346u);
        CHECK_THAT (rand01 (1, 3, 2858833), Catch::Matchers::WithinULP (0.010702152037993073, 0));
        CHECK_THAT (rand01 (7, 15, 28600000), Catch::Matchers::WithinULP (0.046575598418712616, 0));
        CHECK_THAT (randS (1, 7, 5), Catch::Matchers::WithinULP (0.18813587399199605, 0));
    }

    SECTION ("value noise, including negative coordinates")
    {
        // Floating-point contraction (FMA) may differ from V8 in the last bits.
        CHECK (noise1 (1, 81, 3.7) == Catch::Approx (-0.073989315759390495).margin (1e-14));
        CHECK (noise1 (1, 81, -2.25) == Catch::Approx (0.094620778138050810).margin (1e-14));
        CHECK (noise2 (1, 21, 2.5, 7.25) == Catch::Approx (0.0040818725028657354).margin (1e-14));
        CHECK (noise2 (3, 21, -1.5, 0.4) == Catch::Approx (0.61693420101329677).margin (1e-14));
        CHECK (fbm2 (1, 21, 1.3, 4.6) == Catch::Approx (-0.55857909384083659).margin (1e-14));
    }
}

TEST_CASE ("Sun position and magnetic local time match the sketch", "[sky][slow]")
{
    const auto cmo = data::stationInfo (data::StationId::CMO);
    const sky::SkyParams params;
    const sky::Observer observer (cmo, params.poleLatDeg, params.poleLonDeg);

    CHECK (observer.mlatDeg() == Catch::Approx (65.660172719024459).epsilon (1e-12));
    CHECK (observer.mlonDeg() == Catch::Approx (-95.206432456621911).epsilon (1e-12));

    struct Expected
    {
        double utc, gmst, sunAz, sunEl, subLat, subLon, mlt;
    };
    const std::array<Expected, 2> cases { {
        // 2024-05-11T08:15:48Z: the frame of the T2.1 review
        { 1715415348.0, 353.56179937068373, 338.09193832208109, -5.2341570267523627,
          18.041741624163784, 55.142826245554204, 21.000334981388878 },
        // 2025-01-19T00:00:00Z
        { 1737244800.0, 118.64122058264911, 207.49760032090794, 1.8405918285537906,
          -20.326425398038111, -177.33988771905604, 12.394590592518256 },
    } };

    for (const auto& e : cases)
    {
        const auto sun = sky::astro::sun (e.utc, cmo.latDeg, cmo.lonDeg);
        // GMST passes through ≈ 3.2e6° before the wrap: one rounding step there is ≈ 5e-10°,
        // and clang fuses multiply-adds (FMA) where V8 doesn't.
        CHECK (sky::astro::gmstDeg (e.utc) == Catch::Approx (e.gmst).margin (1e-8));
        CHECK (sun.azDeg == Catch::Approx (e.sunAz).epsilon (1e-9));
        CHECK (sun.elDeg == Catch::Approx (e.sunEl).margin (1e-9));
        CHECK (sun.subsolarLatDeg == Catch::Approx (e.subLat).epsilon (1e-9));
        CHECK (sun.subsolarLonDeg == Catch::Approx (e.subLon).epsilon (1e-9));
        CHECK (observer.mlt (observer.mlonDeg(), sun.subsolarLatDeg, sun.subsolarLonDeg) == Catch::Approx (e.mlt).epsilon (1e-9));
    }
}

TEST_CASE ("slowStep doesn't allocate and stays on its 1 s grid", "[sky][slow]")
{
    // A synthetic storm track: strong southward Bz, so the reservoir loads and onsets fire.
    auto track = std::make_unique<data::WorldSnapshot>();
    track->startUtc = 1715299200.0;   // 2024-05-10T00:00:00Z
    track->length = 6 * 60;
    for (int i = 0; i < track->length; ++i)
    {
        const auto k = static_cast<size_t> (i);
        track->value[0][k] = -200.0f * std::sin (static_cast<float> (i) / 40.0f);   // H deviation
        track->value[1][k] = 7.0f;                                                   // Kp
        track->value[2][k] = 700.0f;                                                 // wind
        track->value[3][k] = 20.0f;                                                  // density
        track->value[4][k] = -20.0f;                                                 // Bz
        for (auto& v : track->valid)
            v[k] = 1;
    }

    const sky::SkyParams params;
    const sky::Observer observer (data::stationInfo (track->station), params.poleLatDeg, params.poleLonDeg);
    auto state = sky::initialSlowState (*track, params, track->startUtc);
    std::array<sky::SkyEvent, sky::kMaxEventsPerSlowStep> events {};

    int onsets = 0;
    const auto before = testing::allocationsOnThisThread();
    for (int k = 0; k < 6 * 3600; ++k)
    {
        const int n = sky::slowStep (state, *track, params, observer, 1, events);
        REQUIRE (n >= 0);
        REQUIRE (n <= sky::kMaxEventsPerSlowStep);
        for (int e = 0; e < n; ++e)
            onsets += events[static_cast<size_t> (e)].type == sky::SkyEvent::Type::onset ? 1 : 0;
    }
    CHECK (testing::allocationsOnThisThread() == before);

    CHECK_THAT (state.timeUtc, Catch::Matchers::WithinULP (track->startUtc + 6 * 3600.0, 0));
    CHECK (onsets >= 2);
    CHECK (state.onsetCount == static_cast<uint32_t> (onsets));
}
