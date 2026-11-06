/* The sky geometry the dome needs, as a classic script (the plug-in page has no module loader).
 * GENERATED for T6.1 from prototype/sky/{rng,astro,observer,stars}.js, unchanged apart from the
 * module wrapper: the sketch stays the reference, and a fix there is copied here. */
(function () {
  "use strict";
  const SKY = (window.SKY = window.SKY || {});

  // ---------------------------------------------------------------- rng.js
  (function () {
    // Counter-based, seeded randomness (spec/04-sky-model.md, ADR-0007).
    // Every value is a pure function of (seed, stream, index): no state to drift,
    // identical on every run, and portable 1:1 to C++ with uint32 arithmetic.

    /** lowbias32 integer hash (C. Wellons). Input and output: uint32. */
    function hash32(x) {
      x >>>= 0;
      x ^= x >>> 16;
      x = Math.imul(x, 0x7feb352d);
      x ^= x >>> 15;
      x = Math.imul(x, 0x846ca68b);
      x ^= x >>> 16;
      return x >>> 0;
    }

    /** Uniform [0, 1) from (seed, stream, index). */
    function rand01(seed, stream, index) {
      const h = hash32((seed ^ hash32((stream ^ hash32(index >>> 0)) >>> 0)) >>> 0);
      return h / 4294967296;
    }

    /** Uniform [-1, 1). */
    function randS(seed, stream, index) {
      return rand01(seed, stream, index) * 2 - 1;
    }

    const smooth = (t) => t * t * (3 - 2 * t);

    /** Smooth 1-D value noise in [-1, 1]: knots at integer x, smoothstep between. */
    function noise1(seed, stream, x) {
      const i = Math.floor(x);
      const f = x - i;
      const a = randS(seed, stream, i | 0);
      const b = randS(seed, stream, (i + 1) | 0);
      return a + (b - a) * smooth(f);
    }

    /** Smooth 2-D value noise in [-1, 1] (x: along the curtain, y: time). */
    function noise2(seed, stream, x, y) {
      const ix = Math.floor(x), iy = Math.floor(y);
      const fx = smooth(x - ix), fy = smooth(y - iy);
      const k = (a, b) => randS(seed, stream, (Math.imul(a | 0, 73856093) ^ Math.imul(b | 0, 19349663)) >>> 0);
      const v00 = k(ix, iy), v10 = k(ix + 1, iy), v01 = k(ix, iy + 1), v11 = k(ix + 1, iy + 1);
      const top = v00 + (v10 - v00) * fx;
      const bot = v01 + (v11 - v01) * fx;
      return top + (bot - top) * fy;
    }

    /** Two octaves of noise2, roughly in [-1, 1]. */
    function fbm2(seed, stream, x, y) {
      return 0.67 * noise2(seed, stream, x, y) + 0.33 * noise2(seed, stream + 101, x * 2.13, y * 2.07);
    }
    Object.assign(SKY, { hash32, rand01, randS, noise1, noise2, fbm2 });
  })();

  // ---------------------------------------------------------------- astro.js
  (function () {
    // Lightweight astronomy for the dome (spec/04-sky-model.md §5). No refraction ("airless").
    // Sun: NOAA solar-calculator formulas (~0.01°). Moon: Astronomical Almanac low-precision
    // series (~0.3° in longitude, ~0.2° in latitude) plus topocentric parallax.

    const D2R = Math.PI / 180;
    const R2D = 180 / Math.PI;
    const norm360 = (x) => ((x % 360) + 360) % 360;
    const sind = (x) => Math.sin(x * D2R);
    const cosd = (x) => Math.cos(x * D2R);

    const julianDay = (ms) => ms / 86400000 + 2440587.5;
    const centuries = (ms) => (julianDay(ms) - 2451545.0) / 36525;

    /** Greenwich mean sidereal time (deg). */
    function gmstDeg(ms) {
      const jd = julianDay(ms);
      const t = (jd - 2451545.0) / 36525;
      return norm360(280.46061837 + 360.98564736629 * (jd - 2451545.0) + 0.000387933 * t * t - (t * t * t) / 38710000);
    }

    function obliquityDeg(t, omega) {
      const eps0 = 23 + (26 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60) / 60;
      return eps0 + 0.00256 * cosd(omega);
    }

    /** Sun: apparent ecliptic longitude, RA, Dec (deg). */
    function sunEquatorial(ms) {
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
    function moonEquatorial(ms) {
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
    function altAz(raDeg, decDeg, latDeg, lonDeg, ms) {
      const h = gmstDeg(ms) + lonDeg - raDeg;
      const east = -cosd(decDeg) * sind(h);
      const north = sind(decDeg) * cosd(latDeg) - cosd(decDeg) * sind(latDeg) * cosd(h);
      const up = sind(decDeg) * sind(latDeg) + cosd(decDeg) * cosd(latDeg) * cosd(h);
      return { az: norm360(Math.atan2(east, north) * R2D), el: Math.asin(Math.max(-1, Math.min(1, up))) * R2D };
    }

    /** Sun and moon for a station: positions, illuminated fraction, waxing flag. */
    function skyBodies(ms, latDeg, lonDeg) {
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
    Object.assign(SKY, { julianDay, gmstDeg, sunEquatorial, moonEquatorial, altAz, skyBodies });
  })();

  // ---------------------------------------------------------------- observer.js
  (function () {
    // Observer at a magnetometer station: geomagnetic frame (centred dipole), dome projection,
    // field-line directions, magnetic local time (spec/04-sky-model.md §4, ADR-0006).

    const D2R = Math.PI / 180;
    const R2D = 180 / Math.PI;
    const EARTH_R_KM = 6371.2;

    /** Centred-dipole north pole, IGRF epoch 2025 (approximate). */
    const DIPOLE_POLE = { lat: 80.8, lon: -72.7 };

    const STATIONS = {
      CMO: { code: 'CMO', name: 'College, Alaska', lat: 64.874, lon: -147.86, elevKm: 0.197 },
    };

    const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
    const scale = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
    const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
    const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
    const unit = (a) => scale(a, 1 / Math.hypot(a[0], a[1], a[2]));

    const ecef = (latDeg, lonDeg) => {
      const la = latDeg * D2R, lo = lonDeg * D2R;
      return [Math.cos(la) * Math.cos(lo), Math.cos(la) * Math.sin(lo), Math.sin(la)];
    };

    class Observer {
      constructor(station = STATIONS.CMO, pole = DIPOLE_POLE) {
        this.station = station;
        // Geomagnetic basis: zm = dipole axis, ym ⟂ (geographic axis, dipole axis), xm completes it.
        this.zm = ecef(pole.lat, pole.lon);
        this.ym = unit(cross([0, 0, 1], this.zm));
        this.xm = cross(this.ym, this.zm);
        // Station local basis (east, north, up) and position (km).
        const lo = station.lon * D2R, la = station.lat * D2R;
        this.east = [-Math.sin(lo), Math.cos(lo), 0];
        this.north = [-Math.sin(la) * Math.cos(lo), -Math.sin(la) * Math.sin(lo), Math.cos(la)];
        this.up = ecef(station.lat, station.lon);
        this.pos = scale(this.up, EARTH_R_KM + station.elevKm);
        const m = this.toMag(this.up);
        this.mlat = m.mlat;
        this.mlon = m.mlon;
        this.magZenith = this.direction(this.fieldUp(this.mlat, this.mlon));
      }

      /** ECEF unit vector → geomagnetic latitude/longitude (deg). */
      toMag(r) {
        return {
          mlat: Math.asin(Math.max(-1, Math.min(1, dot(r, this.zm)))) * R2D,
          mlon: Math.atan2(dot(r, this.ym), dot(r, this.xm)) * R2D,
        };
      }

      /** Geomagnetic latitude/longitude (deg) → ECEF unit vector. */
      fromMag(mlatDeg, mlonDeg) {
        const la = mlatDeg * D2R, lo = mlonDeg * D2R;
        return add(add(scale(this.xm, Math.cos(la) * Math.cos(lo)), scale(this.ym, Math.cos(la) * Math.sin(lo))),
          scale(this.zm, Math.sin(la)));
      }

      /** Geographic → geomagnetic (deg). */
      magOfGeo(latDeg, lonDeg) { return this.toMag(ecef(latDeg, lonDeg)); }

      /** Upward unit vector along the dipole field line through (mlat, mlon): inclination tan I = 2 tan λ. */
      fieldUp(mlatDeg, mlonDeg) {
        const la = mlatDeg * D2R, lo = mlonDeg * D2R;
        const r = this.fromMag(mlatDeg, mlonDeg);
        // Tangent toward decreasing magnetic latitude ("magnetic south").
        const south = add(add(scale(this.xm, Math.sin(la) * Math.cos(lo)), scale(this.ym, Math.sin(la) * Math.sin(lo))),
          scale(this.zm, -Math.cos(la)));
        const inc = Math.atan(2 * Math.tan(Math.abs(la)));
        return add(scale(r, Math.sin(inc)), scale(south, Math.cos(inc) * Math.sign(la || 1)));
      }

      /** ECEF point (km) → azimuth (from north through east) and elevation (deg) seen from the station. */
      azEl(pointKm) {
        const d = sub(pointKm, this.pos);
        const e = dot(d, this.east), n = dot(d, this.north), u = dot(d, this.up);
        return { az: (Math.atan2(e, n) * R2D + 360) % 360, el: Math.atan2(u, Math.hypot(e, n)) * R2D };
      }

      /** ECEF direction → az/el. */
      direction(v) { return this.azEl(add(this.pos, scale(v, 1000))); }

      /** Point at altitude hKm above geomagnetic (mlat, mlon) → ECEF km. */
      pointAt(mlatDeg, mlonDeg, hKm) { return scale(this.fromMag(mlatDeg, mlonDeg), EARTH_R_KM + hKm); }

      /** Magnetic local time (h) of a geomagnetic longitude, given the subsolar point. */
      mlt(mlonDeg, subsolarLat, subsolarLon) {
        const s = this.magOfGeo(subsolarLat, subsolarLon);
        return (((12 + (mlonDeg - s.mlon) / 15) % 24) + 24) % 24;
      }
    }

    /**
     * All-sky dome, equidistant fisheye, looking up: zenith at the centre, horizon at radius 1,
     * north at the top and east on the LEFT (as an observer lying on their back, head to the north).
     * Returns x, y in [-1, 1] with y up, and rho (0 zenith … 1 horizon, > 1 below it).
     */
    function domeXY(azDeg, elDeg) {
      const rho = (90 - elDeg) / 90;
      const a = azDeg * D2R;
      return { x: -rho * Math.sin(a), y: rho * Math.cos(a), rho };
    }

    const vec = { dot, cross, scale, add, sub, unit };
    Object.assign(SKY, { EARTH_R_KM, DIPOLE_POLE, STATIONS, ecef, Observer, domeXY, vec });
  })();

  // ---------------------------------------------------------------- stars.js
  (function () {
    // Bright stars for the dome: Yale Bright Star Catalogue, 5th ed. (Hoffleit & Warren 1991; NASA ADC, public domain),
    // via github.com/brettonw/YaleBrightStarCatalog (bsc5-short.json, MIT). V <= 3.0, Dec >= -26 deg (can rise at CMO).
    // Generated 2026-11-05 for T1.2. Columns: RA J2000 (deg), Dec J2000 (deg), V mag, colour temperature K, name.
    const STARS = [
      [101.287,-16.716,-1.46,9750,"Sirius"],
      [213.915,19.183,-0.04,4850,"Arcturus"],
      [279.235,38.784,0.03,10000,"Vega"],
      [79.172,45.998,0.08,5500,"Capella"],
      [78.635,-8.202,0.12,14000,"Rigel"],
      [114.825,5.225,0.38,6750,"Procyon"],
      [88.793,7.407,0.5,3350,"Betelgeuse"],
      [297.696,8.868,0.77,8250,"Altair"],
      [68.98,16.509,0.85,4250,"Aldebaran"],
      [201.298,-11.161,0.98,28000,"Spica"],
      [116.329,28.026,1.14,5000,"Pollux"],
      [310.358,45.28,1.25,9500,"Deneb"],
      [152.093,11.967,1.35,16000,"Regulus"],
      [81.283,6.35,1.64,26000,"Bellatrix"],
      [81.573,28.608,1.65,16000,"Elnath"],
      [84.053,-1.202,1.7,30000,"Alnilam"],
      [193.507,55.96,1.77,10000,"Alioth"],
      [51.081,49.861,1.79,6750,"Mirfak"],
      [165.932,61.751,1.79,5000,"Dubhe"],
      [206.885,49.313,1.86,24000,"Alkaid"],
      [89.882,44.947,1.9,9500,"Menkalinan"],
      [99.428,16.399,1.93,10000,"Alhena"],
      [95.675,-17.956,1.98,28000,"Mirzam"],
      [113.65,31.888,1.98,9750,"Castor"],
      [141.897,-8.659,1.98,4550,"Alphard"],
      [31.793,23.462,2.0,4700,"Hamal"],
      [239.876,25.92,2.0,24000,""],
      [37.953,89.264,2.02,6450,"Polaris"],
      [10.898,-17.987,2.04,5100,"Diphda"],
      [85.19,-1.943,2.05,33000,"Alnitak"],
      [2.097,29.091,2.06,14000,"Alpheratz"],
      [17.433,35.621,2.06,3500,"Mirach"],
      [86.939,-9.67,2.06,30000,"Saiph"],
      [222.676,74.156,2.08,4400,"Kochab"],
      [263.734,12.56,2.08,8750,"Rasalhague"],
      [47.042,40.956,2.12,14000,"Algol"],
      [177.265,14.572,2.14,9250,"Denebola"],
      [305.557,40.257,2.2,6300,"Sadr"],
      [10.127,56.537,2.23,5000,"Schedar"],
      [83.002,-0.299,2.23,33000,"Mintaka"],
      [233.672,26.715,2.23,10000,"Alphecca"],
      [269.152,51.489,2.23,4250,"Eltanin"],
      [30.975,42.33,2.26,4550,"Almach"],
      [2.295,59.15,2.27,7200,"Caph"],
      [200.981,54.925,2.27,9750,"Mizar"],
      [240.083,-22.622,2.32,30000,"Dschubba"],
      [165.46,56.383,2.37,9750,"Merak"],
      [326.047,9.875,2.39,4700,"Enif"],
      [345.944,28.083,2.42,3200,"Scheat"],
      [257.595,-15.725,2.43,9500,"Sabik"],
      [178.457,53.695,2.44,10000,"Phecda"],
      [319.645,62.586,2.44,8250,"Alderamin"],
      [311.553,33.97,2.46,5000,"ε Cyg"],
      [14.177,60.717,2.47,30000,"Navi"],
      [346.19,15.205,2.49,12000,"Markab"],
      [45.57,4.09,2.53,3350,"Menkar"],
      [168.527,20.524,2.56,9000,"Zosma"],
      [249.29,-10.567,2.56,33000,"ζ Oph"],
      [83.182,-17.822,2.58,7500,"Arneb"],
      [183.952,-17.542,2.59,14000,"Gienah"],
      [154.993,19.842,2.61,4850,"Algieba"],
      [229.252,-9.383,2.61,14000,"Zubeneschamali"],
      [89.93,37.213,2.62,10000,"Mahasim"],
      [241.359,-19.806,2.62,28000,"Acrab"],
      [28.66,20.808,2.64,8750,"Sheratan"],
      [188.597,-23.397,2.65,5500,"Kraz"],
      [236.067,6.426,2.65,4700,"Unukalhai"],
      [21.454,60.235,2.68,8750,"Ruchbah"],
      [208.671,18.398,2.68,6000,"Muphrid"],
      [74.248,33.166,2.69,4550,"Kabdhilinan"],
      [221.247,27.074,2.7,5000,"Izar"],
      [296.565,10.613,2.72,4550,"Tarazed"],
      [243.586,-3.694,2.74,3500,"Yed Prior"],
      [245.998,61.514,2.74,5200,"η Dra"],
      [222.72,-16.042,2.75,9250,"Zubenelgenubi"],
      [83.858,-5.91,2.77,33000,"Nair Al Saif"],
      [247.555,21.49,2.77,5300,"Kornephoros"],
      [265.868,4.567,2.77,4700,"Cebalrai"],
      [76.963,-5.086,2.79,9250,"Cursa"],
      [262.608,52.301,2.79,5800,"Rastaban"],
      [121.886,-24.304,2.81,6600,"Tureis"],
      [250.322,31.603,2.81,6000,"ζ Her"],
      [276.993,-25.422,2.81,4850,"Kaus Borealis"],
      [3.309,15.184,2.83,26000,"Algenib"],
      [195.544,10.959,2.83,5200,"Vindemiatrix"],
      [82.061,-20.759,2.84,5500,"Nihal"],
      [58.533,31.884,2.85,28000,"ζ Per"],
      [56.871,24.105,2.87,16000,"Alcyone"],
      [296.244,45.131,2.87,12000,"Al Fawaris"],
      [326.76,-16.127,2.87,8750,"Deneb Algedi"],
      [95.74,22.514,2.88,3050,"Tejat Posterior"],
      [113.65,31.889,2.88,9500,"Castor"],
      [59.463,40.01,2.89,30000,"ε Per"],
      [245.297,-25.593,2.89,28000,"Al Niyat"],
      [287.441,-21.024,2.89,7200,"Albaldah"],
      [111.788,8.289,2.9,14000,"Gomeisa"],
      [194.007,38.318,2.9,10000,"Chara"],
      [322.89,-5.571,2.91,6000,"Sadalsuud"],
      [46.199,53.506,2.93,5200,"γ Per"],
      [340.75,30.221,2.94,5800,"Matar"],
      [59.508,-13.509,2.95,3500,"Zaurak"],
      [187.466,-16.516,2.95,12000,"Algorab"],
      [331.446,-0.32,2.96,5800,"Sadalmelik"],
      [100.983,25.131,2.98,5200,"Mebsuta"],
      [146.463,23.774,2.98,5900,"Ras Elased Australis"],
      [75.492,43.823,2.99,7500,"Haldus"],
      [286.352,13.863,2.99,10000,"Deneb el Okab"],
      [32.386,34.987,3.0,8750,"β Tri"],
      [84.411,21.142,3.0,22000,"Tien Kwan"],
      [182.531,-22.62,3.0,4700,"Minkar"],
      [199.73,-23.172,3.0,5200,"γ Hya"],
    ];
    Object.assign(SKY, { STARS });
  })();
})();
