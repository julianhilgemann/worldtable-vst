// Journey bounce from the command line (T1.3): the same engine as the page, rendered to a WAV.
//   node prototype/sky/bounce.mjs --day 2024-05-10 --from 2024-05-10T17:20 [--minutes 5] [--speed 24]
//        [--seed 1] [--rate 48000] [--out renders/x.wav] [--s.<sound constant> v] [--p.<model constant> v]
import fs from 'node:fs';
import path from 'node:path';
import { loadDay } from './data.js';
import { DEFAULTS } from './model.js';
import { SOUND_DEFAULTS } from './sound/engine.js';
import { renderBounce } from './sound/render.js';

const args = {};
for (let i = 2; i < process.argv.length; i += 2) args[process.argv[i].replace(/^--/, '')] = process.argv[i + 1];
const day = args.day || '2024-05-10';
const root = new URL('../../resources/sky/', import.meta.url);
const track = await loadDay(day, async (d) => JSON.parse(fs.readFileSync(new URL(`${d}/track.json`, root), 'utf8')));
const num = (v) => (Number.isNaN(Number(v)) ? v : Number(v));
const pick = (prefix, defaults) => Object.fromEntries(Object.entries(args)
  .filter(([k]) => k.startsWith(prefix) && k.slice(prefix.length) in defaults)
  .map(([k, v]) => [k.slice(prefix.length), num(v)]));
const sound = pick('s.', SOUND_DEFAULTS), params = pick('p.', DEFAULTS);
const from = args.from || `${day}T00:00`;
const speed = Number(args.speed ?? 24), seed = Number(args.seed ?? 1), minutes = Number(args.minutes ?? 5);
const sampleRate = Number(args.rate ?? 48000);
const voices = DEFAULTS.nodes * (sound.voicesPerNode ?? SOUND_DEFAULTS.voicesPerNode);
const out = args.out || `renders/T1.3-${day}-${from.slice(11, 16).replace(':', '')}-${speed}x-s${seed}-n${voices}.wav`;

let shown = -1;
const { wav, stats, events } = renderBounce({
  track, seed, params, sound, startMs: Date.parse(`${from}Z`), speed, seconds: minutes * 60, sampleRate,
  onProgress: (f) => { const p = Math.floor(f * 10); if (p !== shown) { shown = p; process.stderr.write(`${p * 10}% `); } },
});
process.stderr.write('\n');
fs.mkdirSync(path.dirname(out), { recursive: true });
fs.writeFileSync(out, Buffer.from(wav));

const mmss = (s) => `${String(Math.floor(s / 60)).padStart(1, '0')}:${String(Math.floor(s % 60)).padStart(2, '0')}`;
const hhmm = (ms) => new Date(ms).toISOString().slice(11, 16);
console.log(`${out}`);
console.log(`  model ${stats.modelStart.slice(0, 16)} → ${stats.modelEnd.slice(0, 16)} UT at ${speed}×, seed ${seed}, ${stats.voices} voices/layer`);
console.log(`  peak ${stats.peakDb.toFixed(2)} dBFS · RMS ${stats.rmsDb.toFixed(1)} dBFS · max step ${stats.maxStep.toFixed(4)}`
  + ` · non-finite ${stats.nonFinite} · limiter clamps ${stats.limiterClamps} · resets ${stats.resets}`);
console.log(`  ${stats.realtimeFactor.toFixed(1)}× real time (${(100 / stats.realtimeFactor).toFixed(0)} % of a core)`);
for (const e of events) {
  if (e.type === 'chord') console.log(`  ${mmss(e.atS)}  (${hhmm(e.t)} UT)  chord → ${e.label} (${e.roman}) · ${e.reason}`);
  else if (e.type === 'onset') console.log(`  ${mmss(e.atS)}  (${hhmm(e.t)} UT)  onset · ${e.kind} · strength ${e.strength.toFixed(1)}`);
}
