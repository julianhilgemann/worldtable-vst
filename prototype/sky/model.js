// Sky Model sketch (spec/04-sky-model.md). Pure: no DOM, no wall clock, no Math.random().
//
// Two clocks:
//   model time (UTC ms) — data, reservoir, substorm phases, assimilation, oval. Stepped on a fixed
//                         1 s grid, so the slow sky is identical at every Journey speed.
//   real time (ticks)   — curtain waves, brightness, pulsations, whistlers. 100 Hz ticks of audio
//                         time (ADR-0007), so motion looks natural at any speed.
// tick() advances real time by 10 ms and model time by 10 ms × speed.

import { rand01, randS, noise2, fbm2 } from './rng.js';
import { sample, sampleDH, sampleDHdt } from './data.js';
import { skyBodies } from './astro.js';
import { Observer } from './observer.js';

export const PHASE = { QUIET: 0, GROWTH: 1, ONSET: 2, RECOVERY: 3 };
export const PHASE_NAMES = ['quiet', 'growth', 'onset', 'recovery'];
export const TICK_S = 0.01;
const SLOW_MS = 1000;
const MIN_MS = 60000;

/** Tuning constants (spec/04-sky-model.md §7). Units in the names. */
export const DEFAULTS = {
  nodes: 64,
  spanDeg: 25,               // curtain covers station maglon ± this

  // slow — reservoir and phases (model time)
  eTauMin: 5,                // smoothing of the coupling E = v·Bs
  kpTauMin: 20,
  loadPerHour: 0.8,          // reservoir loading per (mV/m) per hour
  baseLoadPerHour: 0.2,      // quiet-time loading
  leakHours: 2.0,
  threshold: 2.5,            // reservoir level that triggers an onset
  thresholdJitter: 0.3,      // ± fraction, seeded per substorm cycle
  maxRelease: 3.0,           // one onset releases at most this many thresholds
  releaseFraction: 0.7,
  releaseTauMin: 10,
  expansionMin: [14, 26],
  recoveryMin: [50, 100],
  refractoryMin: 90,         // substorms recur every 1.5 h at most (awe needs space)
  growthE: 0.5,              // mV/m: above this the reservoir counts as growing
  forcedBayNt: -80,          // H change over 5 min that forces an onset (nightside) …
  forcedDeviationNt: -150,   // … while H is this far below its quiet level
  forcedRefractoryMin: 60,
  pseudoPerHour: 0.25,       // soft pseudo-breakups when nothing happened for a while
  pseudoAfterMin: 150,
  pseudoStrength: 0.4,
  gainTauMin: 10,            // assimilation time constant
  gainDeadZoneNt: 60,        // |ΔH| below this is quiet-time variation, not disturbance
  gainRange: [0.4, 2.5],
  dhScaleNt: 700,            // model ΔH per unit of disturbance
  activityTauMin: 10,

  // oval (geomagnetic latitude at magnetic midnight)
  eqBase: 66.5, eqPerKp: 2.0,       // equatorward edge
  poleBase: 75.0, polePerKp: 0.45,  // poleward edge
  dayShiftDeg: 7,                    // oval sits this much further poleward at noon
  arcPosition: 0.5,                  // 0 equatorward edge … 1 poleward edge
  growthDriftDeg: 1.5,
  expansionPoleDeg: 2.5,

  // fast — curtain (real time)
  waveC: 2.2,                // nodes per second
  damping: 0.35,             // 1/s
  stiffness: 0.03,           // 1/s²
  forceKm: 900,              // km/s² forcing at activity 1
  surgeKm: 4000,
  displacementLimitKm: 160,
  riseTauS: 0.5,
  fallTauS: 3.0,
  pulsePeriodS: [3, 14],
  pcPeriodS: 40,
  whistlerPerS: 0.08,        // at activity 1
};

/**
 * Slow (model-time) state. A flat object of numbers, so a spread copy checkpoints it
 * (structuredClone doesn't exist in AudioWorklet scope, where T1.3 runs the model).
 */
