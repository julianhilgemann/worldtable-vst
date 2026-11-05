// Observer at a magnetometer station: geomagnetic frame (centred dipole), dome projection,
// field-line directions, magnetic local time (spec/04-sky-model.md §4, ADR-0006).

const D2R = Math.PI / 180;
const R2D = 180 / Math.PI;
export const EARTH_R_KM = 6371.2;

/** Centred-dipole north pole, IGRF epoch 2025 (approximate). */
export const DIPOLE_POLE = { lat: 80.8, lon: -72.7 };

export const STATIONS = {
  CMO: { code: 'CMO', name: 'College, Alaska', lat: 64.874, lon: -147.86, elevKm: 0.197 },
};

const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const scale = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const unit = (a) => scale(a, 1 / Math.hypot(a[0], a[1], a[2]));

export const ecef = (latDeg, lonDeg) => {
  const la = latDeg * D2R, lo = lonDeg * D2R;
  return [Math.cos(la) * Math.cos(lo), Math.cos(la) * Math.sin(lo), Math.sin(la)];
};

export class Observer {
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
export function domeXY(azDeg, elDeg) {
  const rho = (90 - elDeg) / 90;
  const a = azDeg * D2R;
  return { x: -rho * Math.sin(a), y: rho * Math.cos(a), rho };
}

export const vec = { dot, cross, scale, add, sub, unit };
