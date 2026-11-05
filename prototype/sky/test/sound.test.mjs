// T1.3 sound sketch: harmony rules, one clock with the sky, block-size independence, safety.
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { loadDay } from '../data.js';
import { PHASE } from '../model.js';
import {
  VOCABULARY, MOODS, SLOTS, LAYERS, Harmony, layerSlots, leadSlots, nearestNote, layerRange, chordName,
} from '../sound/harmony.js';
import { SoundEngine } from '../sound/engine.js';

const root = new URL('../../../resources/sky/', import.meta.url);
const readJson = async (day) => JSON.parse(fs.readFileSync(new URL(`${day}/track.json`, root), 'utf8'));
const storm = await loadDay('2024-05-10', readJson);
const quiet = await loadDay('2025-01-25', readJson);
const ONSET = Date.parse('2024-05-10T17:37:00Z');                    // a loaded onset follows at 17:38
const CEILING = 10 ** (-1 / 20);

function render(engine, seconds, block = 4800) {
  const frames = Math.round(seconds * engine.sr);
  const L = new Float64Array(frames), R = new Float64Array(frames);
  const l = new Float64Array(block), r = new Float64Array(block);
  for (let at = 0; at < frames; at += block) {
    const n = Math.min(block, frames - at);
    engine.process(l, r, n);
    L.set(l.subarray(0, n), at); R.set(r.subarray(0, n), at);
  }
  return { L, R };
}

const maxStep = (a, from = 1, to = a.length) => {
  let m = 0;
  for (let i = Math.max(1, from); i < to; i++) m = Math.max(m, Math.abs(a[i] - a[i - 1]));
  return m;
};

test('harmony: every chord is 12-TET with a fifth, every layer has 5 slots of pitch classes', () => {
  for (const mood of MOODS) {
    for (const [fn, list] of Object.entries(VOCABULARY[mood])) {
      for (const chord of list) {
        assert.ok(chord.iv.includes(7), `${mood} ${fn} ${chord.name} has a perfect fifth`);
        for (const layer of LAYERS) {
          const slots = layerSlots(chord, 2, layer);
          assert.equal(slots.length, SLOTS);
          for (const pc of slots) assert.ok(Number.isInteger(pc) && pc >= 0 && pc < 12);
          const tones = new Set(chord.iv.map((i) => (2 + chord.deg + i) % 12));
          for (const pc of slots) assert.ok(tones.has(pc), `${layer} plays only chord tones`);
        }
      }
    }
  }
  assert.equal(chordName(VOCABULARY.bright.awe[0], 2).label, 'B♭maj7');   // D major: ♭VI is B♭, not A♯
  assert.equal(chordName(VOCABULARY.bright.awe[1], 2).label, 'F♯add9');   // III is F♯, not G♭
  assert.equal(chordName(VOCABULARY.bright.borrowed[0], 0).roman, 'iv');
});

test('harmony: voice-leading keeps every new tone and moves no more than the plain order', () => {
  const key = 0;
  const cmaj7 = layerSlots(VOCABULARY.bright.home[0], key, 'green');        // C E G B C
  const abmaj7 = layerSlots(VOCABULARY.bright.awe[0], key, 'green');        // A♭ C E♭ G A♭
  const { slots, cost } = leadSlots(cmaj7, abmaj7);
  assert.deepEqual([...slots].sort(), [...abmaj7].sort());
  const circ = (a, b) => Math.min((a - b + 12) % 12, (b - a + 12) % 12);
  const plain = cmaj7.reduce((sum, pc, k) => sum + circ(pc, abmaj7[k]), 0);
  assert.ok(cost <= plain);
  assert.equal(cost, 8);                                                    // C→C, E→E♭, G→G, B→A♭, C→A♭
  for (let near = 30; near < 100; near += 7) {
    for (let pc = 0; pc < 12; pc++) {
      const [lo, hi] = layerRange('green', 0, 2);
      const n = nearestNote(pc, near, lo, hi);
      assert.ok(Number.isInteger(n) && ((n % 12) + 12) % 12 === pc && n >= lo && n <= hi);
      if (near >= lo + 6 && near <= hi - 6) assert.ok(Math.abs(n - near) <= 6);
    }
  }
});

