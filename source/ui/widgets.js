/* Controls bound to parameters (spec/06 "instrument layer"): hairline arcs, segmented rows,
 * a 12-key row, toggles. Every control root carries data-param="<id>" (the UI check counts them).
 * Interaction is the POC's: vertical drag, shift = fine, wheel, double-click = default. */
(function () {
  "use strict";
  const WT = window.WT;
  const SVGNS = "http://www.w3.org/2000/svg";

  const UI = (window.UI = { scale: 1, dpr: window.devicePixelRatio || 1 });

  // ---------- helpers ----------
  function el(tag, cls, parent) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (parent) parent.appendChild(e);
    return e;
  }
  function svg(tag, attrs, parent) {
    const e = document.createElementNS(SVGNS, tag);
    for (const k in attrs) e.setAttribute(k, attrs[k]);
    if (parent) parent.appendChild(e);
    return e;
  }
  function polar(cx, cy, r, deg) {
    const a = (deg - 90) * Math.PI / 180;
    return [cx + r * Math.cos(a), cy + r * Math.sin(a)];
  }
  function arcPath(cx, cy, r, a0, a1) {
    if (a1 < a0) [a0, a1] = [a1, a0];
    if (a1 - a0 < 0.01) a1 = a0 + 0.01;
    const [x0, y0] = polar(cx, cy, r, a0);
    const [x1, y1] = polar(cx, cy, r, a1);
    return `M${x0.toFixed(2)},${y0.toFixed(2)} A${r},${r} 0 ${a1 - a0 > 180 ? 1 : 0} 1 ${x1.toFixed(2)},${y1.toFixed(2)}`;
  }
  function defaultValue(id) {
    const m = WT.META[id];
    return m.type === "bool" ? (m.default ? 1 : 0) : m.default;
  }

  // ---------- value text ----------
  const signedPct = (v) => { const p = Math.round((v - 0.5) * 200); return p === 0 ? "neutral" : (p > 0 ? "+" : "−") + Math.abs(p) + " %"; };
  const FORMAT = {
    macro_storm: signedPct, macro_glow: signedPct, macro_space: signedPct, macro_drift: signedPct,
    macro_breath: (v) => (v < 0.005 ? "off" : Math.round(v * 100) + " % at 5/min"),
    progression: (v) => (v < 0.005 ? "hold" : v > 0.995 ? "follow sky" : Math.round(v * 100) + " %"),
    ph_stages: (v) => v + " st.",
    lim_ceiling: (v) => v.toFixed(1) + " dB",
    master: (v) => (v <= -59.9 ? "−∞" : v.toFixed(1) + " dB"),
  };
  UI.format = (id, v) => (FORMAT[id] ? FORMAT[id](v) : WT.fmt(id, v).replace("-", "−"));

  // ---------- tooltip ----------
  const tip = () => document.getElementById("tooltip");
  UI.showTip = function (target, text) {
    const t = tip();
    if (!t) return;
    t.textContent = text;
    t.style.display = "block";
    const r = target.getBoundingClientRect();
    t.style.left = Math.round(r.left + r.width / 2 - t.offsetWidth / 2) + "px";
    t.style.top = Math.round(r.top - t.offsetHeight - 6) + "px";
  };
  UI.hideTip = function () { const t = tip(); if (t) t.style.display = "none"; };

  // ---------- drag (vertical, shift = fine) ----------
  function verticalDrag(target, id, opts = {}) {
    const sens = opts.sensitivity || 220;
    target.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      target.setPointerCapture(e.pointerId);
      const startY = e.clientY;
      const startN = WT.toNorm(id, WT.get(id));
      WT.begin(id);
      target.classList.add("active");
      const move = (ev) => {
        const fine = ev.shiftKey ? 0.15 : 1;
        WT.set(id, WT.fromNorm(id, startN + (startY - ev.clientY) / (sens * UI.scale) * fine));
      };
      const up = () => {
        target.releasePointerCapture(e.pointerId);
        target.removeEventListener("pointermove", move);
        target.removeEventListener("pointerup", up);
        target.removeEventListener("pointercancel", up);
        target.classList.remove("active");
        WT.end(id);
      };
      target.addEventListener("pointermove", move);
      target.addEventListener("pointerup", up);
      target.addEventListener("pointercancel", up);
    });
    target.addEventListener("dblclick", () => WT.commit(id, defaultValue(id)));

    let wheelTimer = null;
    target.addEventListener("wheel", (e) => {
      e.preventDefault();
      if (!wheelTimer) WT.begin(id);
      if (WT.META[id].type !== "float") WT.set(id, WT.get(id) + (e.deltaY < 0 ? 1 : -1));
      else WT.set(id, WT.fromNorm(id, WT.toNorm(id, WT.get(id)) - e.deltaY * (e.shiftKey ? 0.0002 : 0.0015)));
      clearTimeout(wheelTimer);
      wheelTimer = setTimeout(() => { WT.end(id); wheelTimer = null; }, 250);
    }, { passive: false });
  }

  // ---------- Knob: a hairline arc ----------
  const SIZES = { sm: { d: 30, track: 1, arc: 1.5, dot: 2.1 }, macro: { d: 62, track: 1, arc: 2, dot: 3.2 } };

  function Knob(host, id, label, opts = {}) {
    const m = WT.META[id];
    if (!m) { console.warn("unknown param", id); return null; }
    const S = SIZES[opts.size] || SIZES.sm;
    const pad = opts.size === "macro" ? 7 : 4;
    const W = S.d + pad * 2, cx = W / 2, cy = W / 2, r = S.d / 2;
    const root = el("div", `knob ${opts.size === "macro" ? "macro" : "sm"} k-${opts.color || "ink"}`, host);
    root.dataset.param = id;
    const s = svg("svg", { width: W, height: W, viewBox: `0 0 ${W} ${W}` }, root);

    const neutral = opts.size === "macro" ? 0.5
      : (m.type !== "choice" && m.type !== "bool" && m.min < 0 && m.max > 0) ? WT.toNorm(id, 0) : null;

    svg("path", { d: arcPath(cx, cy, r, -135, 135), class: "track", "stroke-width": S.track, fill: "none" }, s);
    if (neutral !== null) {
      const a = -135 + 270 * neutral;
      const [x0, y0] = polar(cx, cy, r + 2, a), [x1, y1] = polar(cx, cy, r + (opts.size === "macro" ? 6 : 4.5), a);
      svg("line", { x1: x0, y1: y0, x2: x1, y2: y1, class: "zero", "stroke-width": 1 }, s);
    }
    // the live ring (macros): what the sky or the sound is doing with this control right now
    let live = null, liveDot = null;
    if (opts.live) {
      live = svg("circle", { cx, cy, r: r * 0.6, class: "live", "stroke-width": 1, "stroke-opacity": 0 }, s);
      if (opts.live === "drift") liveDot = svg("circle", { cx, cy: cy - r * 0.6, r: 1.6, class: "dot", opacity: 0 }, s);
    }
    const arc = svg("path", { class: "arc", "stroke-width": S.arc, fill: "none", "stroke-linecap": "round" }, s);
    const dot = svg("circle", { r: S.dot, class: "dot" }, s);

    const lab = el("div", "label", root);
    lab.textContent = label;
    const val = el("div", "value", root);

    WT.onParam(id, (v) => {
      const n = WT.toNorm(id, v);
      const a = -135 + 270 * n;
      const from = neutral !== null && opts.size !== "macro" ? -135 + 270 * neutral : -135;
      arc.setAttribute("d", arcPath(cx, cy, r, from, a));
      const [x, y] = polar(cx, cy, r, a);
      dot.setAttribute("cx", x.toFixed(2));
      dot.setAttribute("cy", y.toFixed(2));
      val.textContent = UI.format(id, v);
    });

    root.title = m.name + " — drag, shift for fine, double-click resets";
    verticalDrag(root, id);

    return {
      root,
      /** Live ring: level 0…1 (opacity and size), angle (deg) for the drift dot. */
      setLive(level, angle) {
        if (!live) return;
        const k = Math.max(0, Math.min(1, level));
        live.setAttribute("stroke-opacity", (0.08 + 0.55 * k).toFixed(3));
        live.setAttribute("r", (r * (0.48 + 0.2 * k)).toFixed(2));
        if (liveDot && angle !== undefined) {
          const [x, y] = polar(cx, cy, r * 0.6, angle);
          liveDot.setAttribute("cx", x.toFixed(2));
          liveDot.setAttribute("cy", y.toFixed(2));
          liveDot.setAttribute("opacity", (0.25 + 0.75 * k).toFixed(3));
        }
      },
    };
  }

  // ---------- Seg: a row of choices ----------
  function Seg(host, id, labels, opts = {}) {
    const m = WT.META[id];
    const root = el("div", "seg" + (opts.cls ? " " + opts.cls : ""), host);
    root.dataset.param = id;
    root.title = m.name;
    const names = labels || m.choices;
    const buttons = names.map((name, i) => {
      const b = el("button", opts.classOf ? opts.classOf(i) : "", root);
      b.type = "button";
      b.textContent = name;
      b.addEventListener("click", () => WT.commit(id, i));
      return b;
    });
    WT.onParam(id, (v) => buttons.forEach((b, i) => b.classList.toggle("on", i === Math.round(v))));
    return root;
  }

  /** The key as twelve pitch classes, spelt like the chord names (flats, F♯). */
  const KEY_NAMES = ["C", "D♭", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"];
  const ACCIDENTAL = [1, 3, 6, 8, 10];
  function Keys(host, id) {
    return Seg(host, id, KEY_NAMES, { cls: "keys", classOf: (i) => (ACCIDENTAL.includes(i) ? "acc" : "") });
  }

  // ---------- Toggle ----------
  function Toggle(host, id, label) {
    const root = el("div", "toggle", host);
    root.dataset.param = id;
    el("i", "", root);
    el("span", "", root).textContent = label;
    WT.onParam(id, (v) => root.classList.toggle("on", !!v));
    root.addEventListener("click", () => WT.commit(id, WT.get(id) ? 0 : 1));
    root.title = WT.META[id].name;
    return root;
  }

  // ---------- canvas ----------
  /** Sizes a canvas backing store for the stage zoom and the display; returns {ctx, w, h} in CSS px. */
  function fit(canvas) {
    const w = canvas.clientWidth, h = canvas.clientHeight;
    const k = Math.min(3, UI.dpr * UI.scale);
    const bw = Math.max(1, Math.round(w * k)), bh = Math.max(1, Math.round(h * k));
    if (canvas.width !== bw || canvas.height !== bh) { canvas.width = bw; canvas.height = bh; }
    const ctx = canvas.getContext("2d");
    ctx.setTransform(k, 0, 0, k, 0, 0);
    return { ctx, w, h, k };
  }

  const C = {
    night: "#05070d", ink: "#c9d3e6", muted: "#8a96ad", faint: "#6e7991",
    green: "#5cf2a0", red: "#ff6b6b", violet: "#8f7bff",
    hair: "rgba(201,211,230,0.12)", hairSoft: "rgba(201,211,230,0.06)", hairStrong: "rgba(201,211,230,0.22)",
    rgb: { green: [92, 242, 160], red: [255, 107, 107], violet: [143, 123, 255], ink: [201, 211, 230] },
  };
  const rgba = (name, a) => { const c = C.rgb[name]; return `rgba(${c[0]},${c[1]},${c[2]},${a})`; };
  const MONO = (px) => `${px}px "JetBrains Mono", ui-monospace, monospace`;

  Object.assign(UI, { Knob, Seg, Keys, Toggle, fit, el, svg, C, rgba, MONO, KEY_NAMES, defaultValue });
})();
