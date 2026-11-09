/* Bridge between the UI and the plug-in.
 *
 * Inside the plug-in, JUCE injects window.__JUCE__; parameters travel over the
 * WebSliderRelay protocol ("__juce__slider<ID>" events carrying real values), and
 * the editor pushes "telemetry" (30 Hz) and "day" (the loaded day's chart, once) events.
 * In a plain browser there is no backend: a mock animates fake telemetry and a
 * synthetic table so the UI can be developed and reviewed without a DAW. */
(function () {
  "use strict";

  const META = window.WT_PARAMS;
  const juce = window.__JUCE__ && window.__JUCE__.backend ? window.__JUCE__ : null;

  const values = {};
  const listeners = {};
  const eventListeners = {};

  for (const id of Object.keys(META)) {
    const m = META[id];
    values[id] = m.type === "bool" ? (m.default ? 1 : 0) : m.default;
    listeners[id] = new Set();
  }

  // ---------- value math (mirrors JUCE NormalisableRange) ----------
  function range(id) {
    const m = META[id];
    if (m.type === "choice") return { min: 0, max: m.choices.length - 1, skew: 1, step: 1 };
    if (m.type === "bool") return { min: 0, max: 1, skew: 1, step: 1 };
    if (m.type === "int") return { min: m.min, max: m.max, skew: 1, step: 1 };
    return { min: m.min, max: m.max, skew: m.skew || 1, step: m.step || 0 };
  }

  function toNorm(id, v) {
    const r = range(id);
    const t = (v - r.min) / (r.max - r.min);
    return Math.pow(Math.min(1, Math.max(0, t)), r.skew);
  }

  function fromNorm(id, n) {
    const r = range(id);
    n = Math.min(1, Math.max(0, n));
    let v = r.min + (r.max - r.min) * Math.pow(n, 1 / r.skew);
    if (r.step > 0) v = r.min + Math.round((v - r.min) / r.step) * r.step;
    return Math.min(r.max, Math.max(r.min, v));
  }

  function notify(id) {
    for (const fn of listeners[id]) fn(values[id]);
  }

  // ---------- parameters ----------
  function get(id) { return values[id]; }

  function set(id, v) {
    const r = range(id);
    v = Math.min(r.max, Math.max(r.min, v));
    if (r.step > 0) v = r.min + Math.round((v - r.min) / r.step) * r.step;
    if (v === values[id]) return;
    values[id] = v;
    notify(id);
    if (juce) juce.backend.emitEvent("__juce__slider" + id, { eventType: "valueChanged", value: v });
  }

  function begin(id) { if (juce) juce.backend.emitEvent("__juce__slider" + id, { eventType: "sliderDragStarted" }); }
  function end(id) { if (juce) juce.backend.emitEvent("__juce__slider" + id, { eventType: "sliderDragEnded" }); }

  /** Sets a value as one complete gesture (for clicks on choices and toggles). */
  function commit(id, v) { begin(id); set(id, v); end(id); }

  function onParam(id, fn) { listeners[id].add(fn); fn(values[id]); return () => listeners[id].delete(fn); }

  function on(eventName, fn) {
    (eventListeners[eventName] = eventListeners[eventName] || new Set()).add(fn);
  }

  function dispatch(eventName, payload) {
    const set = eventListeners[eventName];
    if (set) for (const fn of set) fn(payload);
  }

  // ---------- native functions ----------
  let promiseId = 0;
  const pending = new Map();
  function native(name, ...args) {
    // the browser mock answers the few native calls it can (T6.6: seek)
    if (!juce) { const h = window.WT_MOCK_NATIVE && window.WT_MOCK_NATIVE[name]; return Promise.resolve(h ? h(...args) : null); }
    const id = promiseId++;
    return new Promise((resolve) => {
      pending.set(id, resolve);
      juce.backend.emitEvent("__juce__invoke", { name, params: args, resultId: id });
    });
  }

  // ---------- formatting ----------
  function fmt(id, v) {
    const m = META[id];
    if (m.type === "choice") return m.choices[Math.round(v)];
    if (m.type === "bool") return v ? "On" : "Off";
    const u = m.unit || "";
    if (u === "Hz") return v >= 1000 ? (v / 1000).toFixed(v >= 10000 ? 1 : 2) + " kHz" : (v >= 100 ? v.toFixed(0) : v >= 10 ? v.toFixed(1) : v.toFixed(2)) + " Hz";
    if (u === "s") return v < 1 ? (v * 1000).toFixed(v < 0.01 ? 1 : 0) + " ms" : v.toFixed(v < 10 ? 2 : 1) + " s";
    if (u === "ms") return v.toFixed(0) + " ms";
    if (u === "dB") return (v > 0 ? "+" : "") + v.toFixed(1) + " dB";
    if (u === "dB/oct") return (v > 0 ? "+" : "") + v.toFixed(1) + " dB/oct";
    if (u === "BPM") return v.toFixed(1) + " BPM";
    if (u === "oct") return v.toFixed(1) + " oct";
    if (u === "ct") return v.toFixed(1) + " ct";
    if (u === "st") return (v > 0 ? "+" : v < 0 ? "−" : "") + Math.abs(v) + " st";
    if (m.type === "int") return (m.min < 0 && v > 0 ? "+" : "") + v;
    if (m.min < 0) return (v > 0 ? "+" : "") + Math.round(v * 100) + "%";
    if (m.max === 1 && m.min === 0) return Math.round(v * 100) + "%";
    return v.toFixed(2);
  }

  // ---------- backend wiring ----------
  if (juce) {
    for (const id of Object.keys(META)) {
      juce.backend.addEventListener("__juce__slider" + id, (ev) => {
        if (ev.eventType === "valueChanged") {
          values[id] = ev.value;
          notify(id);
        }
      });
      juce.backend.emitEvent("__juce__slider" + id, { eventType: "requestInitialUpdate" });
    }
    juce.backend.addEventListener("__juce__complete", ({ promiseId: pid, result }) => {
      const r = pending.get(pid);
      if (r) { pending.delete(pid); r(result); }
    });
    for (const name of ["telemetry", "day", "status"])
      juce.backend.addEventListener(name, (payload) => dispatch(name, payload));
  }

  window.WT = { META, get, set, begin, end, commit, onParam, on, native, toNorm, fromNorm, fmt, isMock: !juce, dispatch };
})();
