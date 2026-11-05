// Sky Model sketch — wiring: data → model (100 Hz ticks) → dome + chart + readouts.
// URL hash keeps the review state: #day=2024-05-10&t=2024-05-11T08:00&speed=24&seed=1
// Sound on (T1.3): the model runs in the AudioWorklet, clocked by audio (ADR-0007), and the page
// draws its posted snapshots; sound off: the page steps its own model, as in T1.2.

import { DAYS, loadDay } from './data.js';
import { SkyModel, runSlow, DEFAULTS, PHASE_NAMES, TICK_S } from './model.js';
import { Observer } from './observer.js';
import { DomeRenderer } from './dome.js';
import { StripChart } from './charts.js';
import { SoundPanel } from './sound/panel.js';
import { SOUND_DEFAULTS } from './sound/engine.js';

const SPEEDS = [1, 6, 24, 96, 360, 1440];
const START = { '2024-05-10': '2024-05-10T17:00', '2025-01-19': '2025-01-19T09:30', '2025-01-25': '2025-01-25T08:00' };
const TUNING = [
  ['loadPerHour', 0.1, 3, 0.05], ['leakHours', 0.5, 6, 0.1], ['threshold', 1, 6, 0.1],
  ['refractoryMin', 20, 240, 5], ['forcedBayNt', -300, -20, 5], ['pseudoPerHour', 0, 1, 0.05],
  ['eqBase', 60, 72, 0.1], ['eqPerKp', 0.5, 3, 0.05], ['arcPosition', 0, 1, 0.01],
  ['waveC', 0.3, 6, 0.1], ['damping', 0.05, 1.5, 0.01], ['forceKm', 50, 3000, 10], ['surgeKm', 0, 10000, 100],
  ['riseTauS', 0.1, 3, 0.05], ['fallTauS', 0.3, 10, 0.1], ['whistlerPerS', 0, 0.5, 0.01],
];

const $ = (id) => document.getElementById(id);
const hash = new URLSearchParams(location.hash.slice(1));
const state = {
  day: hash.get('day') || DAYS[0].id,
  speed: Number(hash.get('speed')) || 24,
  seed: Number(hash.get('seed')) || 1,
  playing: true,
  params: Object.fromEntries([...hash].filter(([k]) => k.startsWith('p.') && k.slice(2) in DEFAULTS)
    .map(([k, v]) => [k.slice(2), Number(v)])),
  sound: Object.fromEntries([...hash].filter(([k]) => k.startsWith('s.') && k.slice(2) in SOUND_DEFAULTS)
    .map(([k, v]) => [k.slice(2), Number.isNaN(Number(v)) ? v : Number(v)])),
  exposure: 1.6,
};
const observer = new Observer();
const dome = new DomeRenderer($('dome'), $('overlay'), observer, state.seed);
const chart = new StripChart($('chart'), (ms) => seekTo(ms));
let track, model, trace;

const sound = new SoundPanel({
  $, state,
  getStart: () => ({ track, startMs: model.modelMs, fast: { node: model.node, tick: model.tick } }),
  currentMs: () => currentMs(),
  onStop: (snap) => {                                // the page's model continues where the worklet's was
    model.seek(snap.modelTimeUtc);
    for (const key of Object.keys(model.node)) if (snap.nodes[key]) model.node[key].set(snap.nodes[key]);
    model.tick = snap.step;
  },
  writeHash: () => writeHash(),
});
const audioSnap = () => sound.audio?.snap ?? null;
const currentMs = () => audioSnap()?.modelTimeUtc ?? model.modelMs;
function seekTo(ms) {
  model.seek(ms);
  sound.audio?.post({ type: 'seek', ms });
  writeHash();
}

// ------------------------------------------------------------------ controls
for (const d of DAYS) $('day').append(new Option(d.label, d.id, false, d.id === state.day));
$('day').addEventListener('change', async (e) => {
  const wasOn = Boolean(sound.audio);
  if (wasOn) await sound.stop();                     // the worklet holds the old day's track
  state.day = e.target.value;
  await loadAndStart(START[state.day]);
  if (wasOn) await sound.start();
});

for (const s of SPEEDS) {
  const b = document.createElement('button');
  b.type = 'button'; b.textContent = `${s}×`; b.dataset.speed = s;
  b.addEventListener('click', () => {
    state.speed = s; model.setSpeed(s); sound.audio?.post({ type: 'speed', speed: s }); markSpeed(); writeHash();
  });
  document.querySelector('.speeds').append(b);
}
const markSpeed = () => document.querySelectorAll('.speeds button')
  .forEach((b) => b.setAttribute('aria-pressed', String(Number(b.dataset.speed) === state.speed)));

