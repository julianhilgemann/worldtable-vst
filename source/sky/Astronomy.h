#pragma once

namespace worldtable::sky::astro
{

/** Lightweight astronomy for the dome (spec/04-sky-model.md §5): prototype/sky/astro.js.
    No refraction ("airless"). Sun: NOAA solar-calculator formulas (≈ 0.01°). Moon: Astronomical
    Almanac low-precision series (≈ 0.3° in longitude, ≈ 0.2° in latitude) plus topocentric parallax.
    Times are UTC seconds since the Unix epoch. Real-time safe. */

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

struct MoonEquatorial
{
    double lambdaDeg, betaDeg;   ///< geocentric ecliptic longitude and latitude
    double parallaxDeg;          ///< horizontal parallax
    double raDeg, decDeg;
};

/** The moon's geocentric position (the sketch's moonEquatorial()). */
MoonEquatorial moonEquatorial (double utc) noexcept;

struct Moon
{
    double azDeg, elDeg;     ///< seen from the observer, topocentric
    double illumination;     ///< 0 new … 1 full
    bool   waxing;
};

/** The moon for an observer (the moon half of the sketch's skyBodies()). */
Moon moon (double utc, double latDeg, double lonDeg) noexcept;

} // namespace worldtable::sky::astro
