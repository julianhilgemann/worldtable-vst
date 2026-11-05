#pragma once

namespace worldtable::sky::astro
{

/** Lightweight astronomy for the dome (spec/04-sky-model.md §5): prototype/sky/astro.js.
    No refraction ("airless"). Sun: NOAA solar-calculator formulas (≈ 0.01°). The moon follows in
    T2.5. Times are UTC seconds since the Unix epoch. Real-time safe. */

double julianDay (double utc) noexcept;

/** Greenwich mean sidereal time, degrees. */
double gmstDeg (double utc) noexcept;

struct Horizontal
{
    double azDeg;   ///< from north through east
    double elDeg;
};

/** Equatorial → horizontal at an observer. */
Horizontal altAz (double raDeg, double decDeg, double latDeg, double lonDeg, double utc) noexcept;

struct Sun
{
    double azDeg, elDeg;      ///< seen from the observer
    double subsolarLatDeg;    ///< where the sun is overhead
    double subsolarLonDeg;    ///< east positive, −180 … 180
};

/** The sun for an observer (the sun half of the sketch's skyBodies()). */
Sun sun (double utc, double latDeg, double lonDeg) noexcept;

} // namespace worldtable::sky::astro
