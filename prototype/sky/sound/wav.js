// 16-bit stereo PCM WAV with seeded TPDF dither (deterministic files). Pure.

import { hash32 } from '../rng.js';

export class Wav16 {
  constructor(frames, sampleRate, seed = 1) {
    this.buffer = new ArrayBuffer(44 + frames * 4);
    this.view = new DataView(this.buffer);
    this.frames = frames;
    this.at = 0;
    this.rng = hash32(seed ^ 0x5eed) | 1;
    const v = this.view, str = (o, s) => [...s].forEach((ch, i) => v.setUint8(o + i, ch.charCodeAt(0)));
    str(0, 'RIFF'); v.setUint32(4, 36 + frames * 4, true); str(8, 'WAVE');
    str(12, 'fmt '); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 2, true);
    v.setUint32(24, sampleRate, true); v.setUint32(28, sampleRate * 4, true); v.setUint16(32, 4, true);
    v.setUint16(34, 16, true);
    str(36, 'data'); v.setUint32(40, frames * 4, true);
  }

  rand() {                                                            // xorshift32, uniform [0, 1)
    let x = this.rng;
    x ^= x << 13; x ^= x >>> 17; x ^= x << 5;
    this.rng = x;
    return (x >>> 0) / 4294967296;
  }

  write(L, R, n) {
    const v = this.view;
    for (let k = 0; k < n && this.at < this.frames; k++, this.at++) {
      const o = 44 + this.at * 4;
      v.setInt16(o, this.q(L[k]), true);
      v.setInt16(o + 2, this.q(R[k]), true);
    }
  }

  q(x) {
    const d = this.rand() - this.rand();                              // TPDF, ±1 LSB
    return Math.max(-32768, Math.min(32767, Math.round(x * 32767 + d)));
  }
}
