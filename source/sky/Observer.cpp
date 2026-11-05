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

    Vec3 unit (const Vec3& a) noexcept
    {
        const double s = 1.0 / std::hypot (a[0], a[1], a[2]);
        return { a[0] * s, a[1] * s, a[2] * s };
    }
} // namespace

Observer::Vec3 Observer::ecef (double latDeg, double lonDeg) noexcept
{
    const double la = latDeg * kD2R, lo = lonDeg * kD2R;
    return { std::cos (la) * std::cos (lo), std::cos (la) * std::sin (lo), std::sin (la) };
}

Observer::Observer (const data::StationInfo& station, double poleLatDeg, double poleLonDeg) noexcept
    : info (station)
{
    zm = ecef (poleLatDeg, poleLonDeg);
    ym = unit (cross ({ 0.0, 0.0, 1.0 }, zm));
    xm = cross (ym, zm);
    stationMag = magOfGeo (station.latDeg, station.lonDeg);
}

Observer::Geomagnetic Observer::toMag (const Vec3& r) const noexcept
{
    return { std::asin (std::fmax (-1.0, std::fmin (1.0, dot (r, zm)))) * kR2D,
             std::atan2 (dot (r, ym), dot (r, xm)) * kR2D };
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

} // namespace worldtable::sky
