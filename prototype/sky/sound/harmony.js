// Harmony (spec/05-sound-engine.md §3): chord vocabulary per Mood, a progression driven by the
// sky's substorm phases, and voice-leading. Pure, 12-TET only: every note is an integer MIDI number.

import { rand01 } from '../rng.js';

export const MOODS = ['bright', 'bittersweet', 'nocturnal'];
/** Chord function per sky phase, indexed like PHASE (quiet, growth, onset, recovery). */
export const PHASE_FUNCTION = ['home', 'tension', 'awe', 'borrowed'];
const REASON = {
  home: 'quiet → home', tension: 'growth → tension', awe: 'onset → chromatic mediant',
  borrowed: 'recovery → borrowed chord',
};

/** Onsets weaker than this (pseudo-breakups are 0.1) get the tension move, not the awe move. */
export const WEAK_ONSET = 0.3;

const C = (deg, iv, name) => ({ deg, iv, name });
/**
 * deg = chord root in semitones above the key root; iv = intervals above the chord root.
 * A first pass, tuned by ear with the owner. Every chord has a perfect fifth (the red layer's drone).
 */
export const VOCABULARY = {
  bright: {
    home: [C(0, [0, 4, 7, 11], 'maj7'), C(0, [0, 4, 7, 14], 'add9'), C(0, [0, 4, 7, 9, 14], '6/9')],
    tension: [C(0, [0, 2, 7, 11], 'maj7sus2'), C(5, [0, 4, 7, 14], 'add9'), C(7, [0, 5, 7, 14], 'sus4 add9')],
    awe: [C(8, [0, 4, 7, 11], 'maj7'), C(4, [0, 4, 7, 14], 'add9'), C(9, [0, 4, 7, 14], 'add9')],
    borrowed: [C(5, [0, 3, 7, 14], 'm add9'), C(10, [0, 4, 7, 14], 'add9'), C(8, [0, 4, 7, 14], 'add9')],
  },
  bittersweet: {
    home: [C(0, [0, 4, 7, 14], 'add9'), C(0, [0, 4, 7, 11, 14], 'maj9'), C(9, [0, 3, 7, 10, 14], 'm9')],
    tension: [C(5, [0, 4, 7, 11], 'maj7'), C(2, [0, 3, 7, 10, 14], 'm9'), C(0, [0, 5, 7, 10], '7sus4')],
    awe: [C(8, [0, 4, 7, 11], 'maj7'), C(3, [0, 4, 7, 11], 'maj7')],
    borrowed: [C(5, [0, 3, 7, 14], 'm add9'), C(5, [0, 3, 7, 9], 'm6'), C(8, [0, 4, 7, 14], 'add9')],
  },
  nocturnal: {
    home: [C(0, [0, 3, 7, 10, 14], 'm9'), C(0, [0, 3, 7, 14], 'm add9'), C(0, [0, 3, 7, 10, 17], 'm11')],
    tension: [C(0, [0, 5, 7, 10], '7sus4'), C(10, [0, 2, 7, 14], 'sus2'), C(7, [0, 5, 7, 10], '7sus4')],
    awe: [C(8, [0, 4, 7, 11], 'maj7'), C(9, [0, 3, 7, 10], 'm7'), C(4, [0, 4, 7, 14], 'add9')],
    borrowed: [C(5, [0, 3, 7, 14], 'm add9'), C(10, [0, 4, 7, 14], 'add9'), C(3, [0, 4, 7, 11, 14], 'maj9')],
  },
};

// ------------------------------------------------------------------ names
export const KEY_NAMES = ['C', 'D♭', 'D', 'E♭', 'E', 'F', 'F♯', 'G', 'A♭', 'A', 'B♭', 'B'];
const LETTERS = ['C', 'D', 'E', 'F', 'G', 'A', 'B'];
const LETTER_PC = [0, 2, 4, 5, 7, 9, 11];
const DEGREE_LETTER = [0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6];      // ♭II on D, ♭III on E, ♭VI on A, ♭VII on B …
const ROMAN = ['I', '♭II', 'II', '♭III', 'III', 'IV', '♯IV', 'V', '♭VI', 'VI', '♭VII', 'VII'];

