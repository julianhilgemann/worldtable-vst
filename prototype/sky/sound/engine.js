// Sound engine (spec/05-sound-engine.md §1–4): the curtain, heard. The processBlock analogue.
//
// One clock (ADR-0007): the engine owns the SkyModel and steps it every sampleRate/100 samples,
// counted by a running sample counter, so the output is bit-identical whatever the block size.
// Pure: runs the same in an AudioWorklet, a Worker and Node.

import { SkyModel, TICK_S, PHASE } from '../model.js';
import { Observer } from '../observer.js';
import { rand01, noise1 } from '../rng.js';
import { Harmony, LAYERS, SLOTS, layerSlots, leadSlots, nearestNote, layerRange } from './harmony.js';
import { ResonatorBank } from './bank.js';
import { Reverb, AutoGain, DcBlock, Limiter } from './fx.js';

/** Sound constants (internal; the plug-in's parameters come later in parameters.yaml). */
export const SOUND_DEFAULTS = {
  voicesPerNode: 1,          // 1 → 64 resonators per layer, 2 → 128 (fixed per engine)
  mood: 'bright',
  key: 2,                    // D
  progression: 1,            // 0 hold one chord … 1 follow the sky
  register: 0,               // semitones
  spread: 2,                 // octaves per layer (voicing spread)
  red: 1.0, green: 0.7, violet: 0.4,           // layer levels
  redT60: 20, greenT60: 6, violetT60: 2.5,     // s
  bed: 0.3,                  // continuous excitation, × (0.5 + activity)
  rise: 0.4,                 // excitation per unit of brightness rise per second
  strike: 1.0,               // onset strike level
  detuneCents: 3,            // at full fold displacement
  driftCents: 4,             // slow per-voice wander
  gainSlewS: 2,              // brightness → gain (rule 4: ≥ 2 s)
  xfadeS: 12,                // chord change: excitation moves to the new bank over this time
  width: 0.9,                // stereo width of the dome azimuth
  reverbMix: 0.35, reverbT60: 9,
};

const clamp = (x, a, b) => (x < a ? a : x > b ? b : x);
const midiHz = (m) => 440 * 2 ** ((m - 69) / 12);
const ALT_KM = 110;                                                   // green line altitude for the pan
const DAMP_T60 = 3;                                                   // s: an urgent change damps the old bank

export class SoundEngine {
  /**
   * @param o { track, seed, params, sound, startMs, speed, sampleRate, observer,
   *            fast: { node, tick } — the curtain to continue from (sound switched on mid-flight) }
   */
  constructor({ track, seed = 1, params = {}, sound = {}, startMs, speed = 24, sampleRate = 48000, observer, fast } = {}) {
    this.sr = sampleRate;
    this.seed = seed >>> 0;
    this.track = track;
    this.obs = observer || new Observer();
    this.s = { ...SOUND_DEFAULTS, ...sound };
    this.model = new SkyModel(track, { seed, params, startMs, speed, observer: this.obs });
    if (fast) this.adoptFast(fast);

    const N = this.model.p.nodes;
    this.vpn = this.s.voicesPerNode;
    const V = this.V = N * this.vpn;
    this.bank = new ResonatorBank(V, sampleRate, this.seed);
    this.harmony = new Harmony({ seed, mood: this.s.mood, key: this.s.key, progression: this.s.progression });
    this.harmony.start(this.model.slow.phase);                       // begin on the chord the sky is in

    this.pos = new Float64Array(V);                                   // position along the chain (nodes)
    for (let v = 0; v < V; v++) this.pos[v] = Math.min(N - 1, v / this.vpn);
    this.slotOf = new Int8Array(V);
    for (let v = 0; v < V; v++) this.slotOf[v] = v % SLOTS;
    this.notes = [0, 1].map(() => LAYERS.map(() => new Int16Array(V)));
    this.slotPcs = [0, 1].map(() => LAYERS.map(() => new Array(SLOTS).fill(0)));
    this.gain = new Float64Array(3 * V);
    this.prevB = new Float64Array(V); this.rise = new Float64Array(V);
    this.panNode = new Float64Array(N); this.presNode = new Float64Array(N);
    this.panL = new Float64Array(V); this.panR = new Float64Array(V); this.presence = new Float64Array(V);
    this.panInit = false;

    // clock
    this.tickEvery = sampleRate * TICK_S;                            // 480 at 48 kHz, 441 at 44.1 kHz
    this.n = 0; this.ticks = 0; this.nextTickAt = 0;

    // harmony state: bank `cur` takes the excitation; the other rings out, then retires
    this.cur = 0; this.xf = 1; this.xfStartS = -Infinity; this.xfEndS = -Infinity; this.dampStartS = null;
    this.applied = this.harmony.chord();
    this.voiceBank(0, this.applied, null);
    this.bank.active[0] = true;
    this.strikeT0 = -Infinity; this.strikeAmp = 0; this.strikeVoice = V / 2;

    this.reverb = new Reverb(sampleRate, { t60: this.s.reverbT60, mix: this.s.reverbMix });
    this.autoGain = new AutoGain(sampleRate);
    this.dc = new DcBlock(sampleRate);
    this.limiter = new Limiter(sampleRate);
    this.preGain = 2 / Math.sqrt(3 * V);
    this.scratch = [new Float64Array(0), new Float64Array(0)];
    this.outbox = [{ t: this.model.modelMs, type: 'chord', label: this.applied.label, roman: this.applied.roman, reason: 'start' }];
    this.resets = 0;
  }

