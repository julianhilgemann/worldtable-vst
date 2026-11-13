/* The modulation grid (T4.2, spec/09 §3, mockup `drawGrid`): every source against every
 * destination, an overlay over the dome and Harmony (888 × 448) opened from the bottom bar.
 *
 * Rows are the sources in their three groups, each with a small live meter under its name; columns
 * are the destinations under their module's name (a module with one column has no heading). A dot's
 * size is the depth; filled pushes up, hollow pushes down. Drag a dot up or down to set it,
 * double-click to clear; with the grid focused the arrows move between cells, + and − (or Page Up
 * and Page Down; Shift: finer) set the depth, Backspace clears. The row and column under the pointer
 * are lit and the hint line reads the route. Escape or the Modulation button closes it.
 *
 * Everything comes from UI.mod (mod.js): order, labels and modules from the `modulation` payload,
 * the meters from telemetry. One canvas, redrawn on a frame when something changed; no markup is
 * rewritten at 30 Hz. WebKit: it sits over the dome's WebGL canvas, so it has its own compositing
 * layer (style.css `.modgrid`, translateZ(0)). */
(function () {
  "use strict";
  const UI = window.UI;
  const $ = (id) => document.getElementById(id);
  const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
  const panel = $("modgrid"), cv = $("cv-grid"), btn = $("mod-btn");
  if (!panel || !cv || !btn || !UI.mod) return;

  const GR = { gx: 112, top: 96, rh: 26, gap: 12, meter: 72 };
  const BAND = { red_level: "red", green_level: "green", violet_level: "violet" };   // the band levels carry their line's colour
  const HINT0 = "Rows are what moves things, columns are what can be moved. The small bar under a name is that source right now.";
  const HOW = "Drag up for more, down past zero to push the other way, double-click to clear.";
  const KEYS = "Arrows move, + and − set the depth, Backspace clears.";
  let hot = null, cur = [0, 0], drag = null, queued = false, keyed = false;   // keyed: the keyboard moved last (its cell is ringed)

  const info = () => UI.mod.info;
  const depth = (i, j) => (info() && info().depths[i] ? info().depths[i][j] || 0 : 0);

  function geom() {
    const I = info(), w = cv.clientWidth, n = I.destinations.length, cw = (w - GR.gx - 14) / n;
    let y = GR.top + 16, prev = null;
    const rows = I.sources.map((s) => { if (prev !== null && s.group !== prev) y += GR.gap; prev = s.group; const r = y; y += GR.rh; return r; });
    return { w, cw, rows, X: (j) => GR.gx + (j + 0.5) * cw };
  }
  function routeText(k, keys) {
    const I = info(), s = I.sources[k[0]], d = I.destinations[k[1]], a = depth(k[0], k[1]);
    return `${s.label} ${a ? "moves" : "could move"} ${UI.mod.destName(d.key)}${a ? " by " + UI.mod.amountText(d.key, a) : ""}. ${keys ? KEYS : HOW}`;
  }

  // ---------------------------------------------------------------- drawing
  function draw() {
    queued = false;
    if (panel.hidden || !info()) return;
    const f = UI.fit(cv), c = f.ctx, h = f.h, g = geom(), w = g.w, I = info(), C = UI.C, D = I.destinations;
    const tel = UI.mod.tel, focused = document.activeElement === cv;
    const lit = hot || (focused ? cur : null);
    c.clearRect(0, 0, w, h);
    // the modules along the top
    let g0 = 0;
    D.forEach((d, j) => {
      if (j < D.length - 1 && D[j + 1].module === d.module) return;
      const x0 = GR.gx + g0 * g.cw, x1 = GR.gx + (j + 1) * g.cw, span = j + 1 - g0;
      g0 = j + 1;
      if (span < 2) return;   // a module with one column needs no heading: the column says it
      c.font = UI.SANS(11, 500); c.textAlign = "center"; c.textBaseline = "alphabetic"; c.fillStyle = C.inkHi;
      c.fillText(d.module, (x0 + x1) / 2, 12);
      c.fillStyle = "rgba(201,211,230,0.3)"; c.fillRect(x0 + 5, 18, x1 - x0 - 10, 1);
    });
    // what can be moved
    D.forEach((d, j) => {
      const x = g.X(j), on = lit && lit[1] === j;
      c.save(); c.translate(x + 4, GR.top + 2); c.rotate(-Math.PI / 2);
      c.font = UI.SANS(11, on ? 600 : 400); c.textAlign = "left"; c.textBaseline = "alphabetic";
      c.fillStyle = BAND[d.key] ? UI.rgba(BAND[d.key], 1) : on ? C.inkHi : C.muted;
      c.fillText(d.label, 0, 0); c.restore();
      c.fillStyle = on ? "rgba(201,211,230,0.2)" : "rgba(201,211,230,0.06)";
      c.fillRect(Math.round(x), GR.top + 8, 1, h - GR.top - 14);
    });
    // what moves things
    I.sources.forEach((s, i) => {
      const y = g.rows[i], on = lit && lit[0] === i;
      if (i && I.sources[i - 1].group !== s.group) { c.fillStyle = "rgba(201,211,230,0.1)"; c.fillRect(12, y - GR.rh / 2 - 6, w - 24, 1); }
      if (on) { c.fillStyle = "rgba(201,211,230,0.05)"; c.fillRect(8, y - GR.rh / 2, w - 16, GR.rh); }
      c.fillStyle = on ? "#fff" : C.ink; c.font = UI.SANS(12.5, 500); c.textAlign = "left"; c.textBaseline = "middle";
      c.fillText(s.label, 14, y - 3);
      // the meter: from zero (the middle for a bipolar source) to the value now
      const v = tel && tel.src ? tel.src[i] || 0 : 0, mx = 14, mw = GR.meter, bi = s.lo < 0;
      const zero = bi ? mx + mw / 2 : mx, px = clamp(bi ? (v * mw) / 2 : ((v - s.lo) / (s.hi - s.lo || 1)) * mw, -mw / 2, mw);
      c.fillStyle = "#1a2233"; c.fillRect(mx, y + 8, mw, 2);
      c.fillStyle = C.arc; c.fillRect(Math.min(zero, zero + px), y + 8, Math.max(1, Math.abs(px)), 2);
      D.forEach((d, j) => {
        const a = depth(i, j), x = g.X(j) + 0.5;
        c.beginPath();
        if (Math.abs(a) < 0.03) { c.fillStyle = "#56607a"; c.arc(x, y, 1.6, 0, 2 * Math.PI); c.fill(); }
        else {
          c.arc(x, y, 3.5 + 7.5 * Math.abs(a), 0, 2 * Math.PI);
          if (a > 0) { c.fillStyle = C.arc; c.fill(); } else { c.strokeStyle = C.arc; c.lineWidth = 1.6; c.stroke(); }
        }
      });
    });
    // the keyboard's cell
    if (focused && keyed) {
      const x = g.X(cur[1]) + 0.5, y = g.rows[cur[0]];
      c.strokeStyle = C.inkHi; c.lineWidth = 1.5;
      c.beginPath(); c.roundRect(x - g.cw / 2 + 2, y - GR.rh / 2 + 1, g.cw - 4, GR.rh - 2, 5); c.stroke();
    }
  }
  const redraw = () => { if (!queued && !panel.hidden) { queued = true; requestAnimationFrame(draw); } };
  UI.mod.on(redraw);

  // ---------------------------------------------------------------- the pointer
  const local = (e) => { const r = cv.getBoundingClientRect(), k = cv.clientWidth / (r.width || 1); return { x: (e.clientX - r.left) * k, y: (e.clientY - r.top) * k }; };
  function cell(p) {
    if (!info()) return null;
    const g = geom(), j = Math.floor((p.x - GR.gx) / g.cw);
    const i = g.rows.findIndex((y) => Math.abs(p.y - y) <= GR.rh / 2);
    return i >= 0 && j >= 0 && j < info().destinations.length ? [i, j] : null;
  }
  const keyOf = (k) => [info().sources[k[0]].key, info().destinations[k[1]].key];
  function say(text) { if (cv.dataset.hint !== text) { cv.dataset.hint = text; UI.showHint(cv); } }
  /** The knobs the source of row `i` moves are lit while its row is (app.js `setLit`); -1: none. */
  let litRow = -1;
  function lightRow(i) {
    if (i === litRow) return;
    const knobs = UI.knobs || {}, set = (row, on) => {
      if (row < 0 || !info().sources[row]) return;
      for (const r of UI.mod.movedBy(info().sources[row].key)) for (const p of r.destination.params || []) if (knobs[p] && knobs[p].setLit) knobs[p].setLit(on);
    };
    set(litRow, false); set(i, true); litRow = i;
  }
  function point(k) {
    const same = (hot && k && hot[0] === k[0] && hot[1] === k[1]) || (!hot && !k);
    hot = k;
    lightRow(k ? k[0] : -1);
    cv.style.cursor = k ? "ns-resize" : "default";
    say(k ? routeText(k) : HINT0);
    if (!same) redraw();
  }
  cv.addEventListener("pointermove", (e) => {
    if (drag) {
      const p = local(e), a = clamp(drag.a0 + (drag.y0 - p.y) / 70, -1, 1);
      UI.mod.set(...keyOf(drag.k), Math.abs(a) < 0.03 ? 0 : a);
      say(routeText(drag.k));
      return;
    }
    point(cell(local(e)));
  });
  cv.addEventListener("pointerleave", () => { if (!drag) point(null); });
  cv.addEventListener("pointerdown", (e) => {
    if (e.button !== 0) return;
    const p = local(e), k = cell(p);
    if (!k) return;
    e.preventDefault();
    cv.focus({ preventScroll: true });
    cur = k; keyed = false;
    drag = { k, y0: p.y, a0: depth(k[0], k[1]) };
    try { cv.setPointerCapture(e.pointerId); } catch (err) { /* synthetic events: no capture */ }
  });
  const stop = (e) => { if (!drag) return; drag = null; lightRow(-1); point(cell(local(e))); };   // the row's routes may have changed
  cv.addEventListener("pointerup", stop);
  cv.addEventListener("pointercancel", stop);
  cv.addEventListener("dblclick", (e) => {
    const k = cell(local(e));
    if (k) { UI.mod.set(...keyOf(k), 0); say(routeText(k)); }
  });

  // ---------------------------------------------------------------- the keyboard
  cv.addEventListener("keydown", (e) => {
    if (!info()) return;
    const ns = info().sources.length, nd = info().destinations.length, step = e.shiftKey ? 0.01 : 0.05;
    let [i, j] = cur, set = null;
    switch (e.key) {
      case "ArrowUp": i = Math.max(0, i - 1); break;
      case "ArrowDown": i = Math.min(ns - 1, i + 1); break;
      case "ArrowLeft": j = Math.max(0, j - 1); break;
      case "ArrowRight": j = Math.min(nd - 1, j + 1); break;
      case "Home": j = 0; break;
      case "End": j = nd - 1; break;
      case "+": case "=": case "PageUp": set = depth(i, j) + step; break;
      case "-": case "−": case "PageDown": set = depth(i, j) - step; break;
      case "Backspace": case "Delete": set = 0; break;
      default: return;
    }
    e.preventDefault();
    cur = [i, j]; keyed = true;
    if (set !== null) UI.mod.set(...keyOf(cur), Math.abs(set) < 0.005 ? 0 : set);
    if (set !== null) lightRow(-1);
    lightRow(cur[0]);
    say(routeText(cur, true));
    redraw();
  });
  cv.addEventListener("focus", () => { if (cv.matches(":focus-visible")) keyed = true; if (info()) say(routeText(cur, true)); redraw(); });
  cv.addEventListener("blur", () => { keyed = false; lightRow(hot ? hot[0] : -1); say(HINT0); redraw(); });

  // ---------------------------------------------------------------- open and close
  const grid = {
    get open() { return !panel.hidden; },
    set(open) {
      if (open === grid.open) return;
      if (!open && panel.contains(document.activeElement)) btn.focus({ preventScroll: true });
      panel.hidden = !open;
      btn.setAttribute("aria-expanded", String(open));
      if (open) { hot = null; draw(); } else lightRow(-1);
    },
    toggle() { grid.set(!grid.open); },
  };
  UI.grid = grid;
  cv.dataset.hint = HINT0;
  btn.addEventListener("click", () => grid.toggle());   // the click goes on to the document: open popovers close
  panel.addEventListener("click", (e) => e.stopPropagation());
  document.addEventListener("keydown", (e) => { if (e.key === "Escape" && grid.open) grid.set(false); });
  window.addEventListener("resize", redraw);
  // for review screenshots: #grid opens it, #grid=<row>,<column> also lights that cell
  const hash = new URLSearchParams(location.hash.slice(1));
  if (hash.has("grid")) {
    grid.set(true);
    const k = hash.get("grid").split(",").map(Number);
    if (k.length === 2 && info() && info().sources[k[0]] && info().destinations[k[1]]) point(k);
  }
})();