$('play').addEventListener('click', () => {
  state.playing = !state.playing;
  $('play').textContent = state.playing ? '❚❚' : '▶';
  $('play').setAttribute('aria-label', state.playing ? 'Pause' : 'Play');
  sound.audio?.setPlaying(state.playing);
});
$('seed').value = state.seed;
$('seed').addEventListener('change', (e) => { state.seed = Math.max(1, Number(e.target.value) | 0); rebuild(); });
$('next-onset').addEventListener('click', () => {
  const next = trace.events.find((e) => e.type === 'onset' && e.t > currentMs() + 60000);
  if (next) seekTo(next.t - 4 * 60000);
});
$('t-stars').addEventListener('change', (e) => { dome.options.stars = dome.options.moon = e.target.checked; });
$('t-day').addEventListener('change', (e) => { dome.options.dayNight = e.target.checked; });
$('t-grad').addEventListener('change', (e) => { dome.options.gradientsOnly = e.target.checked; });

// tuning sliders
const tuning = $('tuning');
const allTuning = [...TUNING, ['exposure', 0.4, 4, 0.05]];
for (const [key, min, max, step] of allTuning) {
  const wrap = document.createElement('label');
  wrap.className = 'slider';
  const value = key === 'exposure' ? state.exposure : (state.params[key] ?? DEFAULTS[key]);
  wrap.innerHTML = `<span>${key}</span><output>${value}</output><input type="range" min="${min}" max="${max}" step="${step}" value="${value}">`;
  const input = wrap.querySelector('input'), out = wrap.querySelector('output');
  input.addEventListener('input', () => {
    const v = Number(input.value);
    out.textContent = v;
    if (key === 'exposure') { state.exposure = dome.options.exposure = v; return; }
    state.params[key] = v;
    scheduleRebuild();
  });
  tuning.append(wrap);
}
$('copy').addEventListener('click', async () => {
  const text = JSON.stringify(state.params, null, 2);
  $('constants').textContent = text;
  try { await navigator.clipboard.writeText(text); } catch { /* shown below instead */ }
});
$('defaults').addEventListener('click', () => {
  state.params = {};
  tuning.querySelectorAll('.slider').forEach((el, i) => {
    const [key] = allTuning[i];
    const v = key === 'exposure' ? 1.6 : DEFAULTS[key];
    el.querySelector('input').value = v; el.querySelector('output').textContent = v;
  });
  state.exposure = dome.options.exposure = 1.6;
  rebuild();
});

let rebuildTimer = 0;
function scheduleRebuild() { clearTimeout(rebuildTimer); rebuildTimer = setTimeout(rebuild, 250); }

// ------------------------------------------------------------------ model lifecycle
async function loadAndStart(startIso) {
  track = await loadDay(state.day);
  const start = startIso ? Date.parse(startIso + 'Z') : track.startMs;
  rebuild(start);
}

function rebuild(atMs) {
  const at = atMs ?? (model ? currentMs() : track.startMs);
  const old = model;
  model = new SkyModel(track, { seed: state.seed, params: state.params, startMs: at, speed: state.speed, observer });
  if (old) { model.node = old.node; model.tick = old.tick; }       // keep the curtain moving: nothing jumps
  sound.audio?.post({ type: 'sky', seed: state.seed, params: state.params });
  dome.seed = state.seed;
  trace = runSlow(track, { seed: state.seed, params: state.params, observer });
  chart.setData(track, trace);
  $('ticker').innerHTML = '';
  pushEvents(trace.events.filter((e) => e.t <= at && !(e.type === 'phase' && e.phase === 2)).slice(-6), 0);
  markSpeed();
  writeHash();
}

function writeHash() {
  if (!model) return;
  const t = new Date(currentMs()).toISOString().slice(0, 16);
  const params = Object.entries(state.params).map(([k, v]) => `&p.${k}=${v}`).join('');
  const snd = Object.entries(state.sound).map(([k, v]) => `&s.${k}=${encodeURIComponent(v)}`).join('');
  history.replaceState(null, '', `#day=${state.day}&t=${t}&speed=${state.speed}&seed=${state.seed}${params}${snd}`);
}

// ------------------------------------------------------------------ readouts and ticker
const fmt = (x, d = 1) => (Number.isFinite(x) ? x.toFixed(d) : '-');
const hhmm = (ms) => new Date(ms).toISOString().slice(11, 16);
let lastWhistlerShown = -Infinity;

