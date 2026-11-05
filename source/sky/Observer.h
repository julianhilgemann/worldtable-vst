#pragma once

#include "Astronomy.h"
#include "data/WorldSnapshot.h"

#include <array>

namespace worldtable::sky
{

/** Mean Earth radius used for points in the sky, km. */
constexpr double kEarthRadiusKm = 6371.2;

/** The observer at a magnetometer station (ADR-0006, spec/04-sky-model.md §4):
    prototype/sky/observer.js.

    A centred-dipole geomagnetic frame, the station's geomagnetic position and magnetic local time,
    and the dome projection: a point in the sky → azimuth and elevation from the station. Field
    lines follow the dipole (tan I = 2 tan λ), so a curtain's rays converge at the magnetic zenith.
    Built off the audio thread; every method is real-time safe. */
class Observer
{
public:
    using Vec3 = std::array<double, 3>;

    Observer (const data::StationInfo& station, double poleLatDeg, double poleLonDeg) noexcept;

    struct Geomagnetic
    {
        double mlatDeg, mlonDeg;
    };

    //== Geomagnetic frame =============================================================================
    /** Earth-centred unit vector → geomagnetic latitude and longitude. */
    Geomagnetic toMag (const Vec3& r) const noexcept;

    /** Geomagnetic latitude and longitude → Earth-centred unit vector. */
    Vec3 fromMag (double mlatDeg, double mlonDeg) const noexcept;

    /** Geographic → geomagnetic. */
    Geomagnetic magOfGeo (double latDeg, double lonDeg) const noexcept;

    /** Magnetic local time (hours, 0 … 24) of a geomagnetic longitude, given the subsolar point. */
    double mlt (double mlonDeg, double subsolarLatDeg, double subsolarLonDeg) const noexcept;

    /** Upward unit vector along the dipole field line through (mlat, mlon). */
    Vec3 fieldUp (double mlatDeg, double mlonDeg) const noexcept;

    //== Dome projection ===============================================================================
    /** The point `altitudeKm` above geomagnetic (mlat, mlon), Earth-centred, km. */
    Vec3 pointAt (double mlatDeg, double mlonDeg, double altitudeKm) const noexcept;

    /** Azimuth (from north through east) and elevation of an Earth-centred point (km) seen from
        the station. */
    astro::Horizontal azEl (const Vec3& pointKm) const noexcept;

    /** Azimuth and elevation of a direction (an Earth-centred unit vector) seen from the station. */
    astro::Horizontal direction (const Vec3& unitVector) const noexcept;

    /** Where field-aligned rays converge seen from the station (CMO: 202°, 77°). */
    astro::Horizontal magneticZenith() const noexcept { return magZenith; }

    //== Station =======================================================================================
    const data::StationInfo& station() const noexcept { return info; }
    double mlatDeg() const noexcept { return stationMag.mlatDeg; }
    double mlonDeg() const noexcept { return stationMag.mlonDeg; }

    /** Earth-centred unit vector of a geographic position. */
    static Vec3 ecef (double latDeg, double lonDeg) noexcept;

private:
    data::StationInfo info;
    Vec3 xm {}, ym {}, zm {};            ///< geomagnetic basis: zm = dipole axis
    Vec3 east {}, north {}, up {};       ///< the station's local frame
    Vec3 position {};                    ///< the station, Earth-centred, km
    Geomagnetic stationMag {};
    astro::Horizontal magZenith {};
};

/** A point on the all-sky dome: equidistant fisheye looking up, zenith at the centre, horizon at
    radius 1, north at the top and east on the LEFT (lying on your back, head to the north).
    x, y in [−1, 1] with y up; rho 0 at the zenith, 1 at the horizon, > 1 below it. */
struct DomePoint
{
    double x, y, rho;
};

DomePoint domeXY (double azDeg, double elDeg) noexcept;

} // namespace worldtable::sky
