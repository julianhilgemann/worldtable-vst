// Stand-in effects so the sketch isn't dry: FDN reverb, slow auto-gain, DC blocker, lookahead
// limiter (spec/05-sound-engine.md §7–8). The spec's real chain (phaser, ensemble, tape delay,
// shimmer) is T3.5. Pure, allocation only in constructors, stereo in place.

const dbToGain = (db) => 10 ** (db / 20);

/** 8-line feedback delay network, Hadamard mixing, damped loop, pre-delay. */
export class Reverb {
  constructor(sampleRate, { t60 = 9, mix = 0.35 } = {}) {
    this.sr = sampleRate;
    const base = [2203, 2647, 3011, 3389, 3797, 4211, 4603, 5087];   // primes, 46–106 ms at 48 kHz
    this.len = base.map((n) => Math.round((n * sampleRate) / 48000));
    this.buf = this.len.map((n) => new Float64Array(n));
    this.pos = new Int32Array(8);
    this.lp = new Float64Array(8);
    this.x = new Float64Array(8);
    this.g = new Float64Array(8);
    this.pre = new Float64Array(Math.round(0.03 * sampleRate));
    this.prePos = 0;
    this.damp = 1 - Math.exp((-2 * Math.PI * 5000) / sampleRate);  // one-pole low-pass in the loop
    this.set({ t60, mix });
  }

  set({ t60 = this.t60, mix = this.mix }) {
    this.t60 = t60; this.mix = mix;
    let g2 = 0;
    for (let i = 0; i < 8; i++) { this.g[i] = 10 ** ((-3 * this.len[i]) / (t60 * this.sr)); g2 += this.g[i] ** 2; }
    this.inGain = Math.sqrt(1 - g2 / 8);                              // steady-state wet ≈ dry level
  }

  process(L, R, n) {
    const { buf, len, pos, lp, x, g, pre, damp } = this;
    const mix = this.mix, dry = 1 - 0.5 * mix, inG = this.inGain;
    const h = 1 / Math.sqrt(8);
    for (let k = 0; k < n; k++) {
      const input = pre[this.prePos];
      pre[this.prePos] = 0.5 * (L[k] + R[k]) * inG;
      this.prePos = (this.prePos + 1) % pre.length;
      for (let i = 0; i < 8; i++) {
        lp[i] += damp * (buf[i][pos[i]] - lp[i]);
        x[i] = lp[i] * g[i];
      }
      // fast Walsh–Hadamard, orthonormal
      for (let s = 1; s < 8; s <<= 1) {
        for (let i = 0; i < 8; i += s << 1) {
          for (let j = i; j < i + s; j++) { const a = x[j], b = x[j + s]; x[j] = a + b; x[j + s] = a - b; }
        }
      }
      let wl = 0, wr = 0;
      for (let i = 0; i < 8; i++) {
        const y = x[i] * h;
        buf[i][pos[i]] = y + (i & 1 ? -input : input);
        pos[i] = pos[i] + 1 === len[i] ? 0 : pos[i] + 1;
        if (i & 1) wr += lp[i]; else wl += lp[i];
      }
      L[k] = L[k] * dry + 0.5 * mix * wl;
      R[k] = R[k] * dry + 0.5 * mix * wr;
    }
  }
}

/** Slow loudness normaliser toward a target RMS (no K-weighting yet), ± range, slewed. */
export class AutoGain {
  constructor(sampleRate, { targetDb = -18, rangeDb = 12, measureS = 3, slewS = 10 } = {}) {
    this.target = dbToGain(targetDb);
    this.lo = dbToGain(-rangeDb); this.hi = dbToGain(rangeDb);
    this.aP = 1 - Math.exp(-1 / (measureS * sampleRate));
    this.aG = 1 - Math.exp(-1 / (slewS * sampleRate));
    this.p = this.target ** 2;
    this.gain = 1;
  }

