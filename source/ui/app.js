/* Wires the page: mounts controls declared in index.html, scales the stage,
 * receives backend events and runs the render loop. */
(function () {
  "use strict";
  const WT = window.WT, UI = window.UI;
  const BASE_W = 1200, BASE_H = 800;

  const S = { table: null, tel: {}, status: null };
  window.__wtState = S;   // for debugging and the UI integration check

  // ---------- mount controls ----------
  for (const n of document.querySelectorAll("[data-knob]"))
    UI.Knob(n, n.dataset.knob, n.dataset.label || "", { small: !!n.dataset.small, mod: !!n.dataset.mod });
  for (const n of document.querySelectorAll("[data-select]"))
    UI.Select(n, n.dataset.select, { caption: n.dataset.caption, wide: !!n.dataset.wide });
  for (const n of document.querySelectorAll("[data-seg]"))
    UI.Seg(n, n.dataset.seg, null, { big: !!n.dataset.big });
  for (const n of document.querySelectorAll("[data-toggle]"))
    UI.Toggle(n, n.dataset.toggle, n.dataset.label || "");
  for (const n of document.querySelectorAll("[data-drag]"))
    UI.DragValue(n, n.dataset.drag, n.dataset.caption || "");

  // ---------- views ----------
  const $ = (id) => document.getElementById(id);
  const views = [
    UI.WavetableView($("cv-wt")),
    UI.DataView($("cv-data")),
    UI.HarmonicsView($("cv-harm")),
    UI.FilterView($("cv-filter")),
    UI.EnvView($("cv-amp"), "amp_", "rgba(255,201,60,1)", "rgba(255,201,60,0.55)", "ampEnv"),
    UI.EnvView($("cv-fenv"), "fenv_", "rgba(95,184,255,1)", "rgba(95,184,255,0.5)", "fltEnv"),
    UI.LfoView($("cv-lfo1"), 1, "rgba(255,201,60,1)", "rgba(255,201,60,0.55)"),
    UI.LfoView($("cv-lfo2"), 2, "rgba(95,184,255,1)", "rgba(95,184,255,0.5)"),
    UI.EuclidView($("cv-euclid")),
    UI.DelayView($("cv-delay")),
    UI.ReverbView($("cv-space")),
    UI.MeterView($("cv-meter")),
  ];

  // ---------- scaling ----------
  function rescale() {
    const stage = $("stage");
    UI.scale = Math.min(window.innerWidth / BASE_W, window.innerHeight / BASE_H);
    UI.dpr = window.devicePixelRatio || 1;
    const x = (window.innerWidth - BASE_W * UI.scale) / 2;
    const y = (window.innerHeight - BASE_H * UI.scale) / 2;
    stage.style.transform = `translate(${x}px, ${y}px) scale(${UI.scale})`;
  }
  window.addEventListener("resize", rescale);
  rescale();

  // ---------- events ----------
  WT.on("table", (t) => {
    S.table = t;
    const origin = { live: "live data", cache: "cached data", factory: "bundled recording" }[t.origin] || t.origin;
    const span = t.times && t.times.length > 1 ? (t.times[t.times.length - 1] - t.times[0]) / 3600 : 0;
    $("world-info").textContent =
      `${t.provider} ${t.station} · ${t.place} · ${t.frames} frames over ${span.toFixed(span < 10 ? 1 : 0)} h · ${origin}`;
  });

  WT.on("telemetry", (tel) => { S.tel = tel; });

  function ago(sec) {
    if (sec < 90) return "just now";
    if (sec < 5400) return `${Math.round(sec / 60)} min ago`;
    if (sec < 172800) return `${Math.round(sec / 3600)} h ago`;
    return `${Math.round(sec / 86400)} d ago`;
  }

  WT.on("status", (st) => {
    S.status = st;
    const t = S.table;
    let state = st.state, text = st.message || "";
    if (st.state === "disabled" || (!st.lastSuccess && t && t.origin !== "live")) {
      state = t && t.origin === "cache" ? "cached" : "offline";
    }
    $("status-dot").className = "dot " + (st.state === "live" ? "live" : st.state === "fetching" ? "fetching" : st.state === "offline" ? "offline" : "");
    $("status-state").textContent = state.toUpperCase();
    const parts = [];
    if (text) parts.push(text);
    if (t && t.times && t.times.length) parts.push(`newest frame ${UI.fmtTime(t.times[t.times.length - 1], true)} UTC`);
    if (st.lastSuccess) parts.push(`fetched ${ago(st.now - st.lastSuccess)}`);
    $("status-text").textContent = parts.join("  ·  ");
    $("refresh").classList.toggle("spin", st.state === "fetching");
  });

  $("refresh").addEventListener("click", () => {
    $("refresh").classList.add("spin");
    WT.native("refreshWorld");
  });

  // ---------- render loop ----------
  function frame() {
    for (const v of views) v.draw(S);

    const notes = (S.tel.notes || []).map(UI.noteName);
    $("notes-strip").textContent = notes.length ? "♪ " + notes.join("  ") : "";

    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);

  if (!WT.isMock) WT.native("uiReady");
})();