function initialSlow(track, p, startMs, seed) {
  return {
    t: startMs,
    eSm: 0, kpSm: sample(track, 'kp', startMs), act: 0.1, gain: 1, w: 0.4 * p.threshold,
    phase: PHASE.QUIET, phaseStart: startMs, phaseDur: 0, lastOnset: -Infinity,
    cycle: 0, thr: p.threshold, w0: 0, u: 0, ostr: 1, forced: false, onsetNode: p.nodes >> 1,
    eqLat: p.eqBase - p.eqPerKp * sample(track, 'kp', startMs), poleLat: p.poleBase,
    dhModel: 0, dhMeasSm: 0, mlt0: 0, onsetCount: 0, seed,
    e: 0, v: 400, bz: 0, n: 5, kp: 2, dh: 0, dhdt: 0, night: 1, sunEl: 0,
  };
}

const clamp = (x, a, b) => (x < a ? a : x > b ? b : x);
const lerp = (a, b, f) => a + (b - a) * f;
const relax = (x, target, dt, tau) => x + (target - x) * (1 - Math.exp(-dt / tau));
const nightFactor = (mlt) => 0.25 + 0.75 * (0.5 + 0.5 * Math.cos((2 * Math.PI * mlt) / 24));

/** One slow step of 1 s model time. Mutates s. */
function slowStep(s, track, p, obs, events) {
  const dt = SLOW_MS / 1000;
  const t = s.t;
  const valid = (name) => track.valid[name][clamp(Math.floor((t - track.startMs) / MIN_MS), 0, track.length - 1)];

  // data
  s.v = sample(track, 'v', t); s.bz = sample(track, 'bz', t); s.n = sample(track, 'n', t);
  s.kp = sample(track, 'kp', t);
  s.dh = sampleDH(track, t); s.dhdt = sampleDHdt(track, t, 5);
  s.e = (s.v * Math.max(0, -s.bz)) / 1000;                       // mV/m
  s.eSm = relax(s.eSm, s.e, dt, p.eTauMin * 60);
  s.kpSm = relax(s.kpSm, s.kp, dt, p.kpTauMin * 60);

  // sun → magnetic local time at the station
  const bodies = skyBodies(t, obs.station.lat, obs.station.lon);
  s.mlt0 = obs.mlt(obs.mlon, bodies.subsolarLat, bodies.subsolarLon);
  s.night = nightFactor(s.mlt0);
  s.sunEl = bodies.sunEl;

  // reservoir: load from coupling, leak, release
  const sinceOnsetMin = (t - s.lastOnset) / MIN_MS;
  const releaseRate = s.phase === PHASE.ONSET || s.phase === PHASE.RECOVERY
    ? (p.releaseFraction * s.w0 / (p.releaseTauMin * 60)) * Math.exp(-sinceOnsetMin / p.releaseTauMin) : 0;
  const quietLoad = p.baseLoadPerHour * (0.6 + 0.4 * (rand01(s.seed, 3, Math.floor(t / 600000)) * 2));
  s.w += ((p.loadPerHour * s.eSm + quietLoad) / 3600 - s.w / (p.leakHours * 3600) - releaseRate) * dt;
  s.w = Math.max(0, s.w);
  s.u = clamp((s.ostr * releaseRate * p.releaseTauMin * 60) / p.threshold, 0, 2.5);

  // phases
  const nightside = s.mlt0 >= 18 || s.mlt0 <= 6;
  const forcedOk = s.dhdt <= p.forcedBayNt && s.dh <= p.forcedDeviationNt && nightside && valid('h')
    && s.phase !== PHASE.ONSET && sinceOnsetMin >= p.forcedRefractoryMin;
  const spontaneousOk = s.w >= s.thr && sinceOnsetMin >= p.refractoryMin && s.phase !== PHASE.ONSET;
  // pseudo-breakup: evaluated once per model minute, seeded by the minute index
  const minuteIndex = Math.floor(t / MIN_MS);
  const pseudoOk = t % MIN_MS === 0 && sinceOnsetMin >= p.pseudoAfterMin && s.phase !== PHASE.ONSET
    && rand01(s.seed, 15, minuteIndex) < p.pseudoPerHour / 60;
  const elapsed = t - s.phaseStart;
  const enter = (phase, durMs) => {
    s.phase = phase; s.phaseStart = t; s.phaseDur = durMs;
    events.push({ t, type: 'phase', phase });
  };

  if (forcedOk || spontaneousOk || pseudoOk) {
    const kind = forcedOk ? 'forced' : spontaneousOk ? 'loaded' : 'pseudo';
    s.cycle += 1;
    s.onsetCount += 1;
    s.forced = forcedOk;
    const strength = forcedOk ? clamp(-s.dhdt / 200, 0.6, 2.5) : kind === 'pseudo' ? p.pseudoStrength : 1;
    s.w0 = Math.min(Math.max(s.w, p.threshold * strength * (kind === 'loaded' ? 0 : 0.8)), p.maxRelease * p.threshold);
    s.w = Math.max(s.w, s.w0);
    // weakly driven onsets are softer than storm-driven ones
    s.ostr = kind === 'forced' ? 1 : kind === 'pseudo' ? p.pseudoStrength : clamp(0.35 + s.eSm / 2, 0.35, 1.2);
    s.lastOnset = t;
    s.thr = p.threshold * (1 + p.thresholdJitter * randS(s.seed, 7, s.cycle));
    // onset near 23 MLT, jittered, clamped to the visible chain
    const nodeOf23 = (p.nodes - 1) / 2 + (((23 - s.mlt0 + 36) % 24) - 12) * 15 / (2 * p.spanDeg / (p.nodes - 1));
    s.onsetNode = clamp(Math.round(nodeOf23 + 6 * randS(s.seed, 9, s.cycle)), 4, p.nodes - 5);
    enter(PHASE.ONSET, lerp(p.expansionMin[0], p.expansionMin[1], rand01(s.seed, 11, s.cycle)) * MIN_MS);
    events.push({ t, type: 'onset', kind, forced: forcedOk, dhdt: s.dhdt, strength: s.ostr * s.w0 / p.threshold });
  } else if (s.phase === PHASE.ONSET && elapsed >= s.phaseDur) {
    enter(PHASE.RECOVERY, lerp(p.recoveryMin[0], p.recoveryMin[1], rand01(s.seed, 13, s.cycle)) * MIN_MS);
  } else if (s.phase === PHASE.RECOVERY && elapsed >= s.phaseDur) {
    enter(s.eSm > p.growthE ? PHASE.GROWTH : PHASE.QUIET, 0);
  } else if (s.phase === PHASE.QUIET && s.eSm > p.growthE) {
    enter(PHASE.GROWTH, 0);
  } else if (s.phase === PHASE.GROWTH && s.eSm < 0.5 * p.growthE) {
    enter(PHASE.QUIET, 0);
  }

  // activity and oval
  const actTarget = clamp(0.06 + 0.55 * (s.kpSm / 9) + 0.3 * Math.tanh(s.eSm / 8), 0, 1);
  s.act = relax(s.act, actTarget, dt, p.activityTauMin * 60);
  const growth = s.phase === PHASE.GROWTH ? clamp(s.w / s.thr, 0, 1) : 0;
  const eqTarget = p.eqBase - p.eqPerKp * s.kpSm - p.growthDriftDeg * growth + p.expansionPoleDeg * Math.min(1, s.u);
  s.eqLat = relax(s.eqLat, eqTarget, dt, 6 * 60);
  s.poleLat = relax(s.poleLat, p.poleBase - p.polePerKp * s.kpSm + p.expansionPoleDeg * Math.min(1, s.u), dt, 6 * 60);

  // assimilation: pull the disturbance gain so |ΔH model| tracks |ΔH measured|
  const arcLat = lerp(s.eqLat, s.poleLat, p.arcPosition);
  const prox = Math.exp(-(((obs.mlat - arcLat) / 5) ** 2));
  s.dhModel = -p.dhScaleNt * s.gain * (0.25 * s.act + s.u) * prox * s.night;
  s.dhMeasSm = relax(s.dhMeasSm, Math.abs(s.dh), dt, 5 * 60);
  if (valid('h')) {
    const dz = p.gainDeadZoneNt;
    const err = Math.log(Math.max(s.dhMeasSm, dz)) - Math.log(Math.max(Math.abs(s.dhModel), dz));
    // inside the dead zone the gain drifts back to 1 over ~1 h
    const pull = err !== 0 ? err / (p.gainTauMin * 60) : -Math.log(s.gain) / 3600;
    s.gain = clamp(s.gain * Math.exp(pull * dt), p.gainRange[0], p.gainRange[1]);
  }
  s.t = t + SLOW_MS;
}

