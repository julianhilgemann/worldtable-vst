// Lightweight astronomy for the dome (spec/04-sky-model.md §5). No refraction ("airless").
// Sun: NOAA solar-calculator formulas (~0.01°). Moon: Astronomical Almanac low-precision
// series (~0.3° in longitude, ~0.2° in latitude) plus topocentric parallax.

const D2R = Math.PI / 180;
const R2D = 180 / Math.PI;
const norm360 = (x) => ((x % 360) + 360) % 360;
const sind = (x) => Math.sin(x * D2R);
const cosd = (x) => Math.cos(x * D2R);

export const julianDay = (ms) => ms / 86400000 + 2440587.5;
const centuries = (ms) => (julianDay(ms) - 2451545.0) / 36525;

/** Greenwich mean sidereal time (deg). */
export function gmstDeg(ms) {
  const jd = julianDay(ms);
  const t = (jd - 2451545.0) / 36525;
  return norm360(280.46061837 + 360.98564736629 * (jd - 2451545.0) + 0.000387933 * t * t - (t * t * t) / 38710000);
}

function obliquityDeg(t, omega) {
  const eps0 = 23 + (26 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60) / 60;
  return eps0 + 0.00256 * cosd(omega);
}

/** Sun: apparent ecliptic longitude, RA, Dec (deg). */
export function sunEquatorial(ms) {
  const t = centuries(ms);
  const l0 = norm360(280.46646 + t * (36000.76983 + 0.0003032 * t));
  const m = 357.52911 + t * (35999.05029 - 0.0001537 * t);
  const c = sind(m) * (1.914602 - t * (0.004817 + 0.000014 * t)) + sind(2 * m) * (0.019993 - 0.000101 * t) + sind(3 * m) * 0.000289;
  const omega = 125.04 - 1934.136 * t;
  const lambda = l0 + c - 0.00569 - 0.00478 * sind(omega);
  const eps = obliquityDeg(t, omega);
  const ra = norm360(Math.atan2(cosd(eps) * sind(lambda), cosd(lambda)) * R2D);
  const dec = Math.asin(sind(eps) * sind(lambda)) * R2D;
  return { lambda: norm360(lambda), ra, dec, eps };
}

/** Moon: geocentric ecliptic longitude/latitude, horizontal parallax, RA, Dec (deg). */
export function moonEquatorial(ms) {
  const t = centuries(ms);
  const lambda = 218.32 + 481267.881 * t
    + 6.29 * sind(135.0 + 477198.87 * t) - 1.27 * sind(259.3 - 413335.36 * t)
    + 0.66 * sind(235.7 + 890534.22 * t) + 0.21 * sind(269.9 + 954397.74 * t)
    - 0.19 * sind(357.5 + 35999.05 * t) - 0.11 * sind(186.5 + 966404.03 * t);
  const beta = 5.13 * sind(93.3 + 483202.02 * t) + 0.28 * sind(228.2 + 960400.89 * t)
    - 0.28 * sind(318.3 + 6003.15 * t) - 0.17 * sind(217.6 - 407332.21 * t);
  const parallax = 0.9508 + 0.0518 * cosd(135.0 + 477198.87 * t) + 0.0095 * cosd(259.3 - 413335.36 * t)
    + 0.0078 * cosd(235.7 + 890534.22 * t) + 0.0028 * cosd(269.9 + 954397.74 * t);
  const eps = obliquityDeg(t, 125.04 - 1934.136 * t);
  const l = norm360(lambda);
  const x = cosd(beta) * cosd(l);
  const y = cosd(eps) * cosd(beta) * sind(l) - sind(eps) * sind(beta);
  const z = sind(eps) * cosd(beta) * sind(l) + cosd(eps) * sind(beta);
  return { lambda: l, beta, parallax, ra: norm360(Math.atan2(y, x) * R2D), dec: Math.asin(z) * R2D };
}

/** Equatorial (deg) → horizontal at an observer: azimuth from north through east, elevation (deg). */
export function altAz(raDeg, decDeg, latDeg, lonDeg, ms) {
  const h = gmstDeg(ms) + lonDeg - raDeg;
  const east = -cosd(decDeg) * sind(h);
  const north = sind(decDeg) * cosd(latDeg) - cosd(decDeg) * sind(latDeg) * cosd(h);
  const up = sind(decDeg) * sind(latDeg) + cosd(decDeg) * cosd(latDeg) * cosd(h);
  return { az: norm360(Math.atan2(east, north) * R2D), el: Math.asin(Math.max(-1, Math.min(1, up))) * R2D };
}

/** Sun and moon for a station: positions, illuminated fraction, waxing flag. */
export function skyBodies(ms, latDeg, lonDeg) {
  const sun = sunEquatorial(ms);
  const moon = moonEquatorial(ms);
  const s = altAz(sun.ra, sun.dec, latDeg, lonDeg, ms);
  const m = altAz(moon.ra, moon.dec, latDeg, lonDeg, ms);
  m.el -= moon.parallax * cosd(m.el);                       // topocentric
  const elong = Math.acos(cosd(moon.beta) * cosd(moon.lambda - sun.lambda)) * R2D;
  return {
    sunAz: s.az, sunEl: s.el,
    moonAz: m.az, moonEl: m.el,
    moonIllum: (1 - cosd(elong)) / 2,
    moonWaxing: sind(moon.lambda - sun.lambda) > 0,
    subsolarLat: sun.dec,
    subsolarLon: ((sun.ra - gmstDeg(ms) + 540) % 360) - 180,
  };
}
