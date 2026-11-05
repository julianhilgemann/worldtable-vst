#pragma once

#include "data/WorldSnapshot.h"

#include <array>

namespace worldtable::sky
{

/** The observer at a magnetometer station (ADR-0006): prototype/sky/observer.js.

    Centred-dipole geomagnetic frame, the station's geomagnetic position and magnetic local time.
    The dome projection (az/el, field lines, magnetic zenith) follows in T2.5. Built off the audio
    thread; every method is real-time safe. */
class Observer
{
public:
    using Vec3 = std::array<double, 3>;

    Observer (const data::StationInfo& station, double poleLatDeg, double poleLonDeg) noexcept;

    struct Geomagnetic
    {
        double mlatDeg, mlonDeg;
    };

    /** Earth-centred unit vector → geomagnetic latitude and longitude. */
    Geomagnetic toMag (const Vec3& r) const noexcept;

    /** Geographic → geomagnetic. */
    Geomagnetic magOfGeo (double latDeg, double lonDeg) const noexcept;

    /** Magnetic local time (hours, 0 … 24) of a geomagnetic longitude, given the subsolar point. */
    double mlt (double mlonDeg, double subsolarLatDeg, double subsolarLonDeg) const noexcept;

    const data::StationInfo& station() const noexcept { return info; }
    double mlatDeg() const noexcept { return stationMag.mlatDeg; }
    double mlonDeg() const noexcept { return stationMag.mlonDeg; }

    /** Earth-centred unit vector of a geographic position. */
    static Vec3 ecef (double latDeg, double lonDeg) noexcept;

private:
    data::StationInfo info;
    Vec3 xm {}, ym {}, zm {};   ///< geomagnetic basis: zm = dipole axis
    Geomagnetic stationMag {};
};

} // namespace worldtable::sky