export class SkyModel {
  /**
   * @param track   prepared track (data.js)
   * @param options { seed, params, startMs, speed, observer }
   */
  constructor(track, { seed = 1, params = {}, startMs, speed = 24, observer } = {}) {
    this.track = track;
    this.seed = seed >>> 0;
    this.p = { ...DEFAULTS, ...params };
    this.obs = observer || new Observer();
    this.speed = speed;
    this.events = [];
    const n = this.p.nodes;
    this.node = {
      mlon: new Float64Array(n), y: new Float64Array(n), vy: new Float64Array(n), b: new Float64Array(n),
      red: new Float64Array(n), green: new Float64Array(n), violet: new Float64Array(n),
      mlat: new Float64Array(n), pulse: new Float64Array(n),
    };
    for (let i = 0; i < n; i++) this.node.mlon[i] = this.obs.mlon + lerp(-this.p.spanDeg, this.p.spanDeg, i / (n - 1));
    for (let i = 0; i < n; i++) this.node.b[i] = 0.05;
    // pulsating patches of ~6 nodes: seeded period and phase, constant per seed
    const patches = Math.ceil(n / 6);
    this.pulsePeriod = new Float64Array(patches);
    this.pulsePhase = new Float64Array(patches);
    for (let k = 0; k < patches; k++) {
      this.pulsePeriod[k] = lerp(this.p.pulsePeriodS[0], this.p.pulsePeriodS[1], rand01(this.seed, 41, k));
      this.pulsePhase[k] = 2 * Math.PI * rand01(this.seed, 43, k);
    }
    this.tick = 0;
    this.whistlers = 0;
    this.lastWhistler = null;
    this.reset(startMs ?? track.startMs);
  }

