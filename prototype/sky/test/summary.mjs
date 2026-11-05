// Tuning aid: run the slow sky over each recorded day and print phases and onsets.
// node prototype/sky/test/summary.mjs [day] [seed]
import fs from 'node:fs';
import { DAYS, loadDay } from '../data.js';
import { runSlow, PHASE_NAMES } from '../model.js';

const root = new URL('../../../resources/sky/', import.meta.url);
const readJson = async (day) => JSON.parse(fs.readFileSync(new URL(`${day}/track.json`, root), 'utf8'));
const only = process.argv[2];
const seed = Number(process.argv[3] ?? 1);
const hhmm = (ms) => new Date(ms).toISOString().slice(5, 16).replace('T', ' ');

for (const { id } of DAYS) {
  if (only && only !== id) continue;
  const track = await loadDay(id, readJson);
  const t0 = performance.now();
  const r = runSlow(track, { seed });
  const ms = performance.now() - t0;
  const onsets = r.events.filter((e) => e.type === 'onset');
  const share = [0, 0, 0, 0];
  for (const ph of r.phase) share[ph] += 1;
  console.log(`\n${id}  baseline H ${track.baselineH.toFixed(1)} nT  (${ms.toFixed(0)} ms)`);
  console.log('  phase share', PHASE_NAMES.map((n, i) => `${n} ${(100 * share[i] / r.phase.length).toFixed(0)}%`).join(' · '));
  console.log(`  gain ${Math.min(...r.gain).toFixed(2)}–${Math.max(...r.gain).toFixed(2)}  activity ${Math.min(...r.activity).toFixed(2)}–${Math.max(...r.activity).toFixed(2)}  oval ${Math.min(...r.ovalLatitude).toFixed(1)}–${Math.max(...r.ovalLatitude).toFixed(1)}°`);
  console.log(`  ${onsets.length} onsets:`, onsets.map((e) => `${hhmm(e.t)}${e.kind === "forced" ? "*" : e.kind === "pseudo" ? "~" : ""}(${e.strength.toFixed(1)})`).join('  '));
}
console.log('\n* = data-forced (H bay at CMO), ~ = pseudo-breakup; (n) = strength');
