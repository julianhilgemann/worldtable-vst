// Resonator bank (spec/05-sound-engine.md §1–2). Complex one-pole resonators, z ← r·e^{iω}·z + a·x,
// output Im(z). Frequency and decay change smoothly on a ringing resonator (the phasor just turns at
// another rate), and the input is normalised by √(1−r²), so loudness doesn't depend on decay.
//
// Layout: index k = (bank · 3 + layer) · V + voice. Two banks: a chord change retunes the silent one
// and the engine moves the excitation over to it (double-banking). Inactive banks cost nothing.
// Pure; allocates only in the constructor. Ports to dsp/ResonatorBank (use double for the state:
// at T60 = 20 s, 1 − r ≈ 7e-6, close to float's resolution).

import { hash32 } from '../rng.js';

export const T60_LN = Math.log(1000);                                 // −60 dB

export class ResonatorBank {
  constructor(voices, sampleRate, seed = 1) {
    this.V = voices;
    this.sr = sampleRate;
    const R = 2 * 3 * voices;
    this.re = new Float64Array(R); this.im = new Float64Array(R);
    this.c = new Float64Array(R); this.s = new Float64Array(R);       // r·cos ω, r·sin ω
    this.inA = new Float64Array(R);                                   // input amplitude
    this.gL = new Float64Array(R); this.gR = new Float64Array(R);     // output gains, ramped per sample
    this.dgL = new Float64Array(R); this.dgR = new Float64Array(R);
    this.noise = new Int32Array(voices);
    for (let v = 0; v < voices; v++) this.noise[v] = hash32((seed * 0x9e3779b1) ^ hash32(v + 1)) | 1;
    this.xn = new Float64Array(voices);
    this.active = [false, false];
  }

  /** Pole radius for a decay time (T60, s). */
  radius(t60) { return Math.exp(-T60_LN / (t60 * this.sr)); }

  tune(k, hz, r) {
    const w = (2 * Math.PI * hz) / this.sr;
    this.c[k] = r * Math.cos(w);
    this.s[k] = r * Math.sin(w);
  }

  /** Ramp the output gains of resonator k to (l, r) over `samples`. */
  ramp(k, l, r, samples) {
    this.dgL[k] = (l - this.gL[k]) / samples;
    this.dgR[k] = (r - this.gR[k]) / samples;
  }

  /** Silence a bank (state and input), copying output gains from the other bank so nothing jumps. */
  clear(b) {
    const n = 3 * this.V, k0 = b * n, o0 = (1 - b) * n;
    for (let q = 0; q < n; q++) {
      const k = k0 + q;
      this.re[k] = 0; this.im[k] = 0; this.inA[k] = 0;
      this.gL[k] = this.gL[o0 + q]; this.gR[k] = this.gR[o0 + q];
      this.dgL[k] = 0; this.dgR[k] = 0;
    }
  }

  /** NaN/inf guard (spec/05 §8): reset a resonator rather than let it spread. Returns resets. */
  guard() {
    let resets = 0;
    for (let k = 0; k < this.re.length; k++) {
      if (!Number.isFinite(this.re[k]) || !Number.isFinite(this.im[k])) { this.re[k] = 0; this.im[k] = 0; resets++; }
    }
    return resets;
  }

  /** Render n samples into L[off …], R[off …] (overwrites). */
  process(L, R, off, n) {
    const V = this.V, { re, im, c, s, inA, gL, gR, dgL, dgR, noise, xn } = this;
    const scale = 1 / 2147483648;
    for (let j = off; j < off + n; j++) {
      for (let v = 0; v < V; v++) {                                   // xorshift32 white noise per voice
        let x = noise[v];
        x ^= x << 13; x ^= x >>> 17; x ^= x << 5;
        noise[v] = x;
        xn[v] = x * scale;
      }
      let outL = 0, outR = 0;
      for (let b = 0; b < 2; b++) {
        if (!this.active[b]) continue;
        for (let l = 0; l < 3; l++) {
          const k0 = (b * 3 + l) * V;
          for (let v = 0; v < V; v++) {
            const k = k0 + v;
            const zr = re[k], zi = im[k];
            re[k] = c[k] * zr - s[k] * zi + inA[k] * xn[v];
            const y = s[k] * zr + c[k] * zi;
            im[k] = y;
            outL += y * gL[k]; outR += y * gR[k];
            gL[k] += dgL[k]; gR[k] += dgR[k];
          }
        }
      }
      L[j] = outL; R[j] = outR;
    }
  }
}
