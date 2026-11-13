/* The modulation state the page shares (T4.2): the grid's sources, destinations and depths (the
 * `modulation` event, also what native `modGet()` returns) and the live values (telemetry `mod`).
 *
 *   UI.mod.info          the last `modulation` payload: { sources, destinations, targets, depths }
 *   UI.mod.tel           the last telemetry `mod`: { src, val, lo, hi, lfo, env }
 *   UI.mod.set(s, d, x)  sets one depth (source key, destination key, −1 … 1; 0 clears) via `modSet`
 *   UI.mod.routesOf(id)  the routes on a parameter: [{ src, dst, depth }]
 *   UI.mod.movedBy(key)  the destinations a source moves: [{ dst, depth }]
 *   UI.mod.on(fn)        calls fn() whenever info or tel changed
 *
 * Outside the plug-in a mock stands in: the default grid and moving source values, 30 Hz.
 * (Stand-in for the grid's branch: the motion UI's mod.js has the same interface and replaces it.) */
(function () {
  "use strict";
  const WT = window.WT, UI = window.UI;
  const fns = new Set();
  const emit = () => { for (const fn of fns) fn(); };
  const M = (UI.mod = {
    info: null, tel: null,
    on(fn) { fns.add(fn); return () => fns.delete(fn); },
    set(src, dst, depth) {
      const info = M.info; if (!info) return;
      const i = info.sources.findIndex((s) => s.key === src), j = info.destinations.findIndex((d) => d.key === dst);
      if (i < 0 || j < 0) return;
      depth = Math.round(Math.max(-1, Math.min(1, depth)) * 100) / 100;
      if (info.depths[i][j] === depth) return;
      info.depths[i][j] = depth;
      WT.native("modSet", src, dst, depth);
      emit();
    },
    routesOf(id) {
      const info = M.info; if (!info) return [];
      const out = [];
      info.destinations.forEach((d, j) => {
        if (d.key !== id && !d.params.includes(id)) return;
        info.sources.forEach((s, i) => { const x = info.depths[i][j]; if (x) out.push({ src: s.key, dst: d.key, depth: x }); });
      });
      return out;
    },
    movedBy(key) {
      const info = M.info; if (!info) return [];
      const i = info.sources.findIndex((s) => s.key === key);
      return i < 0 ? [] : info.destinations.map((d, j) => ({ dst: d.key, depth: info.depths[i][j] })).filter((r) => r.depth);
    },
  });
  const take = (p) => { if (p && p.sources && p.destinations && p.depths) { M.info = p; emit(); } };
  WT.on("modulation", take);
  WT.on("telemetry", (t) => { if (t && t.mod) { M.tel = t.mod; emit(); } });

  if (!WT.isMock) {
    // bridge.js forwards only telemetry, day and status: listen for `modulation` here as well
    const juce = window.__JUCE__ && window.__JUCE__.backend;
    if (juce) juce.addEventListener("modulation", take);
    WT.native("modGet").then(take);
    return;
  }

  // ---------------------------------------------------------------- the mock (browser only)
  const src = (key, label, group, lo, hi) => ({ key, label, group, lo, hi });
  const dst = (key, label, module, law, params) => ({ key, label, module, law, params: params || [key] });
  const info = {
    sources: [
      src("macro_storm", "Storm", "macros", -1, 1), src("macro_breath", "Breath", "macros", 0, 1),
      src("macro_glow", "Glow", "macros", -1, 1), src("macro_space", "Space", "macros", -1, 1),
      src("macro_drift", "Drift", "macros", -1, 1), src("sky_activity", "Activity", "sky", 0, 1),
      src("sky_onset", "Onset", "sky", 0, 1), src("lfo1", "LFO 1", "motion", -1, 1),
      src("lfo2", "LFO 2", "motion", -1, 1), src("env", "Envelope", "motion", 0, 1),
    ],
    destinations: [
      dst("low_level", "Low", "Resonators", "mul"), dst("red_level", "Red", "Resonators", "mul"),
      dst("green_level", "Green", "Resonators", "mul"), dst("violet_level", "Violet", "Resonators", "mul"),
      dst("excite", "Strike", "Resonators", "mul", []), dst("detune", "Detune", "Resonators", "mul"),
      dst("drift", "Drift", "Resonators", "mul"), dst("width", "Width", "Resonators", "mul"),
      dst("eq1_gain", "Low", "EQ", "add"), dst("eq2_gain", "Mid", "EQ", "add"), dst("eq3_gain", "High", "EQ", "add"),
      dst("eq4_freq", "Cut", "EQ", "oct"), dst("eq_drive", "Drive", "EQ", "add", ["eq1_drive", "eq2_drive", "eq3_drive", "eq4_drive"]),
      dst("ph_mix", "Phaser mix", "Phaser", "mul"), dst("ch_mix", "Chorus mix", "Chorus", "mul"),
      dst("dly_fb", "Feed", "Delay", "mul"), dst("dly_mix", "Mix", "Delay", "mul"),
      dst("rev_decay", "Decay", "Reverb", "mul"), dst("rev_shimmer", "Shimmer", "Reverb", "mul"), dst("rev_mix", "Mix", "Reverb", "mul"),
      dst("progression", "Progression", "Harmony", "mul"),
    ],
  };
  info.targets = info.destinations.flatMap((d) => (d.params.length ? d.params : [""]));
  info.depths = info.sources.map(() => info.destinations.map(() => 0));
  const DEFAULT = [["macro_storm", "excite", 0.6], ["macro_storm", "progression", 0.6], ["macro_glow", "violet_level", 0.5],
    ["macro_glow", "rev_shimmer", 0.5], ["macro_space", "dly_mix", 0.5], ["macro_space", "rev_mix", 0.5],
    ["macro_drift", "detune", 0.5], ["macro_drift", "drift", 0.5], ["macro_breath", "red_level", -0.35],
    ["macro_breath", "green_level", -0.35], ["macro_breath", "violet_level", -0.5], ["sky_activity", "eq4_freq", 0.3]];
  for (const [s, d, x] of DEFAULT)
    info.depths[info.sources.findIndex((q) => q.key === s)][info.destinations.findIndex((q) => q.key === d)] = x;
  M.info = info;

  // the sources' values, from the mock's telemetry (breath cycle, activity, onsets) and two LFOs
  let onset = 0, last = performance.now();
  const smooth = (t) => { const i = Math.floor(t), f = t - i, h = (n) => { const x = Math.sin(n * 127.1 + 311.7) * 43758.5453; return (x - Math.floor(x)) * 2 - 1; }; return h(i) + (h(i + 1) - h(i)) * f * f * (3 - 2 * f); };
  WT.on("telemetry", (t) => {
    if (!t || t.mod) return;
    const now = performance.now(), dt = (now - last) / 1000, r = now / 1000; last = now;
    if (t.sky && t.sky.newOnsets && t.sky.newOnsets.length) onset = 1;
    onset *= Math.exp(-dt / 6);
    const mac = (id) => 2 * (WT.get(id) - 0.5);
    const p1 = (r * 0.47) % 1, p2 = (r * 0.22) % 1;
    const lfo = [[p1, Math.sin(2 * Math.PI * p1)], [p2, smooth(r * 0.22)]];
    const envPhase = (r % 12) / 12, env = envPhase < 0.1 ? envPhase / 0.1 : Math.max(0, 1 - (envPhase - 0.1) / 0.5);
    const s = [mac("macro_storm"), (t.breath || 0) * WT.get("macro_breath"), mac("macro_glow"), mac("macro_space"), mac("macro_drift"),
      (t.sky && t.sky.activity) || 0, onset, lfo[0][1], lfo[1][1], env];
    // the targets: each parameter's base moved by its routes (mul: base · Π(1 + d·s); add; oct)
    const val = [], lo = [], hi = [];
    info.destinations.forEach((d, j) => {
      const law = (b, k) => (d.law === "oct" ? b * Math.pow(2, 3 * k) : d.law === "add" ? b + k : b * (1 + k));
      let now0 = 0, lo0 = 0, hi0 = 0;
      info.sources.forEach((q, i) => { const x = info.depths[i][j]; if (!x) return; now0 += x * s[i]; lo0 += Math.min(x * q.lo, x * q.hi); hi0 += Math.max(x * q.lo, x * q.hi); });
      for (const id of d.params.length ? d.params : [""]) {
        const m = WT.META[id], b = m ? WT.get(id) : 1, c = (v) => (m ? Math.max(m.min, Math.min(m.max, v)) : v);
        val.push(c(law(b, now0))); lo.push(c(law(b, lo0))); hi.push(c(law(b, hi0)));
      }
    });
    M.tel = { src: s, val, lo, hi, lfo, env: [env > 0 ? 1 : 0, envPhase, env] };
    emit();
  });
})();