function updateReadouts(s) {
  $('r-ut').textContent = `${new Date(s.modelTimeUtc).toISOString().slice(0, 16).replace('T', ' ')} UT`;
  $('r-kp').textContent = fmt(s.kp);
  $('r-bz').textContent = `${fmt(s.bz)} nT`;
  $('r-v').textContent = `${fmt(s.speed, 0)} km/s`;
  $('r-mode').textContent = state.playing ? `JOURNEY ${state.speed}×` : 'PAUSED';
  $('r-phase').textContent = PHASE_NAMES[s.phase];
  $('r-progress').style.width = `${(100 * s.phaseProgress).toFixed(1)}%`;
  $('r-energy').textContent = fmt(s.energy, 2);
  $('r-activity').textContent = fmt(s.activity, 2);
  $('r-gain').textContent = fmt(s.gain, 2);
  $('r-e').textContent = `${fmt(s.coupling, 2)} mV/m`;
  $('r-dh').textContent = `${fmt(s.dh, 0)} nT`;
  $('r-oval').textContent = `${fmt(s.ovalLatitude)}°`;
  $('r-mlt').textContent = `${fmt(s.mlt)} h`;
  $('r-sun').textContent = `${fmt(s.sky.sunEl, 0)}° / ${(100 * s.sky.moonIllum).toFixed(0)}% ${fmt(s.sky.moonEl, 0)}°`;
}

function pushEvents(events, realS) {
  const list = $('ticker');
  for (const e of events) {
    let text = null, cls = '';
    if (e.type === 'onset') {
      cls = e.kind === 'forced' ? 'onset forced' : 'onset';
      text = e.kind === 'forced' ? `substorm onset · data-forced (ΔH ${fmt(e.dhdt, 0)} nT / 5 min)`
        : e.kind === 'pseudo' ? 'pseudo-breakup' : `substorm onset · energy ${fmt(e.strength, 1)}`;
    } else if (e.type === 'phase' && e.phase !== 2) {
      text = `${PHASE_NAMES[e.phase]} phase`;
    } else if (e.type === 'chord') {
      cls = 'chord';
      text = `chord → ${e.label} (${e.roman}) · ${e.reason}`;
    } else if (e.type === 'whistler' && realS - lastWhistlerShown > 20) {
      lastWhistlerShown = realS;
      text = `whistler (${e.falling ? 'falling' : 'rising'})`;
    }
    if (!text) continue;
    const li = document.createElement('li');
    li.className = cls;
    li.textContent = `${hhmm(e.t)} UT  ${text}`;
    list.prepend(li);
    while (list.children.length > 40) list.lastChild.remove();
  }
}

// ------------------------------------------------------------------ loop
let last = performance.now(), acc = 0, readoutClock = 0, costMs = 0;
function frame(now) {
  const dt = Math.min(0.1, (now - last) / 1000);
  last = now;
  const work0 = performance.now();
  if (model) {
    let snap, events;
    if (sound.audio) {                                // the audio thread steps the sky; we only draw it
      snap = audioSnap();
      events = sound.audio.takeEvents();
      sound.noteEvents(events);
    } else {
      if (state.playing) {
        acc += dt;
        let n = 0;
        while (acc >= TICK_S && n < 40) { model.step(); acc -= TICK_S; n++; }
        if (n === 40) acc = 0;
      }
      snap = model.snapshot();
      events = model.takeEvents();
    }
    if (!snap) sound.readout();                       // says why the worklet is silent
    if (snap) {
      dome.render(snap, snap.step * TICK_S);
      readoutClock += dt;
      if (readoutClock > 0.1) {
        readoutClock = 0; updateReadouts(snap); chart.draw(snap.modelTimeUtc); sound.readout();
        $('r-perf').textContent = `${costMs.toFixed(1)} ms/frame (JS)`;
      }
      pushEvents(events, snap.step * TICK_S);
    }
    costMs += (performance.now() - work0 - costMs) * 0.05;
  }
  requestAnimationFrame(frame);
}

window.addEventListener('resize', () => { if (trace) chart.drawBase(); });
window.sky = { get model() { return model; }, dome, state, sound };   // console access for tuning
await loadAndStart(hash.get('t') || START[state.day]);
requestAnimationFrame(frame);
// Checks without a click (headless Chrome with --autoplay-policy=no-user-gesture-required).
if (hash.get('autosound') === '1') sound.start();