/** Spell the chord root from the key's letter, so D major's III is F♯ and its ♭VI is B♭. */
export function chordName(chord, key) {
  const keyLetter = LETTERS.indexOf(KEY_NAMES[key][0]);
  const letter = (keyLetter + DEGREE_LETTER[chord.deg]) % 7;
  const pc = (key + chord.deg) % 12;
  const acc = ((pc - LETTER_PC[letter] + 18) % 12) - 6;          // −2 … +2 semitones
  const sign = acc > 0 ? '♯'.repeat(acc) : '♭'.repeat(-acc);
  const minor = chord.iv.includes(3) && !chord.iv.includes(4);
  const roman = minor ? ROMAN[chord.deg].toLowerCase() : ROMAN[chord.deg];
  return { label: `${LETTERS[letter]}${sign}${chord.name}`, roman };
}

// ------------------------------------------------------------------ voicing
export const SLOTS = 5;
export const LAYERS = ['red', 'green', 'violet'];

/** Pitch classes per layer, padded to SLOTS. red: root + fifth; green: the chord; violet: upper tones. */
export function layerSlots(chord, key, layer) {
  const pcs = (ivs) => [...new Set(ivs.map((i) => (key + chord.deg + i) % 12))];
  const pad = (list) => Array.from({ length: SLOTS }, (_, k) => list[k % list.length]);
  if (layer === 'red') return pad(pcs(chord.iv.includes(7) ? [0, 7] : [0]));
  if (layer === 'green') {
    const tones = pcs(chord.iv);
    const priority = pcs([0, 7, ...chord.iv]);                    // pad with root, fifth, then the rest
    return [...tones, ...Array.from({ length: SLOTS }, (_, k) => priority[k % priority.length])].slice(0, SLOTS);
  }
  const upper = pcs(chord.iv.filter((i) => i % 12 !== 0 && i % 12 !== 7));
  return pad(upper.length ? upper : pcs([7, 0]));
}

const circ = (a, b) => { const d = Math.abs(a - b) % 12; return Math.min(d, 12 - d); };
const PERMS = (function permutations(n) {
  if (n === 1) return [[0]];
  const out = [];
  for (const p of permutations(n - 1)) for (let i = 0; i < n; i++) out.push([...p.slice(0, i), n - 1, ...p.slice(i)]);
  return out;
})(SLOTS).sort((a, b) => a.join() < b.join() ? -1 : 1);            // lexicographic, so ties break the same way

/** Reorder `next` so slot k moves from from[k] to the result's k-th pc with the least total motion. */
export function leadSlots(from, next) {
  let best = null, bestCost = Infinity;
  for (const p of PERMS) {
    let cost = 0;
    for (let k = 0; k < SLOTS; k++) cost += circ(from[k], next[p[k]]);
    if (cost < bestCost) { bestCost = cost; best = p; }
  }
  return { slots: best.map((i) => next[i]), cost: bestCost };
}

/** Nearest MIDI note with pitch class pc to `near`, folded into [lo, hi]. */
export function nearestNote(pc, near, lo, hi) {
  let n = near + ((((pc - near) % 12) + 18) % 12) - 6;               // within ±6 semitones of near
  while (n < lo) n += 12;
  while (n > hi) n -= 12;
  return n < lo ? n + 12 : n;                                         // a range narrower than an octave keeps lo
}

/** Layer register at Register 0 and Voicing spread 2: centre and half-width (semitones, MIDI). */
const LAYER_RANGE = { red: [40, 9], green: [62, 12], violet: [81, 9] };
export function layerRange(layer, register, spread) {
  const [centre, half] = LAYER_RANGE[layer];
  const h = Math.max(6, (half * spread) / 2);
  return [Math.round(centre + register - h), Math.round(centre + register + h)];
}

// ------------------------------------------------------------------ progression
/**
 * Chord choice from the sky's phases. Each phase move happens with probability = progression
 * (seeded by the move count); the return home always happens while progression > 0. In long
 * phases a seeded wander swaps to another chord of the same function, so Live at 1× keeps moving.
 * The engine applies requests when its resonator bank is free; a newer request replaces a pending one.
 */
