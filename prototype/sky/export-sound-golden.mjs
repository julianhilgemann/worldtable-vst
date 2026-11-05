// Sound golden for the C++ port (T3.1): node prototype/sky/export-sound-golden.mjs [day] [from] [seconds]
// Runs the T1.3 SoundEngine dry (resonator bank only, effects bypassed) and writes
// tests/golden/sound/<day>-<hhmm>.json:
//   the chord requests Harmony made, with the tick they were made on (the C++ test replays them);
//   the notes each applied chord was voiced to (bank, layer, voice);
//   the dry RMS per 100 ms (L, R) and raw sample windows around the interesting moments.
// engine.js is unchanged: the loop below is SoundEngine.process() without the effects.
import fs from 'node:fs';
import { loadDay } from './data.js';
import { SoundEngine, SOUND_DEFAULTS } from './sound/engine.js';

const day = process.argv[2] || '2024-05-10';
const from = process.argv[3] || '17:20';
const seconds = Number(process.argv[4] || 120);
const seed = 1, speed = 24, sampleRate = 48000, rmsWindow = 4800, sampleWindow = 256;
const root = new URL('../../resources/sky/', import.meta.url);
const track = await loadDay(day, async (d) => JSON.parse(fs.readFileSync(new URL(`${d}/track.json`, root), 'utf8')));
const startMs = Date.parse(`${day}T${from}:00Z`);
const sig = (x) => Number(x.toPrecision(9));

const engine = new SoundEngine({ track, seed, startMs, speed, sampleRate });

// Record what Harmony asks for and what the engine applies.
const requests = [], applied = [];
const chordOf = (c) => ({ key: c.key, degree: c.deg, intervals: c.iv, label: c.label });
const notesOf = (bank) => engine.notes[bank].map((layer) => Array.from(layer));
const request = engine.harmony.request.bind(engine.harmony);
engine.harmony.request = (fn, index, reason) => {
  request(fn, index, reason);
  const p = engine.harmony.pending;
  requests.push({ tick: engine.ticks, ...chordOf(p.chord), urgent: p.urgent, reason });
};
const applyChord = engine.applyChord.bind(engine);
engine.applyChord = (p, realS) => {
  applyChord(p, realS);
  applied.push({ tick: engine.ticks, bank: engine.cur, ...chordOf(p.chord), notes: notesOf(engine.cur) });
};
const start = { tick: 0, bank: 0, ...chordOf(engine.applied), notes: notesOf(0) };

// SoundEngine.process() without reverb, auto-gain, DC blocker and limiter.
const frames = Math.round(seconds * sampleRate);
const L = new Float64Array(frames), R = new Float64Array(frames);
const onsets = [];
const t0 = performance.now();
for (let done = 0; done < frames;) {
  if (engine.n === engine.nextTickAt) {
    engine.tick();
    for (const e of engine.takeOutbox()) {
      if (e.type === 'onset') onsets.push({ tick: engine.ticks, strength: sig(e.strength), node: e.node ?? engine.model.slow.onsetNode });
    }
  }
  const run = Math.min(frames - done, engine.nextTickAt - engine.n);
  engine.bank.process(L, R, done, run);
  engine.n += run; done += run;
}
const elapsed = (performance.now() - t0) / 1000;

const rms = (x, a, n) => { let s = 0; for (let k = a; k < a + n; k++) s += x[k] * x[k]; return Math.sqrt(s / n); };
const rmsL = [], rmsR = [];
for (let a = 0; a + rmsWindow <= frames; a += rmsWindow) { rmsL.push(sig(rms(L, a, rmsWindow))); rmsR.push(sig(rms(R, a, rmsWindow))); }

// Sample windows: the start, steady ringing, each onset strike, each chord change and its crossfade, the end.
const at = new Set([0.5, 10, 30, seconds - 0.5].map((s) => Math.round(s * sampleRate)));
const tickSample = (tick) => Math.round(tick * sampleRate / 100);
for (const o of onsets) [0.2, 1, 4].forEach((s) => at.add(tickSample(o.tick) + Math.round(s * sampleRate)));
for (const c of applied) [0.1, 6, 11.9].forEach((s) => at.add(tickSample(c.tick) + Math.round(s * sampleRate)));
const windows = [...at].filter((s) => s + sampleWindow <= frames).sort((a, b) => a - b)
  .map((s) => ({ start: s, left: Array.from(L.subarray(s, s + sampleWindow), sig), right: Array.from(R.subarray(s, s + sampleWindow), sig) }));

let peak = 0, sumSq = 0;
for (let k = 0; k < frames; k++) { peak = Math.max(peak, Math.abs(L[k]), Math.abs(R[k])); sumSq += 0.5 * (L[k] * L[k] + R[k] * R[k]); }

const golden = {
  about: 'Sound golden from the T1.3 sketch (prototype/sky/sound), dry: the resonator bank before any effect. '
    + 'The C++ SkySound replays the chord requests at their ticks and must match the RMS and the sample windows.',
  day, from: new Date(startMs).toISOString(), seconds, seed, speed, sampleRate, voicesPerNode: SOUND_DEFAULTS.voicesPerNode,
  voices: engine.V, sound: SOUND_DEFAULTS,
  start, requests, applied, onsets,
  rms: { window: rmsWindow, left: rmsL, right: rmsR },
  windows,
  stats: { peak: sig(peak), rms: sig(Math.sqrt(sumSq / frames)), resets: engine.resets },
};
const hhmm = from.replace(':', '');
const out = new URL(`../../tests/golden/sound/${day}-${hhmm}.json`, import.meta.url);
fs.mkdirSync(new URL('.', out), { recursive: true });
fs.writeFileSync(out, JSON.stringify(golden) + '\n');
console.log(`${day} ${from} UT, ${seconds} s at ${speed}× (${elapsed.toFixed(1)} s): start ${start.label}, `
  + `${requests.length} requests, ${applied.length} changes, ${onsets.length} onsets, `
  + `${windows.length} windows, peak ${(20 * Math.log10(peak)).toFixed(1)} dBFS → ${out.pathname}`);
for (const r of requests) console.log(`  tick ${r.tick} (${(r.tick / 100).toFixed(2)} s) request ${r.label} · ${r.reason}${r.urgent ? ' · urgent' : ''}`);
for (const c of applied) console.log(`  tick ${c.tick} (${(c.tick / 100).toFixed(2)} s) applied ${c.label} → bank ${c.bank}`);
for (const o of onsets) console.log(`  tick ${o.tick} (${(o.tick / 100).toFixed(2)} s) onset ${o.strength} at node ${o.node}`);
