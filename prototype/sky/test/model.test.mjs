// node --test prototype/sky/test/
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { loadDay } from '../data.js';
import { SkyModel, runSlow, PHASE } from '../model.js';
import { skyBodies } from '../astro.js';
import { Observer } from '../observer.js';

const root = new URL('../../../resources/sky/', import.meta.url);
const readJson = async (day) => JSON.parse(fs.readFileSync(new URL(`${day}/track.json`, root), 'utf8'));
const storm = await loadDay('2024-05-10', readJson);
const MIN = 60000;

function trace(model, ticks, every = 50) {
  const out = [];
  for (let k = 0; k < ticks; k++) {
    model.step();
    if (k % every === 0) {
      const s = model.snapshot();
      out.push([s.modelTimeUtc, s.phase, s.energy, s.gain, ...s.nodes.b, ...s.nodes.y, s.whistlerCount]);
    }
  }
  return out;
}

test('same seed + same track → identical snapshots', () => {
  const start = storm.startMs + 17.5 * 60 * MIN;          // around the storm's arrival
  const a = trace(new SkyModel(storm, { seed: 7, startMs: start, speed: 24 }), 6000);
  const b = trace(new SkyModel(storm, { seed: 7, startMs: start, speed: 24 }), 6000);
  assert.deepEqual(a, b);
});

test('a different seed gives a different sky', () => {
  const start = storm.startMs + 17.5 * 60 * MIN;
  const a = trace(new SkyModel(storm, { seed: 7, startMs: start }), 500);
  const b = trace(new SkyModel(storm, { seed: 8, startMs: start }), 500);
  assert.notDeepEqual(a, b);
});

test('slow sky is the same at every speed and matches runSlow', () => {
  const ref = runSlow(storm, { seed: 3 });
  const start = storm.startMs + 17 * 60 * MIN;            // the storm's arrival: phases change
  const first = Math.round((start - storm.startMs) / MIN);
  for (const speed of [6, 96]) {
    const m = new SkyModel(storm, { seed: 3, startMs: start, speed });
    let checked = 0, phases = new Set();
    // runSlow samples after the last slow step of minute k, i.e. when slow.t = ref.t[k] + 1 s
    for (let k = first; k < first + 90; k++) {
      while (m.slow.t < ref.t[k] + 1000) m.step();
      assert.equal(m.slow.t, ref.t[k] + 1000);
      assert.equal(m.slow.phase, ref.phase[k], `phase at speed ${speed}, minute ${k}`);
      assert.equal(Math.fround(m.slow.w / m.p.threshold), ref.energy[k], `energy at speed ${speed}, minute ${k}`);
      assert.equal(Math.fround(m.slow.gain), ref.gain[k]);
      phases.add(m.slow.phase);
      checked += 1;
    }
    assert.equal(checked, 90);
    assert.ok(phases.size >= 2, 'the window crosses a phase change');
  }
});

test('seeking back and forth reproduces the slow state', () => {
  const m = new SkyModel(storm, { seed: 5, startMs: storm.startMs });
  const t1 = storm.startMs + 30 * 60 * MIN;
  m.seek(t1);
  const at1 = structuredClone(m.slow);
  m.seek(storm.startMs + 40 * 60 * MIN);
  m.seek(storm.startMs + 2 * 60 * MIN);
  m.seek(t1);
  assert.deepEqual(m.slow, at1);
});

test('storm day: onsets during the storm, data-forced onsets on the CMO night', () => {
  const r = runSlow(storm, { seed: 1 });
  const onsets = r.events.filter((e) => e.type === 'onset');
  const arrival = Date.parse('2024-05-10T17:00:00Z');
  assert.ok(onsets.filter((e) => e.t > arrival).length >= 8);
  assert.ok(onsets.some((e) => e.kind === 'forced'));
  assert.ok(r.phase.includes(PHASE.RECOVERY));
});

test('quiet day: never dead, never alarming', async () => {
  const quiet = await loadDay('2025-01-25', readJson);
  const r = runSlow(quiet, { seed: 1 });
  const onsets = r.events.filter((e) => e.type === 'onset');
  assert.ok(onsets.length >= 2, 'some soft activity');
  assert.ok(onsets.every((e) => e.strength < 0.5), 'all soft');
  const m = new SkyModel(quiet, { seed: 1, startMs: quiet.startMs + 8 * 60 * MIN, speed: 24 });
  let maxB = 0, motion = 0;
  for (let k = 0; k < 3000; k++) {
    m.step();
    for (let i = 0; i < m.p.nodes; i++) { maxB = Math.max(maxB, m.node.b[i]); motion += Math.abs(m.node.vy[i]); }
  }
  assert.ok(motion > 0, 'the curtain moves');
  assert.ok(maxB < 0.6, `quiet brightness stays low (max ${maxB.toFixed(2)})`);
});

test('sun and moon within 2° of JPL Horizons at CMO', () => {
  const ref = JSON.parse(fs.readFileSync(new URL('./horizons-cmo.json', import.meta.url), 'utf8'));
  const daz = (a, b) => Math.abs(((a - b + 540) % 360) - 180);
  ref.times.forEach((t, i) => {
    const b = skyBodies(Date.parse(t), 64.874, -147.86);
    const sun = ref.rows['10'][i].map(Number), moon = ref.rows['301'][i].map(Number);
    assert.ok(daz(b.sunAz, sun[3]) < 2 && Math.abs(b.sunEl - sun[4]) < 2, `sun ${t}`);
    assert.ok(daz(b.moonAz, moon[3]) < 2 && Math.abs(b.moonEl - moon[4]) < 2, `moon ${t}`);
    assert.ok(Math.abs(b.moonIllum * 100 - moon[5]) < 3, `moon phase ${t}`);
  });
});

test('observer: CMO geomagnetic latitude and magnetic zenith', () => {
  const o = new Observer();
  assert.ok(Math.abs(o.mlat - 65.7) < 0.5);
  assert.ok(Math.abs(o.magZenith.el - 77) < 1.5);
  assert.ok(o.magZenith.az > 180 && o.magZenith.az < 225, 'magnetic zenith in the SSW');
});