  adoptFast({ node, tick }) {
    for (const key of Object.keys(this.model.node)) if (key !== 'mlon' && node[key]) this.model.node[key].set(node[key]);
    this.model.tick = tick;
  }

  get realS() { return this.ticks * TICK_S; }

  // ---------------------------------------------------------------- controls (between blocks)
  seek(ms) { this.model.seek(ms); this.harmony.onPhase(this.model.slow.phase, Math.floor(ms / 60000)); }

  setSpeed(speed) { this.model.setSpeed(speed); }

  setSound(patch) {
    Object.assign(this.s, patch);
    if ('mood' in patch) this.harmony.setMood(patch.mood);
    if ('key' in patch) this.harmony.setKey(patch.key);
    if ('progression' in patch) this.harmony.setProgression(patch.progression);
    if ('register' in patch || 'spread' in patch) this.harmony.revoice();
    if ('reverbMix' in patch || 'reverbT60' in patch) this.reverb.set({ t60: this.s.reverbT60, mix: this.s.reverbMix });
  }

  /** New sky (seed or tuning) at the current model time; the curtain keeps moving. */
  rebuildSky({ seed = this.model.seed, params = {} } = {}) {
    const old = this.model;
    this.model = new SkyModel(this.track, { seed, params, startMs: old.modelMs, speed: old.speed, observer: this.obs });
    this.model.node = old.node; this.model.tick = old.tick;
  }

  takeOutbox() { const e = this.outbox; this.outbox = []; return e; }

  snapshot() {
    return { ...this.model.snapshot(), chord: this.applied.label, roman: this.applied.roman, voices: this.V };
  }

  // ---------------------------------------------------------------- audio
  /** Render `frames` samples into L and R (any block size, any typed array). */
  process(L, R, frames) {
    if (this.scratch[0].length < frames) this.scratch = [new Float64Array(frames), new Float64Array(frames)];
    const [l, r] = this.scratch;
    let done = 0;
    while (done < frames) {
      if (this.n === this.nextTickAt) this.tick();
      const run = Math.min(frames - done, this.nextTickAt - this.n);
      this.bank.process(l, r, done, run);
      this.n += run; done += run;
    }
    this.reverb.process(l, r, frames);
    this.autoGain.process(l, r, frames);
    this.dc.process(l, r, frames);
    this.limiter.process(l, r, frames);
    for (let k = 0; k < frames; k++) { L[k] = l[k]; R[k] = r[k]; }
  }