  /** Restart the slow sky at a model time (fast state keeps moving, so nothing jumps). */
  reset(startMs) {
    this.slow = initialSlow(this.track, this.p, this.track.startMs, this.seed);
    this.checkpoints = [];
    this.seek(startMs);
  }

  /** Move model time; slow state is replayed deterministically from the nearest checkpoint. */
  seek(ms) {
    const target = clamp(Math.floor(ms / SLOW_MS) * SLOW_MS, this.track.startMs, this.track.endMs - SLOW_MS);
    let cp = null;
    for (const c of this.checkpoints) if (c.t <= target && (!cp || c.t > cp.t)) cp = c;
    if (cp && (cp.t > this.slow.t || this.slow.t > target)) this.slow = { ...cp };
    if (this.slow.t > target) this.slow = initialSlow(this.track, this.p, this.track.startMs, this.seed);
    const sink = [];
    while (this.slow.t <= target) this.stepSlow(sink);
    this.modelMs = target;
  }

  stepSlow(events) {
    if (this.slow.t % (10 * MIN_MS) === 0 && !this.checkpoints.some((c) => c.t === this.slow.t)) {
      this.checkpoints.push({ ...this.slow });
    }
    slowStep(this.slow, this.track, this.p, this.obs, events);
  }

  setSpeed(speed) { this.speed = speed; }