export class Harmony {
  constructor({ seed = 1, mood = 'bright', key = 2, progression = 1 } = {}) {
    this.seed = seed >>> 0;
    this.mood = mood; this.key = key; this.progression = progression;
    this.moves = 0; this.wanders = 0;
    this.last = {};                                                   // last index per function: no repeats
    this.target = { fn: 'home', index: 0 };
    this.pending = null;
    this.lastChangeS = 0;
    this.wanderAfterS = this.wanderDelay();
  }

  chord(fn = this.target.fn, index = this.target.index) {
    const list = VOCABULARY[this.mood][fn];
    const c = list[Math.min(index, list.length - 1)];
    return { ...c, fn, index, key: this.key, ...chordName(c, this.key) };
  }

  wanderDelay() {
    return (150 + 210 * rand01(this.seed, 65, this.wanders)) / (0.5 + this.progression);
  }

  /** Initial chord for the sky's current phase (no request: the engine voices it directly). */
  start(phase) {
    if (this.progression > 0) this.target = { fn: PHASE_FUNCTION[phase], index: 0 };
  }

  /** Sky moves and user changes are urgent (the engine may damp a ringing bank); wanders wait. */
  request(fn, index, reason) {
    this.target = { fn, index };
    this.pending = { chord: this.chord(fn, index), reason, urgent: reason !== 'wander' };
  }

  /**
   * A sky phase change (PHASE index). `when` seeds the choice (the engine passes the model minute),
   * so different onsets and days pick different chords, and the same sky always picks the same.
   */
  onPhase(phase, when = 0, reason = null) {
    const fn = PHASE_FUNCTION[phase];
    this.moves += 1;
    if (this.progression <= 0) return;
    if (fn === 'home') {
      if (this.target.fn !== 'home') this.request('home', 0, REASON.home);
      return;
    }
    if (rand01(this.seed, 61, when) >= this.progression) return;
    const n = VOCABULARY[this.mood][fn].length;
    let index = Math.floor(rand01(this.seed, 63, when) * n);
    if (n > 1 && (index === this.last[fn] || (fn === this.target.fn && index === this.target.index))) index = (index + 1) % n;
    this.last[fn] = index;
    this.request(fn, index, reason || REASON[fn]);
  }

  /** A substorm onset: the awe move; a weak one (a quiet-day pseudo-breakup) only adds tension. */
  onOnset(strength, when = 0) {
    if (strength >= WEAK_ONSET) this.onPhase(2, when);
    else this.onPhase(1, when, 'weak onset → tension');
  }

  /** Called every control tick with real time in seconds. */
  update(realS) {
    if (this.progression <= 0 || this.pending || realS - this.lastChangeS < this.wanderAfterS) return;
    const n = VOCABULARY[this.mood][this.target.fn].length;
    if (n < 2) return;
    this.wanders += 1;
    const step = 1 + Math.floor(rand01(this.seed, 67, this.wanders) * (n - 1));
    this.request(this.target.fn, (this.target.index + step) % n, 'wander');
    this.lastChangeS = realS;                                          // wait a full delay before the next wander
  }

  /** The engine took the pending chord. */
  take(realS) {
    const p = this.pending;
    this.pending = null;
    this.lastChangeS = realS;
    this.wanders += 1;
    this.wanderAfterS = this.wanderDelay();
    return p;
  }

  setMood(mood) {
    if (mood === this.mood || !VOCABULARY[mood]) return;
    this.mood = mood;
    this.request(this.target.fn, this.target.index, 'mood');
  }

  setKey(key) {
    if (key === this.key) return;
    this.key = key;
    this.request(this.target.fn, this.target.index, 'root');
  }

  setProgression(amount) {
    this.progression = amount;
    if (amount <= 0 && (this.target.fn !== 'home' || this.target.index !== 0)) this.request('home', 0, 'hold');
  }

  /** Same chord, new register or spread. */
  revoice() { this.request(this.target.fn, this.target.index, 'voicing'); }
}