  // ---------------------------------------------------------------- control tick (100 Hz)
  tick() {
    const m = this.model, s = this.s, V = this.V, N = m.p.nodes, nd = m.node;
    m.step();
    this.ticks += 1;
    this.nextTickAt = Math.round(this.ticks * this.tickEvery);
    const tickLen = this.nextTickAt - this.n;
    const realS = this.realS;

    for (const e of m.takeEvents()) {
      const minute = Math.floor(e.t / 60000);
      if (e.type === 'phase' && e.phase !== PHASE.ONSET) this.harmony.onPhase(e.phase, minute);
      if (e.type === 'onset') {
        this.harmony.onOnset(e.strength, minute);
        this.strikeT0 = realS;
        this.strikeAmp = clamp(0.3 + 0.35 * e.strength, 0.3, 1.5);
        this.strikeVoice = m.slow.onsetNode * this.vpn;
      }
      this.push(e);
    }

    // chord changes: the old bank retires once silent (one T60 after the crossfade), then the next may
    // come. An urgent change (sky phase, user) doesn't wait: it ends the crossfade and damps the old
    // bank over DAMP_T60 instead.
    if (this.xf < 1) {
      this.xf = Math.min(1, (realS - this.xfStartS) / s.xfadeS);
      if (this.xf === 1) this.xfEndS = realS;
    }
    const other = 1 - this.cur;
    this.harmony.update(realS);
    const pending = this.harmony.pending;
    if (pending?.urgent && this.bank.active[other] && this.dampStartS === null) {
      if (this.xf < 1) { this.xf = 1; this.xfEndS = realS; }
      this.dampStartS = realS;
    }
    const ringOut = this.dampStartS !== null ? DAMP_T60 : Math.max(s.redT60, s.greenT60, s.violetT60);
    const since = this.dampStartS !== null ? this.dampStartS : this.xfEndS;
    if (this.bank.active[other] && this.xf === 1 && realS - since >= ringOut) {
      this.bank.active[other] = false;
      this.dampStartS = null;
    }
    if (pending && this.xf === 1 && !this.bank.active[other]) this.applyChord(this.harmony.take(realS), realS);

    // pan and presence slew toward the dome geometry (updated at 10 Hz)
    if (this.ticks % 10 === 1 || !this.panInit) this.geometry();
    const panK = this.panInit ? 1 - Math.exp(-TICK_S / 0.5) : 1;
    for (let v = 0; v < V; v++) {
      const x = this.pos[v], i0 = Math.floor(x), i1 = Math.min(N - 1, i0 + 1), f = x - i0;
      const pan = this.panNode[i0] + (this.panNode[i1] - this.panNode[i0]) * f;
      const pres = this.presNode[i0] + (this.presNode[i1] - this.presNode[i0]) * f;
      const th = ((pan + 1) * Math.PI) / 4;
      this.panL[v] += (Math.cos(th) - this.panL[v]) * panK;
      this.panR[v] += (Math.sin(th) - this.panR[v]) * panK;
      this.presence[v] += (pres - this.presence[v]) * panK;
    }
    this.panInit = true;

    // exciter
    const ts = realS - this.strikeT0;
    const strikeEnv = ts >= 0 ? this.strikeAmp * (1 - Math.exp(-ts / 0.08)) * Math.exp(-ts / 6) : 0;
    const bed = s.bed * (0.5 + m.slow.act);
    const riseK = 1 - Math.exp(-TICK_S / 0.3);
    const gainK = 1 - Math.exp(-TICK_S / s.gainSlewS);
    const t60 = [s.redT60, s.greenT60, s.violetT60];
    const radius = t60.map((t) => this.bank.radius(t));
    const norm = radius.map((r) => Math.sqrt(1 - r * r));
    const dampR = this.bank.radius(DAMP_T60);
    const level = [s.red, s.green, s.violet];
    const colour = [nd.red, nd.green, nd.violet];
    const w = [0, 0];
    w[this.cur] = Math.sin(0.5 * Math.PI * this.xf);
    w[other] = Math.cos(0.5 * Math.PI * this.xf);
    const strikeWidth = 12 * this.vpn;

    for (let v = 0; v < V; v++) {
      const x = this.pos[v], i0 = Math.floor(x), i1 = Math.min(N - 1, i0 + 1), f = x - i0;
      const b = nd.b[i0] + (nd.b[i1] - nd.b[i0]) * f, y = nd.y[i0] + (nd.y[i1] - nd.y[i0]) * f;
      this.rise[v] += (Math.max(0, (b - this.prevB[v]) / TICK_S) - this.rise[v]) * riseK;
      this.prevB[v] = b;
      const spatial = 0.5 + 0.5 * Math.exp(-(((v - this.strikeVoice) / strikeWidth) ** 2));
      const exc = bed + s.rise * this.rise[v];
      const strikeX = s.strike * strikeEnv * spatial;               // the strike lands on the newest chord
      const pl = this.panL[v], pr = this.panR[v];

      for (let l = 0; l < 3; l++) {
        const gi = l * V + v;
        const col = colour[l];
        // √colour: a storm is brighter and wider, not 25 dB louder (auto-gain does the rest)
        const target = level[l] * Math.sqrt(col[i0] + (col[i1] - col[i0]) * f) * this.presence[v];
        this.gain[gi] += (target - this.gain[gi]) * gainK;
        const g = this.gain[gi] * this.preGain;
        const cents = s.detuneCents * (y / 160) + s.driftCents * noise1(this.seed, 81 + l, realS / 20 + v * 3.7);
        for (let bk = 0; bk < 2; bk++) {
          if (!this.bank.active[bk]) continue;
          const k = (bk * 3 + l) * V + v;
          const damped = bk !== this.cur && this.dampStartS !== null;
          this.bank.inA[k] = (exc * w[bk] + (bk === this.cur ? strikeX : 0)) * norm[l];
          this.bank.tune(k, midiHz(this.notes[bk][l][v] + cents / 100), damped ? Math.min(dampR, radius[l]) : radius[l]);
          this.bank.ramp(k, g * pl, g * pr, tickLen);
        }
      }
    }
    if (this.ticks % 100 === 0) this.resets += this.bank.guard();
  }