  /** Advance 10 ms of real time. */
  step() {
    const p = this.p, s = this.slow, nd = this.node, n = p.nodes;
    this.tick += 1;
    const tr = this.tick * TICK_S;

    // model time
    if (this.speed > 0 && this.modelMs < this.track.endMs - SLOW_MS) {
      this.modelMs += TICK_S * 1000 * this.speed;
      while (s.t <= this.modelMs && s.t < this.track.endMs) this.stepSlow(this.events);
    }

    // curtain: damped, driven wave equation along the chain (real time)
    const sinceOnsetMin = (this.modelMs - s.lastOnset) / MIN_MS;
    const inExpansion = s.phase === PHASE.ONSET;
    const surgeCenter = s.onsetNode - 1.2 * Math.max(0, sinceOnsetMin);        // westward travelling surge
    const front = 3 + 40 * (1 - Math.exp(-Math.max(0, sinceOnsetMin) / 5));    // expansion width (nodes)
    const drive = p.forceKm * (0.15 + s.act) * Math.sqrt(s.gain);
    const c2 = p.waveC * p.waveC;
    const recoveryPulse = s.phase === PHASE.RECOVERY ? Math.sin(Math.PI * clamp((this.modelMs - s.phaseStart) / Math.max(1, s.phaseDur), 0, 1)) : 0;
    const pc = 1 + 0.08 * s.act * Math.sin((2 * Math.PI * tr) / p.pcPeriodS);
    const stormRed = clamp((s.kpSm - 5) / 4, 0, 1);
    const quietRed = s.phase === PHASE.QUIET || s.phase === PHASE.RECOVERY ? 1 : 0.4;

    for (let i = 0; i < n; i++) {
      const yl = nd.y[i > 0 ? i - 1 : 0], yr = nd.y[i < n - 1 ? i + 1 : n - 1];
      let f = drive * fbm2(this.seed, 21, i / 7, tr / 3);
      if (inExpansion) f += p.surgeKm * s.u * Math.exp(-(((i - surgeCenter) / 3) ** 2)) * Math.sin(tr * 1.7 + i * 0.4);
      const acc = c2 * (yl - 2 * nd.y[i] + yr) - p.damping * nd.vy[i] - p.stiffness * nd.y[i] + f;
      nd.vy[i] += acc * TICK_S;
      nd.y[i] += nd.vy[i] * TICK_S;
      if (nd.y[i] > p.displacementLimitKm) { nd.y[i] = p.displacementLimitKm; nd.vy[i] *= -0.3; }
      if (nd.y[i] < -p.displacementLimitKm) { nd.y[i] = -p.displacementLimitKm; nd.vy[i] *= -0.3; }

      const mltI = s.mlt0 + (nd.mlon[i] - this.obs.mlon) / 15;
      const night = nightFactor(mltI);
      const patch = 0.5 + 0.5 * fbm2(this.seed, 31, i / 10, tr / 25);
      const envelope = inExpansion || s.u > 0.02 ? Math.exp(-(((i - s.onsetNode) / front) ** 2)) : 0;
      // pulsating aurora in recovery: patches of ~6 nodes, each with its own seeded period
      const k = (i / 6) | 0;
      const pulse = 0.5 + 0.5 * Math.sin((2 * Math.PI * tr) / this.pulsePeriod[k] + this.pulsePhase[k]);
      nd.pulse[i] = pulse;
      const pulseGain = 1 + 0.8 * recoveryPulse * (pulse - 0.5) * (patch > 0.45 ? 1 : 0.3);
      const motion = 1 + Math.min(1.5, Math.abs(nd.vy[i]) / 120);
      const target = night * s.gain * (0.09 + 0.45 * s.act * (0.55 + 0.45 * patch) + 1.1 * s.u * envelope)
        * motion * pulseGain * pc;
      nd.b[i] = relax(nd.b[i], target, TICK_S, target > nd.b[i] ? p.riseTauS : p.fallTauS);

      nd.green[i] = clamp(nd.b[i], 0, 2.5);
      nd.red[i] = clamp(nd.b[i] * (0.15 + 0.4 * quietRed) + 0.7 * stormRed * night, 0, 2.5);
      nd.violet[i] = clamp(nd.b[i] * (0.1 + 0.9 * s.u * envelope), 0, 2.5);
      const arc = lerp(s.eqLat, s.poleLat, p.arcPosition) + (p.dayShiftDeg * (1 - Math.cos((2 * Math.PI * mltI) / 24))) / 2;
      nd.mlat[i] = arc + nd.y[i] / 111.2;
    }

    // whistlers / chorus: seeded Poisson events, rate grows with activity
    if (rand01(this.seed, 51, this.tick) < p.whistlerPerS * s.act * s.act * TICK_S * (1 + 2 * s.u)) {
      this.whistlers += 1;
      this.lastWhistler = {
        tick: this.tick, falling: rand01(this.seed, 53, this.whistlers) < 0.8,
        strength: 0.3 + 0.7 * rand01(this.seed, 55, this.whistlers),
      };
      this.events.push({ t: this.modelMs, type: 'whistler', ...this.lastWhistler });
    }
  }

