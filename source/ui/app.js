/* Wires the page: mounts controls declared in index.html, scales the stage,
 * receives backend telemetry and runs the render loop. */
(function () {
  "use strict";
  const WT = window.WT, UI = window.UI;
  const BASE_W = 1200, BASE_H = 800;

  const S = { tel: {} };
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
    UI.ChordView($("cv-chord")),
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

  // ---------- the Sky Model (T2.4): model time, phase, Kp, onsets; read-only ----------
  let skyShownAt = 0;
  function showSky(sky) {
    if (!sky) return;                       // the browser mock has no sky
    const now = performance.now();
    if (now - skyShownAt < 250) return;     // 4 Hz is plenty for text
    skyShownAt = now;
    const d = new Date(sky.utc * 1000);
    const day = d.toLocaleDateString("en-GB", { day: "numeric", month: "short", timeZone: "UTC" });
    const hm = d.toISOString().slice(11, 16);
    const el = $("sky");
    el.hidden = false;
    el.classList.toggle("onset", sky.phase === "onset");
    $("sky-text").textContent =
      `${day} ${hm} UT · ${sky.phase} · Kp ${sky.kp.toFixed(1)} · ${sky.onsets} onsets · ${Math.round(sky.speed)}×`;
  }

  // ---------- the chord readout (T3.6) ----------
  function showChord(chord) {
    if (!chord) return;
    const box = $("chord");
    box.classList.toggle("midi", !!chord.latched);
    $("chord-text").textContent = chord.label || "—";
    $("chord-roman").textContent = chord.roman || "";
    $("chord-state").textContent = chord.latched ? "MIDI HOLD" : chord.pending ? "→ NEXT" : "";
  }

  WT.on("telemetry", (tel) => {
    S.tel = tel;
    showSky(tel.sky);
    showChord(tel.chord);
    if (tel.autoGain) {
      const db = 20 * Math.log10(tel.autoGain);
      $("gain-info").textContent = `auto ${db >= 0 ? "+" : ""}${db.toFixed(1)} dB`;
    }
  });

  // ---------- render loop ----------
  function frame() {
    for (const v of views) v.draw(S);
    requestAnimationFrame(frame);
  }
  requestAnimationFrame(frame);

  if (!WT.isMock) WT.native("uiReady");
})();