  /** Dome azimuth → pan (east left, like the dome); elevation → presence. 10 Hz. */
  geometry() {
    const nd = this.model.node, N = this.model.p.nodes;
    for (let i = 0; i < N; i++) {
      const { az, el } = this.obs.azEl(this.obs.pointAt(nd.mlat[i], nd.mlon[i], ALT_KM));
      this.panNode[i] = clamp(-Math.sin((az * Math.PI) / 180) * this.s.width, -1, 1);
      this.presNode[i] = 0.45 + 0.55 * clamp(el / 50, 0, 1);
    }
  }

  /** Voice a chord into bank b: from scratch, or led from bank `from` (each voice keeps its slot). */
  voiceBank(b, chord, from) {
    const s = this.s;
    LAYERS.forEach((layer, l) => {
      const next = layerSlots(chord, chord.key, layer);
      const [lo, hi] = layerRange(layer, s.register, s.spread);
      const slots = from === null ? next : leadSlots(this.slotPcs[from][l], next).slots;
      for (let v = 0; v < this.V; v++) {
        const near = from === null ? lo + rand01(this.seed, 71 + l, v) * (hi - lo) : this.notes[from][l][v];
        this.notes[b][l][v] = nearestNote(slots[this.slotOf[v]], near, lo, hi);
      }
      this.slotPcs[b][l] = slots;
    });
  }

  applyChord({ chord, reason }, realS) {
    const nb = 1 - this.cur;
    this.voiceBank(nb, chord, this.cur);
    this.bank.clear(nb);
    this.bank.active[nb] = true;
    this.cur = nb;
    this.xf = 0; this.xfStartS = realS;
    this.applied = chord;
    this.push({ t: this.model.modelMs, type: 'chord', label: chord.label, roman: chord.roman, fn: chord.fn, reason });
  }

  push(e) {
    this.outbox.push(e);
    if (this.outbox.length > 500) this.outbox.splice(0, this.outbox.length - 500);
  }
}
