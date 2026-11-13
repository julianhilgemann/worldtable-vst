/* The Journey transport (T6.9, mockup top bar): run or freeze, the clock, the speed, the recorded
 * day, loop. The plug-in owns the state (PluginProcessor::setJourney, saved with the session); the
 * page asks through the native function `journeySet({ frozen?, speed?, loop?, day? })` and shows
 * what telemetry's `journey: { day, frozen, speed, loop, ended }` says. The day list and the speeds
 * come with the "day" event (`days: [{ id, label }]`, `speeds`). The clock itself is app.js's
 * (#clock, #ro-date, #ro-ut). No key is bound globally: hosts use the space bar. */
(function () {
  "use strict";
  const WT = window.WT;
  const $ = (id) => document.getElementById(id);
  const runBtn = $("tp-run"), speedSeg = $("tp-speed"), dayBtn = $("tp-day"), loopBtn = $("tp-loop"), menu = $("tp-days");
  if (!runBtn) return;

  const ICON = {
    freeze: '<svg width="12" height="12" viewBox="0 0 12 12" aria-hidden="true"><rect x="2" y="1.5" width="3" height="9" rx="0.8" fill="currentColor"/><rect x="7" y="1.5" width="3" height="9" rx="0.8" fill="currentColor"/></svg>',
    run: '<svg width="12" height="12" viewBox="0 0 12 12" aria-hidden="true"><path d="M3 1.5v9l7.5-4.5z" fill="currentColor"/></svg>',
  };
  const CHEVRON = '<svg class="chev" width="9" height="6" viewBox="0 0 9 6" fill="none" aria-hidden="true"><path d="M1 1l3.5 3.5L8 1" stroke="currentColor" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"/></svg>';

  /** What the transport shows; telemetry and journeySet's answer overwrite it. */
  const J = { day: "", frozen: false, speed: 24, loop: false, ended: false };
  let days = [], speeds = [];
  let speedButtons = [];
  const shown = {};
  window.__wtState && (window.__wtState.journey = J);   // for the UI check

  /** "10–11 May 2024 · Gannon storm (Kp 9)" → the date and the words. */
  const split = (label) => { const i = (label || "").indexOf(" · "); return i < 0 ? [label || "", ""] : [label.slice(0, i), label.slice(i + 3)]; };
  const dayOf = (id) => days.find((d) => d.id === id);
  UI.dayLabel = (id, sep = ", ") => { const d = dayOf(id); return d ? split(d.label).filter(Boolean).join(sep) : id; };
  const fmtSpeed = (s) => Math.round(s) + "×";

  // ---------------------------------------------------------------- asking the plug-in
  function send(change) {
    Object.assign(J, change);   // at once; the answer and the next telemetry frame correct it
    if (change.frozen === false) J.ended = false;
    show();
    return Promise.resolve(WT.native("journeySet", change)).then((s) => { if (s && typeof s === "object") { Object.assign(J, s); show(); } });
  }

  // ---------------------------------------------------------------- run or freeze
  runBtn.addEventListener("click", () => send({ frozen: !(J.frozen || J.ended) }));

  // ---------------------------------------------------------------- speed: a radiogroup (arrows, Home/End)
  function buildSpeeds() {
    speedSeg.textContent = "";
    speedButtons = speeds.map((s) => {
      const b = document.createElement("button");
      b.type = "button";
      b.setAttribute("role", "radio");
      b.textContent = fmtSpeed(s);
      b.setAttribute("aria-label", fmtSpeed(s) + (s >= 24 ? `, a day in ${24 / s >= 1 ? (24 / s) + " h" : Math.round(1440 / s) + " min"}` : ", real time"));
      b.addEventListener("click", () => send({ speed: s }));
      speedSeg.appendChild(b);
      return b;
    });
    shown.speed = undefined;
  }
  speedSeg.addEventListener("keydown", (e) => {
    const n = speeds.length, i = Math.max(0, speeds.indexOf(J.speed)), k = e.key;
    const to = k === "ArrowRight" || k === "ArrowDown" ? Math.min(n - 1, i + 1) : k === "ArrowLeft" || k === "ArrowUp" ? Math.max(0, i - 1)
      : k === "Home" ? 0 : k === "End" ? n - 1 : null;
    if (to === null || !n) return;
    e.preventDefault();
    if (to !== i) send({ speed: speeds[to] });
    speedButtons[to].focus({ preventScroll: true });
  });

  // ---------------------------------------------------------------- the day menu
  function openMenu(open, focusItem) {
    menu.hidden = !open;
    dayBtn.setAttribute("aria-expanded", String(open));
    if (!open) return;
    menu.textContent = "";
    const items = [];
    for (const d of days) {
      const [date, words] = split(d.label);
      const b = document.createElement("button");
      b.type = "button";
      b.setAttribute("role", "menuitemradio");
      b.setAttribute("aria-checked", String(d.id === J.day));
      b.className = d.id === J.day ? "on" : "";
      b.innerHTML = `<span>${date}<small>${words}</small></span>`;
      b.addEventListener("click", () => { openMenu(false); dayBtn.focus({ preventScroll: true }); if (d.id !== J.day) send({ day: d.id }); });
      menu.appendChild(b);
      items.push(b);
    }
    menu.appendChild(document.createElement("hr"));
    const live = document.createElement("button");
    live.type = "button";
    live.setAttribute("role", "menuitem");
    live.setAttribute("aria-disabled", "true");
    live.disabled = true;
    live.innerHTML = "<span>Live sky<small>arrives with the data milestone</small></span>";
    menu.appendChild(live);
    // under the button, in stage pixels (the stage is scaled)
    const s = $("stage").getBoundingClientRect(), r = dayBtn.getBoundingClientRect(), k = UI.scale || 1;
    menu.style.left = Math.round((r.left - s.left) / k) + "px";
    menu.style.top = Math.round((r.bottom - s.top) / k + 4) + "px";
    if (focusItem) (items.find((b) => b.classList.contains("on")) || items[0]).focus({ preventScroll: true });
  }
  dayBtn.addEventListener("click", () => openMenu(menu.hidden, false));
  dayBtn.addEventListener("keydown", (e) => {
    if (e.key === "ArrowDown" || e.key === "ArrowUp") { e.preventDefault(); openMenu(true, true); }
  });
  menu.addEventListener("keydown", (e) => {
    const items = [...menu.querySelectorAll("button:not([disabled])")], i = items.indexOf(document.activeElement);
    if (e.key === "ArrowDown" || e.key === "ArrowUp") {
      e.preventDefault();
      items[(i + (e.key === "ArrowDown" ? 1 : items.length - 1)) % items.length].focus({ preventScroll: true });
    } else if (e.key === "Home" || e.key === "End") {
      e.preventDefault();
      items[e.key === "Home" ? 0 : items.length - 1].focus({ preventScroll: true });
    } else if (e.key === "Escape" || e.key === "Tab") {
      if (e.key === "Escape") e.preventDefault();
      openMenu(false);
      dayBtn.focus({ preventScroll: true });
    }
  });
  // a click anywhere else closes it (capture: other popovers stop their clicks' propagation)
  document.addEventListener("click", (e) => { if (!menu.hidden && !menu.contains(e.target) && !dayBtn.contains(e.target)) openMenu(false); }, true);
  document.addEventListener("keydown", (e) => { if (e.key === "Escape" && !menu.hidden) openMenu(false); });

  // ---------------------------------------------------------------- loop: a switch
  loopBtn.addEventListener("click", () => send({ loop: !J.loop }));
  loopBtn.addEventListener("keydown", (e) => {
    const k = e.key, to = k === "ArrowRight" || k === "ArrowUp" || k === "End" ? true : k === "ArrowLeft" || k === "ArrowDown" || k === "Home" ? false : null;
    if (to === null) return;
    e.preventDefault();
    if (to !== J.loop) send({ loop: to });
  });

  // ---------------------------------------------------------------- showing the state
  function show() {
    const run = J.frozen || J.ended;   // the button offers what a click does
    if (shown.run !== run || shown.ended !== J.ended) {
      shown.run = run;
      shown.ended = J.ended;
      runBtn.innerHTML = run ? ICON.run : ICON.freeze;
      runBtn.setAttribute("aria-label", J.ended ? "Run the recording again" : run ? "Run the journey" : "Freeze the journey");
      runBtn.dataset.hint = J.ended ? "The recording has ended. Run plays it again from the beginning."
        : run ? "Run the journey on from here. Frozen, the curtain keeps breathing in place."
        : "Freeze the journey. Frozen, the curtain keeps breathing in place.";
      runBtn.classList.toggle("frozen", run);
    }
    if (shown.speed !== J.speed) {
      shown.speed = J.speed;
      const k = speeds.indexOf(J.speed);
      speedButtons.forEach((b, i) => {
        b.classList.toggle("on", i === k);
        b.setAttribute("aria-checked", String(i === k));
        b.tabIndex = i === (k < 0 ? 0 : k) ? 0 : -1;
      });
      speedSeg.setAttribute("aria-valuetext", fmtSpeed(J.speed));
    }
    if (shown.day !== J.day || shown.days !== days) {
      shown.day = J.day;
      shown.days = days;
      const [date, words] = split((dayOf(J.day) || {}).label || J.day);
      dayBtn.innerHTML = `<b>${date || "—"}</b><span>${words}</span>${CHEVRON}`;
      dayBtn.setAttribute("aria-label", "Recorded day: " + UI.dayLabel(J.day));
    }
    if (shown.loop !== J.loop) {
      shown.loop = J.loop;
      loopBtn.classList.toggle("on", J.loop);
      loopBtn.setAttribute("aria-checked", String(J.loop));
    }
  }

  WT.on("day", (d) => {
    if (Array.isArray(d.days) && d.days.length) days = d.days;
    if (Array.isArray(d.speeds) && d.speeds.join() !== speeds.join()) { speeds = d.speeds.slice(); buildSpeeds(); }
    if (!J.day) J.day = d.day;
    show();
  });
  WT.on("telemetry", (tel) => {
    const j = tel.journey;
    if (!j) return;
    J.day = j.day; J.frozen = !!j.frozen; J.speed = j.speed; J.loop = !!j.loop; J.ended = !!j.ended;
    show();
  });
  show();
})();