  /** Drain events since the last call (phase changes, onsets, whistlers). */
  takeEvents() { const e = this.events; this.events = []; return e; }

  /** SkySnapshot (spec/03-architecture.md §B), plus per-node geomagnetic position for the renderer. */
  snapshot() {
    const s = this.slow, p = this.p, nd = this.node;
    const progress = s.phase === PHASE.GROWTH ? clamp(s.w / s.thr, 0, 1)
      : s.phase === PHASE.QUIET ? 0 : clamp((this.modelMs - s.phaseStart) / Math.max(1, s.phaseDur), 0, 1);
    return {
      modelTimeUtc: this.modelMs, step: this.tick,
      phase: s.phase, phaseProgress: progress,
      energy: s.w / p.threshold, activity: s.act, gain: s.gain, unloading: s.u,
      kp: s.kp, bz: s.bz, speed: s.v, density: s.n, coupling: s.e,
      dh: s.dh, dhModel: s.dhModel, mlt: s.mlt0,
      ovalLatitude: lerp(s.eqLat, s.poleLat, p.arcPosition), eqLat: s.eqLat, poleLat: s.poleLat,
      dayShiftDeg: p.dayShiftDeg, stormRed: clamp((s.kpSm - 5) / 4, 0, 1),
      pulsation: [(this.tick * TICK_S) / p.pcPeriodS % 1, nd.pulse[0]],
      onsetCount: s.onsetCount, whistlerCount: this.whistlers, lastWhistler: this.lastWhistler,
      nodeCount: p.nodes, nodes: nd,
      sky: skyBodies(this.modelMs, this.obs.station.lat, this.obs.station.lon),
    };
  }
}

/**
 * Slow sky only, over a span: 1-minute traces for charts and goldens. Identical to what a
 * SkyModel sees at any speed, because the slow sky runs on its own 1 s model-time grid.
 */
export function runSlow(track, { seed = 1, params = {}, observer } = {}) {
  const p = { ...DEFAULTS, ...params };
  const obs = observer || new Observer();
  const s = initialSlow(track, p, track.startMs, seed >>> 0);
  const minutes = track.length;
  const out = {
    t: new Float64Array(minutes), phase: new Uint8Array(minutes), energy: new Float32Array(minutes),
    activity: new Float32Array(minutes), gain: new Float32Array(minutes), unloading: new Float32Array(minutes),
    coupling: new Float32Array(minutes), dh: new Float32Array(minutes), dhModel: new Float32Array(minutes),
    ovalLatitude: new Float32Array(minutes), threshold: new Float32Array(minutes), events: [],
    // T6.10: the keogram's inputs (the oval as the dome draws it)
    eqLat: new Float32Array(minutes), poleLat: new Float32Array(minutes), stormRed: new Float32Array(minutes),
    mlt: new Float32Array(minutes), kp: new Float32Array(minutes),
  };
  for (let m = 0; m < minutes; m++) {
    for (let k = 0; k < 60; k++) slowStep(s, track, p, obs, out.events);
    out.t[m] = s.t - SLOW_MS;
    out.phase[m] = s.phase; out.energy[m] = s.w / p.threshold; out.threshold[m] = s.thr / p.threshold;
    out.activity[m] = s.act; out.gain[m] = s.gain; out.unloading[m] = s.u; out.coupling[m] = s.eSm;
    out.dh[m] = s.dh; out.dhModel[m] = s.dhModel; out.ovalLatitude[m] = lerp(s.eqLat, s.poleLat, p.arcPosition);
    out.eqLat[m] = s.eqLat; out.poleLat[m] = s.poleLat; out.stormRed[m] = clamp((s.kpSm - 5) / 4, 0, 1);
    out.mlt[m] = s.mlt0; out.kp[m] = s.kp;
  }
  return out;
}