test('harmony: the sky writes the dramaturgy; amount 0 holds home; same sky → same chords', () => {
  const run = (progression) => {
    const h = new Harmony({ seed: 3, progression });
    const out = [];
    const moves = [[PHASE.GROWTH, 100], [PHASE.ONSET, 160], [PHASE.RECOVERY, 180], [PHASE.QUIET, 260]];
    for (const [phase, when] of moves) {
      if (phase === PHASE.ONSET) h.onOnset(1.0, when); else h.onPhase(phase, when);
      const p = h.pending && h.take(when);
      out.push(p ? `${p.chord.fn}:${p.chord.label}` : '-');
    }
    return out;
  };
  const a = run(1);
  assert.deepEqual(a.map((x) => x.split(':')[0]), ['tension', 'awe', 'borrowed', 'home']);
  assert.deepEqual(run(1), a);
  assert.deepEqual(run(0), ['-', '-', '-', '-']);
  const weak = new Harmony({ seed: 3 });
  weak.onOnset(0.1, 50);
  assert.equal(weak.pending.chord.fn, 'tension');                         // a pseudo-breakup isn't the awe move
});

test('engine: one clock with the sky (ADR-0007) at 48 and 44.1 kHz', () => {
  for (const sampleRate of [48000, 44100]) {
    const e = new SoundEngine({ track: storm, startMs: ONSET, speed: 24, sampleRate });
    render(e, 2, 1000);
    assert.equal(e.ticks, 200);
    assert.equal(e.model.tick, 200);
    assert.equal(e.model.modelMs, ONSET + 2 * 24 * 1000);
  }
});

test('engine: same output whatever the block size; same seed → same sound', () => {
  const make = (seed = 1) => new SoundEngine({ track: storm, seed, startMs: ONSET, speed: 24 });
  const a = render(make(), 3, 4800), b = render(make(), 3, 128), c = render(make(), 3, 37);
  assert.deepEqual(b.L, a.L); assert.deepEqual(c.R, a.R);
  const d = render(make(2), 3, 4800);
  assert.notDeepEqual(d.L, a.L);
});

test('engine: a minute of storm with an onset and a chord change is safe (no NaN, ≤ −1 dBFS, no clicks)', () => {
  const e = new SoundEngine({ track: storm, startMs: ONSET, speed: 24 });
  const { L, R } = render(e, 60);
  const events = e.takeOutbox();
  assert.ok(events.some((x) => x.type === 'onset'));
  assert.ok(events.some((x) => x.type === 'chord' && x.reason.startsWith('onset')));
  let peak = 0;
  for (let i = 0; i < L.length; i++) {
    assert.ok(Number.isFinite(L[i]) && Number.isFinite(R[i]));
    peak = Math.max(peak, Math.abs(L[i]), Math.abs(R[i]));
  }
  assert.ok(peak <= CEILING + 1e-12, `peak ${peak}`);
  assert.ok(Math.max(maxStep(L), maxStep(R)) < 0.25);                     // the POC's NoClicks threshold
  assert.equal(e.limiter.clamps, 0);
  assert.equal(e.resets, 0);
});

test('engine: user changes (root, mood, voicing) crossfade without clicks', () => {
  const e = new SoundEngine({ track: quiet, startMs: Date.parse('2025-01-25T05:00:00Z'), speed: 1 });
  const before = render(e, 8);
  e.setSound({ key: 7 });                                                  // D → G
  const during = render(e, 8);
  e.setSound({ mood: 'nocturnal', register: 5 });
  const after = render(e, 8);
  const reasons = e.takeOutbox().filter((x) => x.type === 'chord').map((x) => x.reason);
  assert.deepEqual(reasons.slice(1), ['root', 'voicing']);                // mood + register: the newest request wins
  const base = maxStep(before.L, 48000);
  assert.ok(maxStep(during.L) < 1.5 * base + 0.01 && maxStep(after.L) < 1.5 * base + 0.01);
});
