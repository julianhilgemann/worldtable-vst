#include "Astronomy.h"

#include <cmath>
#include <numbers>

namespace worldtable::sky::astro
{
namespace
{
    constexpr double kD2R = std::numbers::pi / 180.0;
    constexpr double kR2D = 180.0 / std::numbers::pi;

    double norm360 (double x) noexcept { return std::fmod (std::fmod (x, 360.0) + 360.0, 360.0); }
    double sind (double x) noexcept    { return std::sin (x * kD2R); }
    double cosd (double x) noexcept    { return std::cos (x * kD2R); }

    double centuries (double utc) noexcept { return (julianDay (utc) - 2451545.0) / 36525.0; }

    double obliquityDeg (double t, double omega) noexcept
    {
        const double eps0 = 23.0 + (26.0 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60.0) / 60.0;
        return eps0 + 0.00256 * cosd (omega);
    }

    struct SunEquatorial
    {
        double lambda, ra, dec;
    };

    SunEquatorial sunEquatorial (double utc) noexcept
    {
        const double t = centuries (utc);
        const double l0 = norm360 (280.46646 + t * (36000.76983 + 0.0003032 * t));
        const double m = 357.52911 + t * (35999.05029 - 0.0001537 * t);
        const double c = sind (m) * (1.914602 - t * (0.004817 + 0.000014 * t))
                       + sind (2.0 * m) * (0.019993 - 0.000101 * t) + sind (3.0 * m) * 0.000289;
        const double omega = 125.04 - 1934.136 * t;
        const double lambda = l0 + c - 0.00569 - 0.00478 * sind (omega);
        const double eps = obliquityDeg (t, omega);
        const double ra = norm360 (std::atan2 (cosd (eps) * sind (lambda), cosd (lambda)) * kR2D);
        const double dec = std::asin (sind (eps) * sind (lambda)) * kR2D;
        return { norm360 (lambda), ra, dec };
    }
} // namespace

double julianDay (double utc) noexcept { return utc / 86400.0 + 2440587.5; }

double gmstDeg (double utc) noexcept
{
    const double jd = julianDay (utc);
    const double t = (jd - 2451545.0) / 36525.0;
    return norm360 (280.46061837 + 360.98564736629 * (jd - 2451545.0) + 0.000387933 * t * t - (t * t * t) / 38710000.0);
}

Horizontal altAz (double raDeg, double decDeg, double latDeg, double lonDeg, double utc) noexcept
{
    const double h = gmstDeg (utc) + lonDeg - raDeg;
    const double east = -cosd (decDeg) * sind (h);
    const double north = sind (decDeg) * cosd (latDeg) - cosd (decDeg) * sind (latDeg) * cosd (h);
    const double up = sind (decDeg) * sind (latDeg) + cosd (decDeg) * cosd (latDeg) * cosd (h);
    return { norm360 (std::atan2 (east, north) * kR2D), std::asin (std::fmax (-1.0, std::fmin (1.0, up))) * kR2D };
}

Sun sun (double utc, double latDeg, double lonDeg) noexcept
{
    const auto s = sunEquatorial (utc);
    const auto where = altAz (s.ra, s.dec, latDeg, lonDeg, utc);
    return { where.azDeg, where.elDeg, s.dec, std::fmod (s.ra - gmstDeg (utc) + 540.0, 360.0) - 180.0 };
}

MoonEquatorial moonEquatorial (double utc) noexcept
{
    const double t = centuries (utc);
    const double lambda = 218.32 + 481267.881 * t
        + 6.29 * sind (135.0 + 477198.87 * t) - 1.27 * sind (259.3 - 413335.36 * t)
        + 0.66 * sind (235.7 + 890534.22 * t) + 0.21 * sind (269.9 + 954397.74 * t)
        - 0.19 * sind (357.5 + 35999.05 * t) - 0.11 * sind (186.5 + 966404.03 * t);
    const double beta = 5.13 * sind (93.3 + 483202.02 * t) + 0.28 * sind (228.2 + 960400.89 * t)
        - 0.28 * sind (318.3 + 6003.15 * t) - 0.17 * sind (217.6 - 407332.21 * t);
    const double parallax = 0.9508 + 0.0518 * cosd (135.0 + 477198.87 * t) + 0.0095 * cosd (259.3 - 413335.36 * t)
        + 0.0078 * cosd (235.7 + 890534.22 * t) + 0.0028 * cosd (269.9 + 954397.74 * t);
    const double eps = obliquityDeg (t, 125.04 - 1934.136 * t);
    const double l = norm360 (lambda);
    const double x = cosd (beta) * cosd (l);
    const double y = cosd (eps) * cosd (beta) * sind (l) - sind (eps) * sind (beta);
    const double z = sind (eps) * cosd (beta) * sind (l) + cosd (eps) * sind (beta);
    return { l, beta, parallax, norm360 (std::atan2 (y, x) * kR2D), std::asin (z) * kR2D };
}

Moon moon (double utc, double latDeg, double lonDeg) noexcept
{
    const auto s = sunEquatorial (utc);
    const auto m = moonEquatorial (utc);
    auto where = altAz (m.raDeg, m.decDeg, latDeg, lonDeg, utc);
    where.elDeg -= m.parallaxDeg * cosd (where.elDeg);   // topocentric
    const double elongation = std::acos (cosd (m.betaDeg) * cosd (m.lambdaDeg - s.lambda)) * kR2D;
    return { where.azDeg, where.elDeg, (1.0 - cosd (elongation)) / 2.0, sind (m.lambdaDeg - s.lambda) > 0.0 };
}

} // namespace worldtable::sky::astro
