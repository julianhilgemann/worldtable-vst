#include "Observer.h"

#include <cmath>
#include <numbers>

namespace worldtable::sky
{
namespace
{
    constexpr double kD2R = std::numbers::pi / 180.0;
    constexpr double kR2D = 180.0 / std::numbers::pi;

    using Vec3 = Observer::Vec3;

    double dot (const Vec3& a, const Vec3& b) noexcept { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

    Vec3 cross (const Vec3& a, const Vec3& b) noexcept
    {
        return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
    }

    Vec3 scale (const Vec3& a, double s) noexcept { return { a[0] * s, a[1] * s, a[2] * s }; }
    Vec3 add (const Vec3& a, const Vec3& b) noexcept { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
    Vec3 sub (const Vec3& a, const Vec3& b) noexcept { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
    Vec3 unit (const Vec3& a) noexcept { return scale (a, 1.0 / std::hypot (a[0], a[1], a[2])); }
} // namespace

Observer::Vec3 Observer::ecef (double latDeg, double lonDeg) noexcept
{
    const double la = latDeg * kD2R, lo = lonDeg * kD2R;
    return { std::cos (la) * std::cos (lo), std::cos (la) * std::sin (lo), std::sin (la) };
}

Observer::Observer (const data::StationInfo& station, double poleLatDeg, double poleLonDeg) noexcept
    : info (station)
{
    // Geomagnetic basis: zm = dipole axis, ym ⟂ (geographic axis, dipole axis), xm completes it.
    zm = ecef (poleLatDeg, poleLonDeg);
    ym = unit (cross ({ 0.0, 0.0, 1.0 }, zm));
    xm = cross (ym, zm);

    // The station's local basis (east, north, up) and position (km).
    const double lo = station.lonDeg * kD2R, la = station.latDeg * kD2R;
    east = { -std::sin (lo), std::cos (lo), 0.0 };
    north = { -std::sin (la) * std::cos (lo), -std::sin (la) * std::sin (lo), std::cos (la) };
    up = ecef (station.latDeg, station.lonDeg);
    position = scale (up, kEarthRadiusKm + station.elevKm);

    stationMag = toMag (up);
    magZenith = direction (fieldUp (stationMag.mlatDeg, stationMag.mlonDeg));
}

Observer::Geomagnetic Observer::toMag (const Vec3& r) const noexcept
{
    return { std::asin (std::fmax (-1.0, std::fmin (1.0, dot (r, zm)))) * kR2D,
             std::atan2 (dot (r, ym), dot (r, xm)) * kR2D };
}

Observer::Vec3 Observer::fromMag (double mlatDeg, double mlonDeg) const noexcept
{
    const double la = mlatDeg * kD2R, lo = mlonDeg * kD2R;
    return add (add (scale (xm, std::cos (la) * std::cos (lo)), scale (ym, std::cos (la) * std::sin (lo))),
                scale (zm, std::sin (la)));
}

Observer::Geomagnetic Observer::magOfGeo (double latDeg, double lonDeg) const noexcept
{
    return toMag (ecef (latDeg, lonDeg));
}

double Observer::mlt (double mlonDeg, double subsolarLatDeg, double subsolarLonDeg) const noexcept
{
    const auto s = magOfGeo (subsolarLatDeg, subsolarLonDeg);
    return std::fmod (std::fmod (12.0 + (mlonDeg - s.mlonDeg) / 15.0, 24.0) + 24.0, 24.0);
}

Observer::Vec3 Observer::fieldUp (double mlatDeg, double mlonDeg) const noexcept
{
    const double la = mlatDeg * kD2R, lo = mlonDeg * kD2R;
    const auto r = fromMag (mlatDeg, mlonDeg);
    // tangent toward decreasing magnetic latitude ("magnetic south")
    const auto south = add (add (scale (xm, std::sin (la) * std::cos (lo)), scale (ym, std::sin (la) * std::sin (lo))),
                            scale (zm, -std::cos (la)));
    const double inclination = std::atan (2.0 * std::tan (std::fabs (la)));
    const double hemisphere = la < 0.0 ? -1.0 : 1.0;   // Math.sign(la || 1)
    return add (scale (r, std::sin (inclination)), scale (south, std::cos (inclination) * hemisphere));
}

Observer::Vec3 Observer::pointAt (double mlatDeg, double mlonDeg, double altitudeKm) const noexcept
{
    return scale (fromMag (mlatDeg, mlonDeg), kEarthRadiusKm + altitudeKm);
}

astro::Horizontal Observer::azEl (const Vec3& pointKm) const noexcept
{
    const auto d = sub (pointKm, position);
    const double e = dot (d, east), n = dot (d, north), u = dot (d, up);
    return { std::fmod (std::atan2 (e, n) * kR2D + 360.0, 360.0), std::atan2 (u, std::hypot (e, n)) * kR2D };
}

astro::Horizontal Observer::direction (const Vec3& unitVector) const noexcept
{
    return azEl (add (position, scale (unitVector, 1000.0)));
}

DomePoint domeXY (double azDeg, double elDeg) noexcept
{
    const double rho = (90.0 - elDeg) / 90.0;
    const double a = azDeg * kD2R;
    return { -rho * std::sin (a), rho * std::cos (a), rho };
}

} // namespace worldtable::sky
