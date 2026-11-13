/* The modulation grid's state (T4.2), shared by the rings, the motion panels and the grid overlay.
 *
 *   UI.mod = {
 *     info,                    the last `modulation` event: { sources: [{key, label, group, lo, hi}],
 *                              destinations: [{key, label, module, law, params}], targets: [param id per
 *                              target, "" for Strike], depths: [[a row per source, a depth per destination]] }
 *     tel,                     the last telemetry `mod`: { src, val, lo, hi (per target), lfo: [[phase, value]],
 *                              env: [stage, progress, value] }
 *     set(src, dst, depth),    a route by key (0 clears it; depths in steps of 0.01), through the native `modSet`
 *     routesOf(paramId),       the routes onto that parameter (or a destination key): [{ source, destination, depth }]
 *     movedBy(sourceKey),      that source's routes: [{ source, destination, depth }]
 *     on(fn),                  fn() now (when known), on every `modulation` payload and every telemetry
 *                              `mod`; returns an unsubscribe
 *     destName(key),           a destination as a sentence names it: "Delay mix", "Width", "Red level", "EQ cut"
 *     amountText(dst, depth),  a route's depth in the destination's own terms: "+50 %", "+6.0 dB", "+0.9 oct"
 *   }
 *
 * The plug-in sends `modulation` on page load and whenever the depths change (also the answer of the
 * native `modGet`). Outside the plug-in (the browser mock) this file is that engine as well: the
 * default grid, the two LFOs, the envelope and the `mod` telemetry, with the owner's laws
 * (2026-11-13): `mul` target = base · Π(1 + depth·source), the band levels up to 2 (+6 dB) though
 * their knobs stop at 1; `add` base + Σ depth·source · half the range (EQ gains, drives); `oct`
 * base · 2^(3·Σ) (the EQ's cut). Review only: #routes=lfo1>width:0.3,env>rev_shimmer:0.9 adds routes. */
