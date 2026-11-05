// Journey bounce: render a stretch of the sky to a WAV, with a pre-roll so the resonators are
// already ringing when the file starts. Used by bounce.mjs (Node) and bounce-worker.js (browser).
// Same arguments → same bytes.

import { SoundEngine } from './engine.js';
import { TICK_S } from '../model.js';
import { Wav16 } from './wav.js';

/**
 * @param o { track, seed, params, sound, startMs, speed, seconds, sampleRate, preRollS, fadeInS,
 *            fadeOutS, block, onProgress(fraction) }
 * @returns { wav: ArrayBuffer, stats, events }
 */
export function renderBounce({
  track, seed = 1, params = {}, sound = {}, startMs, speed = 24, seconds = 300, sampleRate = 48000,
  preRollS = 20, fadeInS = 2, fadeOutS = 3, block = 4800, onProgress,
} = {}) {
  // start early enough that the pre-roll ends exactly at startMs (model time per tick: 10 ms × speed)
  const msPerTick = TICK_S * 1000 * speed;
  const wantTicks = Math.round(preRollS / TICK_S);
  const preTicks = speed > 0 ? Math.min(wantTicks, Math.floor((startMs - track.startMs) / msPerTick)) : wantTicks;
  const engine = new SoundEngine({ track, seed, params, sound, sampleRate, speed, startMs: startMs - preTicks * msPerTick });
  const L = new Float64Array(block), R = new Float64Array(block);
  const t0 = Date.now();

  let pre = Math.round(preTicks * engine.tickEvery);
  while (pre > 0) { const n = Math.min(block, pre); engine.process(L, R, n); pre -= n; }
  engine.takeOutbox();                                                // pre-roll events aren't in the file

  const frames = Math.round(seconds * sampleRate);
  const wav = new Wav16(frames, sampleRate, seed);
  const fadeIn = fadeInS * sampleRate, fadeOut = fadeOutS * sampleRate;
  const stats = { peak: 0, maxStep: 0, sumSq: 0, nonFinite: 0 };
  const events = [];
  let prevL = null, prevR = null;
  for (let at = 0; at < frames; at += block) {
    const n = Math.min(block, frames - at);
    engine.process(L, R, n);
    for (let k = 0; k < n; k++) {
      const i = at + k;
      const fade = Math.min(1, i < fadeIn ? 0.5 - 0.5 * Math.cos((Math.PI * i) / fadeIn) : 1,
        frames - i < fadeOut ? 0.5 - 0.5 * Math.cos((Math.PI * (frames - i)) / fadeOut) : 1);
      const l = L[k] * fade, r = R[k] * fade;
      if (!Number.isFinite(l) || !Number.isFinite(r)) stats.nonFinite++;
      stats.peak = Math.max(stats.peak, Math.abs(l), Math.abs(r));
      if (prevL !== null) stats.maxStep = Math.max(stats.maxStep, Math.abs(l - prevL), Math.abs(r - prevR));
      prevL = l; prevR = r;
      stats.sumSq += 0.5 * (l * l + r * r);
      L[k] = l; R[k] = r;
    }
    wav.write(L, R, n);
    for (const e of engine.takeOutbox()) events.push({ ...e, atS: (at + n) / sampleRate });
    onProgress?.((at + n) / frames);
  }
  const rmsDb = 10 * Math.log10(stats.sumSq / frames + 1e-20);
  return {
    wav: wav.buffer,
    events,
    stats: {
      peakDb: 20 * Math.log10(stats.peak + 1e-20), rmsDb, maxStep: stats.maxStep, nonFinite: stats.nonFinite,
      limiterClamps: engine.limiter.clamps, resets: engine.resets, voices: engine.V,
      seconds, realtimeFactor: seconds / ((Date.now() - t0) / 1000),
      modelStart: new Date(startMs).toISOString(), modelEnd: new Date(engine.model.modelMs).toISOString(),
    },
  };
}
