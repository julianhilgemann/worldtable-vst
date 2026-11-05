// Golden traces for the C++ port (M2): node prototype/sky/export-goldens.mjs [day] [speed]
// Writes tests/golden/sky/<day>.json at 1-minute resolution:
//   slow sky (speed-independent): phase, energy, activity, gain, unloading, coupling, ΔH model, arc latitude;
//   fast sky at the given Journey speed (default 24×): brightness and displacement of 8 sampled nodes;
//   onset events.
import fs from 'node:fs';
import { loadDay } from './data.js';
import { SkyModel, runSlow, DEFAULTS, TICK_S } from './model.js';

const day = process.argv[2] || '2024-05-10';
const speed = Number(process.argv[3] || 24);
const seed = 1;
const root = new URL('../../resources/sky/', import.meta.url);
const track = await loadDay(day, async (d) => JSON.parse(fs.readFileSync(new URL(`${d}/track.json`, root), 'utf8')));
const r4 = (x) => Math.round(x * 1e4) / 1e4;

const slow = runSlow(track, { seed });
const model = new SkyModel(track, { seed, startMs: track.startMs, speed });
const sampled = [0, 9, 18, 27, 36, 45, 54, 63];
const bright = sampled.map(() => []), disp = sampled.map(() => []);
const ticksPerMinute = 60 / (TICK_S * speed);
const t0 = performance.now();
for (let m = 0; m < track.length; m++) {
  for (let k = 0; k < ticksPerMinute; k++) model.step();
  sampled.forEach((i, j) => { bright[j].push(r4(model.node.b[i])); disp[j].push(r4(model.node.y[i])); });
}
const seconds = ((performance.now() - t0) / 1000).toFixed(1);

const golden = {
  about: 'Sky Model goldens from the T1.2 browser sketch (prototype/sky). The C++ port must match the slow '
    + 'channels closely (same algorithm, double precision) and the node channels statistically.',
  day, seed, speed, tickS: TICK_S, startUtc: new Date(track.startMs).toISOString(), stepS: 60, length: track.length,
  params: DEFAULTS,
  slow: {
    phase: Array.from(slow.phase), energy: Array.from(slow.energy, r4), activity: Array.from(slow.activity, r4),
    gain: Array.from(slow.gain, r4), unloading: Array.from(slow.unloading, r4), coupling: Array.from(slow.coupling, r4),
    dhModel: Array.from(slow.dhModel, r4), arcLatitude: Array.from(slow.ovalLatitude, r4),
  },
  nodes: { index: sampled, brightness: bright, displacementKm: disp },
  onsets: slow.events.filter((e) => e.type === 'onset')
    .map((e) => ({ utc: new Date(e.t).toISOString(), kind: e.kind, strength: r4(e.strength) })),
};
const out = new URL(`../../tests/golden/sky/${day}.json`, import.meta.url);
fs.mkdirSync(new URL('.', out), { recursive: true });
fs.writeFileSync(out, JSON.stringify(golden) + '\n');
console.log(`${day}: ${golden.onsets.length} onsets, ${track.length} minutes at ${speed}× (${seconds} s) → ${out.pathname}`);