(function () {
  "use strict";
  const WT = window.WT, UI = window.UI;
  const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
  const fns = new Set();

  const sourceIndex = (key) => (mod.info ? mod.info.sources.findIndex((s) => s.key === key) : -1);
  const destinationIndex = (key) => (mod.info ? mod.info.destinations.findIndex((d) => d.key === key) : -1);
  const notify = () => { for (const fn of fns) fn(); };
  function setInfo(p) {
    if (!p || !Array.isArray(p.sources) || !Array.isArray(p.destinations) || !Array.isArray(p.depths)) return;
    mod.info = p;
    notify();
  }

  const mod = (UI.mod = {
    info: null,
    tel: null,
    set(src, dst, depth) {
      const i = sourceIndex(src), j = destinationIndex(dst);
      if (i < 0 || j < 0) return;
      depth = Math.round(clamp(Number(depth) || 0, -1, 1) * 100) / 100 || 0;
      mod.info.depths[i][j] = depth;   // at once, so the page answers before the plug-in's echo
      notify();
      // the plug-in answers with the whole grid (false for an unknown key); it also sends `modulation`
      WT.native("modSet", src, dst, depth).then((p) => { if (p && p.sources) setInfo(p); });
    },
    routesOf(paramId) {
      const info = mod.info, out = [];
      if (!info) return out;
      info.destinations.forEach((d, j) => {
        if (d.key !== paramId && !(d.params || []).includes(paramId)) return;
        info.sources.forEach((s, i) => { const depth = info.depths[i][j]; if (depth) out.push({ source: s, destination: d, depth }); });
      });
      return out;
    },
    movedBy(sourceKey) {
      const i = sourceIndex(sourceKey), out = [];
      if (i < 0) return out;
      const s = mod.info.sources[i];
      mod.info.destinations.forEach((d, j) => { const depth = mod.info.depths[i][j]; if (depth) out.push({ source: s, destination: d, depth }); });
      return out;
    },
    on(fn) {
      fns.add(fn);
      if (mod.info) fn();
      return () => fns.delete(fn);
    },
    destName(key) {
      const d = mod.info && mod.info.destinations.find((x) => x.key === key);
      if (!d) return key;
      const p = (d.params || [])[0];
      if (d.module === "Resonators" && p && p.endsWith("_level")) return d.label + " level";
      return d.module === "Resonators" || d.module === "Harmony" || d.label.startsWith(d.module) ? d.label : d.module + " " + d.label.toLowerCase();
    },
    amountText(key, depth) {
      const d = mod.info && mod.info.destinations.find((x) => x.key === key);
      const sign = (x, digits) => (x > 0 ? "+" : x < 0 ? "−" : "") + Math.abs(x).toFixed(digits);
      if (d && d.law === "oct") return sign(3 * depth, 1) + " oct";
      if (d && d.law === "add") {
        const m = WT.META[(d.params || [])[0]];   // half the range per unit of depth
        const half = m ? (m.max - m.min) / 2 : 1;
        return m && m.unit === "dB" ? sign(depth * half, 1) + " dB" : sign(Math.round(depth * half / (m ? m.max - m.min : 1) * 100), 0) + " %";
      }
      return sign(Math.round(depth * 100), 0) + " %";
    },
  });

  const engine = WT.isMock ? mockEngine() : null;
  WT.on("modulation", setInfo);
  // registered before app.js's listener (this file loads first), so the mock's frame carries `mod`
  WT.on("telemetry", (tel) => {
    if (engine && tel && !tel.mod) engine.frame(tel);
    mod.tel = (tel && tel.mod) || null;
    if (mod.tel && mod.info) notify();
  });
  if (engine) setInfo(engine.info());
  else WT.native("modGet").then(setInfo);

  // ================================================================ the browser mock's engine
  function mockEngine() {
    const SOURCES = [
      ["macro_storm", "Storm", "macros", -1, 1], ["macro_breath", "Breath", "macros", 0, 1], ["macro_glow", "Glow", "macros", -1, 1],
      ["macro_space", "Space", "macros", -1, 1], ["macro_drift", "Drift", "macros", -1, 1],
      ["sky_activity", "Activity", "sky", 0, 1], ["sky_onset", "Onset", "sky", 0, 1],
      ["lfo1", "LFO 1", "motion", -1, 1], ["lfo2", "LFO 2", "motion", -1, 1], ["env", "Envelope", "motion", 0, 1],
    ].map(([key, label, group, lo, hi]) => ({ key, label, group, lo, hi }));
    const DESTINATIONS = [
      ["low_level", "Low", "Resonators", "mul"], ["red_level", "Red", "Resonators", "mul"], ["green_level", "Green", "Resonators", "mul"],
      ["violet_level", "Violet", "Resonators", "mul"], ["detune", "Detune", "Resonators", "mul"],
      ["drift", "Drift", "Resonators", "mul"], ["width", "Width", "Resonators", "mul"], ["excite", "Strike", "Resonators", "mul", []],
      ["eq1_gain", "Low", "EQ", "add"], ["eq2_gain", "Mid", "EQ", "add"], ["eq3_gain", "High", "EQ", "add"], ["eq4_freq", "Cut", "EQ", "oct"],
      ["eq_drive", "Drive", "EQ", "add", ["eq1_drive", "eq2_drive", "eq3_drive", "eq4_drive"]],
      ["ph_mix", "Phaser mix", "Phaser", "mul"], ["ch_mix", "Chorus mix", "Chorus", "mul"], ["dly_fb", "Feed", "Delay", "mul"], ["dly_mix", "Mix", "Delay", "mul"],
      ["rev_decay", "Decay", "Reverb", "mul"], ["rev_shimmer", "Shimmer", "Reverb", "mul"], ["rev_mix", "Mix", "Reverb", "mul"],
      ["progression", "Progression", "Harmony", "mul"],
    ].map(([key, label, module, law, params]) => ({ key, label, module, law, params: params || [key] }));
    const targets = [], targetCol = [];
    DESTINATIONS.forEach((d, j) => { for (const p of d.params.length ? d.params : [""]) { targets.push(p); targetCol.push(j); } });
    const si = (k) => SOURCES.findIndex((s) => s.key === k), dj = (k) => DESTINATIONS.findIndex((d) => d.key === k);
    const depths = SOURCES.map(() => DESTINATIONS.map(() => 0));
    const route = (s, d, v) => { if (si(s) >= 0 && dj(d) >= 0) depths[si(s)][dj(d)] = clamp(v, -1, 1); };
    // today's wiring is the default grid (the owner's decisions, 2026-11-13)
    [["macro_storm", "excite", 0.6], ["macro_storm", "progression", 0.6], ["macro_glow", "violet_level", 0.5], ["macro_glow", "rev_shimmer", 0.5],
      ["macro_space", "dly_mix", 0.5], ["macro_space", "rev_mix", 0.5], ["macro_drift", "detune", 0.5], ["macro_drift", "drift", 0.5],
      ["macro_breath", "red_level", -0.35], ["macro_breath", "green_level", -0.35], ["macro_breath", "violet_level", -0.5],
      ["sky_activity", "eq4_freq", 0.3]].forEach((r) => route(...r));
    const hash = new URLSearchParams(location.hash.slice(1));
    if (hash.has("routes"))
      for (const kv of hash.get("routes").split(",")) { const [sd, v] = kv.split(":"), [s, d] = sd.split(">"); route(s, d, Number(v) || 0); }
    const info = () => ({ sources: SOURCES.map((s) => ({ ...s })), destinations: DESTINATIONS.map((d) => ({ ...d, params: d.params.slice() })),
      targets: targets.slice(), depths: depths.map((r) => r.slice()) });
    const say = () => setTimeout(() => WT.dispatch("modulation", info()), 0);   // as the editor echoes a change

    // ---- the sources that run by themselves: two LFOs (SlowLfo.h's shapes) and the envelope (Envelope.h)
    const get = (id, d) => (WT.META[id] ? WT.get(id) : d);
    const knot = (stream, i) => { const x = Math.sin((i + 1013 * stream) * 127.1 + 311.7) * 43758.5453; return (x - Math.floor(x)) * 2 - 1; };
    function lfoAt(stream, shape, cycle) {
      const i = Math.floor(cycle), p = cycle - i;
      if (shape === 0) return Math.sin(2 * Math.PI * p);
      if (shape === 1) return 1 - 4 * Math.abs(p - 0.5);
      if (shape === 2) { const a = knot(stream, i), b = knot(stream, i + 1); return a + (b - a) * p * p * (3 - 2 * p); }
      return knot(stream, i);
    }
    const lfos = [{ cycle: 0, stream: 1 }, { cycle: 0.3, stream: 2 }];
    const HOLD = 1.2;
    const env = { stage: 0, f: 0, v: 0, v0: 0 };
    const fire = () => { env.stage = 1; env.f = env.v; };
    function envStep(dt) {
      const A = get("env_attack", 0.1), D = get("env_decay", 0.6), S = get("env_sustain", 0.35), R = get("env_release", 1.5);
      if (env.stage === 1) { env.v = Math.min(1, env.v + dt / A); env.f = env.v; if (env.v >= 1) { env.stage = 2; env.f = 0; } }
      else if (env.stage === 2) { env.f = Math.min(1, env.f + dt / D); env.v = 1 - (1 - S) * (1 - (1 - env.f) ** 2); if (env.f >= 1) { env.stage = 3; env.f = 0; } }
      else if (env.stage === 3) { env.v = S; env.f = Math.min(1, env.f + dt / HOLD); if (env.f >= 1) { env.stage = 4; env.f = 0; env.v0 = S; } }
      else if (env.stage === 4) { env.f = Math.min(1, env.f + dt / R); env.v = env.v0 * (1 - env.f) ** 2; if (env.f >= 1) { env.stage = 0; env.f = 0; env.v = 0; } }
    }
    window.WT_MOCK_NATIVE = Object.assign(window.WT_MOCK_NATIVE || {}, {
      modGet: () => info(),
      modSet(s, d, v) { route(s, d, Number(v) || 0); say(); return true; },
      envFire() { fire(); return true; },
    });

    let at = 0, act = null, onset = 0, onsetRise = 0;
    const t0 = performance.now();
    let demo = !hash.has("nodemo");
    /** One telemetry frame: the sources now and every target with them, as the engine computes it. */
    function frame(tel) {
      const now = performance.now() / 1000, dt = at ? clamp(now - at, 0, 0.25) : 0;
      at = now;
      const sky = tel.sky || {};
      // Activity, slewed τ 2 s; Onset, 80 ms up to 1 from where it is, then e^(−t / 6 s)
      const a = clamp(sky.activity || 0, 0, 1);
      act = act === null ? a : act + (a - act) * (1 - Math.exp(-dt / 2));
      const onsets = (sky.newOnsets || []).length;
      if (onsets) onsetRise = 1;
      if (onsetRise) { onset = Math.min(1, onset + dt / 0.08); if (onset >= 1) onsetRise = 0; } else onset *= Math.exp(-dt / 6);
      lfos.forEach((l, k) => { l.cycle += dt * get(`lfo${k + 1}_rate`, 0.1); });
      if (onsets && Math.round(get("env_trigger", 0)) === 0) fire();
      if (demo && now - t0 / 1000 > 1.5) { demo = false; fire(); }   // once, so the panel is seen moving
      envStep(dt);
      const macro = (id) => 2 * (WT.get(id) - 0.5);
      const src = [macro("macro_storm"), (tel.breath || 0) * WT.get("macro_breath"), macro("macro_glow"), macro("macro_space"), macro("macro_drift"),
        act, onset, ...lfos.map((l, k) => lfoAt(l.stream, Math.round(get(`lfo${k + 1}_shape`, 0)), l.cycle)), env.v];
      const range = SOURCES.map((s, i) => (i === 1 ? [0, WT.get("macro_breath")] : [s.lo, s.hi]));
      const val = [], lo = [], hi = [];
      targets.forEach((p, t) => {
        const j = targetCol[t], law = DESTINATIONS[j].law, m = p ? WT.META[p] : null;
        // the modulated range (ModMatrix.h kTargets modMin … modMax): levels and Strike to 2, Detune and Drift × 1.5
        const base = m ? WT.get(p) : 1, min = m ? m.min : 0;
        const max = !m ? 2 : p.endsWith("_level") ? 2 * m.max : p === "detune" || p === "drift" ? 1.5 * m.max : m.max;
        let now = law === "mul" ? 1 : 0, l = now, h = now;
        SOURCES.forEach((s, i) => {
          const d = depths[i][j];
          if (!d) return;
          const e0 = d * range[i][0], e1 = d * range[i][1];
          if (law === "mul") { now *= 1 + d * src[i]; l *= 1 + Math.min(e0, e1); h *= 1 + Math.max(e0, e1); }
          else { now += d * src[i]; l += Math.min(e0, e1); h += Math.max(e0, e1); }
        });
        const half = m ? (m.max - m.min) / 2 : 1;
        const f = (x) => clamp(law === "mul" ? base * x : law === "add" ? base + x * half : base * Math.pow(2, 3 * x), min, max);
        val.push(f(now)); lo.push(f(l)); hi.push(f(h));
      });
      const lfo = lfos.map((l, k) => {
        const i = Math.floor(l.cycle);
        // beyond the contract's [phase, value]: the five knots around now, so the picture can draw a
        // random shape's past and what still comes (the engine has them: SlowLfo's seeded knots)
        return [l.cycle - i, src[7 + k], ...[-2, -1, 0, 1, 2].map((n) => knot(l.stream, i + n))];
      });
      tel.mod = { src, val, lo, hi, lfo, env: [env.stage, env.f, env.v] };
      const cut = val[targets.indexOf("eq4_freq")];
      tel.eqCutHz = cut >= 19980 ? 20000 : cut;
    }
    return { info, frame };
  }
})();
