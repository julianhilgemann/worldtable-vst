#include "AllocationCounter.h"
#include "SkyTestData.h"
#include "sky/Astronomy.h"
#include "sky/Observer.h"
#include "sky/SkyModel.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <memory>

using namespace worldtable;

// Expected values come from the T1.2 sketch (prototype/sky astro.js and observer.js, Node 22),
// printed with 17 digits.

namespace
{
/** Within 1e-9, relative for large values and absolute near zero. */
bool near (double actual, double expected, double tolerance = 1e-9)
{
    return std::fabs (actual - expected) <= tolerance * std::fmax (1.0, std::fabs (expected));
}

sky::Observer cmoObserver()
{
    const sky::SkyParams params;
    return { data::stationInfo (data::StationId::CMO), params.poleLatDeg, params.poleLonDeg };
}
} // namespace

TEST_CASE ("The moon matches the sketch", "[sky][astro]")
{
    const auto cmo = data::stationInfo (data::StationId::CMO);
    struct Expected
    {
        double utc, lambda, beta, parallax, ra, dec, az, el, illumination;
        bool waxing;
    };
    const std::array<Expected, 3> cases { {
        // 2024-05-11T08:15:48Z: a thin waxing crescent low in the north-west
        { 1715415348.0, 92.961627748460160, 5.0699398925526760, 0.95717857554664221, 93.356431220877198,
          28.474049549758721, 301.84658095143311, 15.926491469701823, 0.12887673089606105, true },
        // 2025-01-19T00:00:00Z
        { 1737244800.0, 178.20283435688179, 0.024485812032837173, 0.91042719513839865, 178.36077490335765,
          0.73721983626018683, 330.18632386989088, -22.235688155087015, 0.75716738310361209, false },
        // 2025-01-25T12:00:00Z
        { 1737806400.0, 256.21495346417942, -4.9634143110538087, 0.93036342005494876, 254.45360381721741,
          -27.661106149807335, 96.216675218865930, -28.718747892064762, 0.17685102325223873, false },
    } };

    for (const auto& e : cases)
    {
        INFO ("utc " << e.utc);
        const auto eq = sky::astro::moonEquatorial (e.utc);
        CHECK (near (eq.lambdaDeg, e.lambda));
        CHECK (near (eq.betaDeg, e.beta));
        CHECK (near (eq.parallaxDeg, e.parallax));
        CHECK (near (eq.raDeg, e.ra));
        CHECK (near (eq.decDeg, e.dec));

        const auto m = sky::astro::moon (e.utc, cmo.latDeg, cmo.lonDeg);
        CHECK (near (m.azDeg, e.az));
        CHECK (near (m.elDeg, e.el));
        CHECK (near (m.illumination, e.illumination));
        CHECK (m.waxing == e.waxing);
    }
}

TEST_CASE ("The dome projection matches the sketch", "[sky][observer]")
{
    const auto obs = cmoObserver();

    SECTION ("magnetic zenith: where CMO's rays converge, SSW and 77° up")
    {
        CHECK (near (obs.magneticZenith().azDeg, 202.02324810436266));
        CHECK (near (obs.magneticZenith().elDeg, 77.255434033490715));
    }

    SECTION ("geomagnetic frame and field lines")
    {
        const auto r = obs.fromMag (62, -100);
        CHECK (near (r[0], -0.42337494784504920));
        CHECK (near (r[1], -0.19543490799799890));
        CHECK (near (r[2], 0.88462356416332000));

        const auto north = obs.fieldUp (65, -95);
        CHECK (near (north[0], -0.56558982792922108));
        CHECK (near (north[1], -0.25226361474946146));
        CHECK (near (north[2], 0.78515680931679455));

        const auto south = obs.fieldUp (-40, 10);   // southern hemisphere: the field points down
        CHECK (near (south[0], 0.44140788895992711));
        CHECK (near (south[1], -0.84078896676314141));
        CHECK (near (south[2], -0.31342142385821276));

        const auto ray = obs.direction (obs.fieldUp (61, -110));
        CHECK (near (ray.azDeg, 230.98438774849708));
        CHECK (near (ray.elDeg, 68.312473103602514));
    }

    SECTION ("points in the sky → azimuth and elevation")
    {
        const auto p = obs.pointAt (62, -100, 110);
        CHECK (near (p[0], -2743.9777119733326));
        CHECK (near (p[1], -1266.6527257166304));
        CHECK (near (p[2], 5733.4222440553094));

        const auto low = obs.azEl (p);   // an arc at 62° maglat, 110 km: low in the south-west
        CHECK (near (low.azDeg, 234.20527181437006));
        CHECK (near (low.elDeg, 10.930473290407196));

        const auto northEast = obs.azEl (obs.pointAt (70.5, -80, 220));
        CHECK (near (northEast.azDeg, 64.682668664888126));
        CHECK (near (northEast.elDeg, 10.935812276931868));
    }

    SECTION ("the fisheye: east on the left, below the horizon outside the rim")
    {
        const auto a = sky::domeXY (200, 30);
        CHECK (near (a.x, 0.22801342888377910));
        CHECK (near (a.y, -0.62646174719060554));
        CHECK (near (a.rho, 0.66666666666666663));

        const auto b = sky::domeXY (77.5, -5);
        CHECK (near (b.x, -1.0305346741821519));
        CHECK (near (b.y, 0.22846403693466419));
        CHECK (near (b.rho, 1.0555555555555556));
    }

    SECTION ("nothing allocates")
    {
        const auto before = testing::allocationsOnThisThread();
        double sink = 0.0;
        for (int i = 0; i < 128; ++i)
        {
            const auto h = obs.azEl (obs.pointAt (60.0 + i * 0.05, -120.0 + i * 0.4, 110.0));
            sink += sky::domeXY (h.azDeg, h.elDeg).rho + sky::astro::moon (1715415348.0 + i, 64.9, -147.9).elDeg;
        }
        CHECK (testing::allocationsOnThisThread() == before);
        CHECK (std::isfinite (sink));
    }
}

TEST_CASE ("The snapshot carries the moon and the magnetic zenith", "[sky][model]")
{
    const auto& day = testing::stormDay();
    auto m = std::make_unique<sky::SkyModel>();
    m->setTimeline (&day.timeline);
    m->seek (1715415348.0);   // 2024-05-11T08:15:48Z

    const auto& s = m->current();
    // float copies of the values above
    CHECK (std::fabs (s.astronomy.moonAzDeg - 301.846581f) < 1e-3f);
    CHECK (std::fabs (s.astronomy.moonElDeg - 15.926491f) < 1e-3f);
    CHECK (std::fabs (s.astronomy.moonIllumination - 0.128877f) < 1e-5f);
    CHECK (s.astronomy.moonWaxing);
    CHECK (std::fabs (s.astronomy.sunElDeg - -5.234157f) < 1e-3f);
    CHECK (std::fabs (s.observer.magZenithAzDeg - 202.023248f) < 1e-3f);
    CHECK (std::fabs (s.observer.magZenithElDeg - 77.255434f) < 1e-3f);

    // at 1440× the moon moves with model time, tick by tick
    m->setSpeed (sky::SkyModel::kMaxSpeed);
    const float az0 = s.astronomy.moonAzDeg;
    for (int k = 0; k < 250; ++k)   // one hour of model time
        m->step();
    CHECK (std::fabs (m->current().astronomy.moonAzDeg - az0) > 5.0f);
}
