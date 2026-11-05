// Counter-based, seeded randomness (spec/04-sky-model.md, ADR-0007).
// Every value is a pure function of (seed, stream, index): no state to drift,
// identical on every run, and portable 1:1 to C++ with uint32 arithmetic.

/** lowbias32 integer hash (C. Wellons). Input and output: uint32. */
export function hash32(x) {
  x >>>= 0;
  x ^= x >>> 16;
  x = Math.imul(x, 0x7feb352d);
  x ^= x >>> 15;
  x = Math.imul(x, 0x846ca68b);
  x ^= x >>> 16;
  return x >>> 0;
}

/** Uniform [0, 1) from (seed, stream, index). */
export function rand01(seed, stream, index) {
  const h = hash32((seed ^ hash32((stream ^ hash32(index >>> 0)) >>> 0)) >>> 0);
  return h / 4294967296;
}

/** Uniform [-1, 1). */
export function randS(seed, stream, index) {
  return rand01(seed, stream, index) * 2 - 1;
}

const smooth = (t) => t * t * (3 - 2 * t);

/** Smooth 1-D value noise in [-1, 1]: knots at integer x, smoothstep between. */
export function noise1(seed, stream, x) {
  const i = Math.floor(x);
  const f = x - i;
  const a = randS(seed, stream, i | 0);
  const b = randS(seed, stream, (i + 1) | 0);
  return a + (b - a) * smooth(f);
}

/** Smooth 2-D value noise in [-1, 1] (x: along the curtain, y: time). */
export function noise2(seed, stream, x, y) {
  const ix = Math.floor(x), iy = Math.floor(y);
  const fx = smooth(x - ix), fy = smooth(y - iy);
  const k = (a, b) => randS(seed, stream, (Math.imul(a | 0, 73856093) ^ Math.imul(b | 0, 19349663)) >>> 0);
  const v00 = k(ix, iy), v10 = k(ix + 1, iy), v01 = k(ix, iy + 1), v11 = k(ix + 1, iy + 1);
  const top = v00 + (v10 - v00) * fx;
  const bot = v01 + (v11 - v01) * fx;
  return top + (bot - top) * fy;
}

/** Two octaves of noise2, roughly in [-1, 1]. */
export function fbm2(seed, stream, x, y) {
  return 0.67 * noise2(seed, stream, x, y) + 0.33 * noise2(seed, stream + 101, x * 2.13, y * 2.07);
}