  process(L, R, n) {
    for (let k = 0; k < n; k++) {
      this.p += this.aP * (0.5 * (L[k] * L[k] + R[k] * R[k]) - this.p);
      const want = Math.min(this.hi, Math.max(this.lo, this.target / Math.sqrt(this.p + 1e-12)));
      this.gain += this.aG * (want - this.gain);
      L[k] *= this.gain; R[k] *= this.gain;
    }
  }
}

export class DcBlock {
  constructor(sampleRate, hz = 10) {
    this.r = 1 - (2 * Math.PI * hz) / sampleRate;
    this.x = new Float64Array(2); this.y = new Float64Array(2);
  }

  process(L, R, n) {
    const { r, x, y } = this;
    for (let k = 0; k < n; k++) {
      const l = L[k], rr = R[k];
      y[0] = l - x[0] + r * y[0]; x[0] = l; L[k] = y[0];
      y[1] = rr - x[1] + r * y[1]; x[1] = rr; R[k] = y[1];
    }
  }
}

/**
 * Lookahead peak limiter. The gain is the minimum need over the lookahead window, averaged over
 * the same window (so it ramps, never steps, and is already low enough when the peak arrives),
 * then released slowly. A final clamp at the ceiling catches anything left (counted in `clamps`).
 */
export class Limiter {
  constructor(sampleRate, { ceilingDb = -1, lookaheadMs = 5, releaseS = 0.25 } = {}) {
    this.ceiling = dbToGain(ceilingDb);
    const L = Math.max(1, Math.round((lookaheadMs / 1000) * sampleRate));
    this.L = L;
    const W = L + 1;                                                  // window: the sample out + L ahead
    this.dl = new Float64Array(W); this.dr = new Float64Array(W);
    this.need = new Float64Array(W).fill(1);
    this.dq = new Float64Array(W + 1);                                // monotonic deque of sample indices
    this.head = 0; this.tail = 0;
    this.box = new Float64Array(W).fill(1);
    this.boxSum = W;
    this.n = 0;
    this.rel = 1 - Math.exp(-1 / (releaseS * sampleRate));
    this.gain = 1;
    this.clamps = 0;
  }

  process(Lb, Rb, frames) {
    const W = this.L + 1, Q = W + 1, c = this.ceiling;
    const { dl, dr, need, dq, box } = this;
    for (let k = 0; k < frames; k++) {
      const n = this.n++;
      const slot = n % W;
      // sliding minimum of need over samples n−L … n (expire first: slot is about to be overwritten)
      while (this.tail > this.head && dq[this.head % Q] <= n - W) this.head++;
      const l = Lb[k], r = Rb[k];
      const peak = Math.max(Math.abs(l), Math.abs(r));
      need[slot] = peak > c ? c / peak : 1;
      while (this.tail > this.head && need[dq[(this.tail - 1) % Q] % W] >= need[slot]) this.tail--;
      dq[this.tail % Q] = n; this.tail++;
      const wmin = need[dq[this.head % Q] % W];
      // box average of the window minima over n−L … n: every term ≤ need[n−L], so the average is too
      this.boxSum += wmin - box[slot]; box[slot] = wmin;
      if (slot === 0) { let s = 0; for (let i = 0; i < W; i++) s += box[i]; this.boxSum = s; }   // no drift
      const safe = Math.min(1, this.boxSum / W);
      this.gain = safe < this.gain ? safe : this.gain + this.rel * (safe - this.gain);
      dl[slot] = l; dr[slot] = r;
      const read = slot + 1 === W ? 0 : slot + 1;                    // the sample from n − L
      const outL = dl[read] * this.gain, outR = dr[read] * this.gain;
      let ol = Number.isFinite(outL) ? outL : 0, or = Number.isFinite(outR) ? outR : 0;
      if (ol > c || ol < -c || or > c || or < -c) {
        if (Math.max(Math.abs(ol), Math.abs(or)) > c * (1 + 1e-9)) this.clamps++;   // not float rounding
        ol = Math.max(-c, Math.min(c, ol)); or = Math.max(-c, Math.min(c, or));
      }
      Lb[k] = ol; Rb[k] = or;
    }
  }
}
