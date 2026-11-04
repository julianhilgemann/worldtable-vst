/* Controls bound to parameters: Knob, Select, Seg, Toggle, DragValue; canvas helpers. */
(function () {
  "use strict";
  const WT = window.WT;
  const SVGNS = "http://www.w3.org/2000/svg";

  const UI = (window.UI = { scale: 1, dpr: window.devicePixelRatio || 1 });

  // ---------- tooltip ----------
  const tip = () => document.getElementById("tooltip");
  function showTip(el, text) {
    const t = tip();
    if (!t) return;
    const r = el.getBoundingClientRect();
    t.textContent = text;
    t.style.display = "block";
    t.style.left = r.left + r.width / 2 - t.offsetWidth / 2 + "px";
    t.style.top = r.top - 22 + "px";
  }
  function hideTip() { const t = tip(); if (t) t.style.display = "none"; }
  UI.showTip = showTip;
  UI.hideTip = hideTip;

  // ---------- geometry ----------
  function polar(cx, cy, r, deg) {
    const a = (deg - 90) * Math.PI / 180;
    return [cx + r * Math.cos(a), cy + r * Math.sin(a)];
  }
  function arcPath(cx, cy, r, a0, a1) {
    if (Math.abs(a1 - a0) < 0.01) a1 = a0 + 0.01;
    const [x0, y0] = polar(cx, cy, r, a0);
    const [x1, y1] = polar(cx, cy, r, a1);
    const large = Math.abs(a1 - a0) > 180 ? 1 : 0;
    const sweep = a1 > a0 ? 1 : 0;
    return `M${x0.toFixed(2)},${y0.toFixed(2)} A${r},${r} 0 ${large} ${sweep} ${x1.toFixed(2)},${y1.toFixed(2)}`;
  }
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

  // ---------- drag helper (vertical, shift = fine) ----------
  function verticalDrag(target, id, opts = {}) {
    const sens = opts.sensitivity || 200;
    target.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      target.setPointerCapture(e.pointerId);
      const startY = e.clientY;
      const startN = WT.toNorm(id, WT.get(id));
      WT.begin(id);
      target.classList.add("active");
      opts.onStart && opts.onStart();
      const move = (ev) => {
        const fine = ev.shiftKey ? 0.15 : 1;
        const dn = (startY - ev.clientY) / (sens * UI.scale) * fine;
        WT.set(id, WT.fromNorm(id, startN + dn));
      };
      const up = () => {
        target.releasePointerCapture(e.pointerId);
        target.removeEventListener("pointermove", move);
        target.removeEventListener("pointerup", up);
        target.removeEventListener("pointercancel", up);
        target.classList.remove("active");
        WT.end(id);
        opts.onEnd && opts.onEnd();
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
      const r = WT.META[id];
      const stepped = r.type !== "float";
      const n = WT.toNorm(id, WT.get(id));
      if (stepped) {
        WT.set(id, WT.get(id) + (e.deltaY < 0 ? 1 : -1));
      } else {
        WT.set(id, WT.fromNorm(id, n - e.deltaY * (e.shiftKey ? 0.0002 : 0.0015)));
      }
      clearTimeout(wheelTimer);
      wheelTimer = setTimeout(() => { WT.end(id); wheelTimer = null; }, 250);
    }, { passive: false });
  }

  function defaultValue(id) {
    const m = WT.META[id];
    return m.type === "bool" ? (m.default ? 1 : 0) : m.default;
  }

  // ---------- Knob ----------
  function Knob(host, id, label, opts = {}) {
    const m = WT.META[id];
    if (!m) { console.warn("unknown param", id); return; }
    const size = opts.small ? 32 : 38;
    const pad = 4;
    const root = el("div", "knob" + (opts.small ? " sm" : "") + (opts.mod ? " mod" : ""), host);
    const s = svg("svg", { width: size + pad * 2, height: size + pad * 2 }, root);
    const cx = size / 2 + pad, cy = size / 2 + pad;
    const rArc = size / 2;
    svg("circle", { cx, cy, r: size / 2 - 4.5, class: "rim", "stroke-width": 1 }, s);
    svg("path", { d: arcPath(cx, cy, rArc, -135, 135), class: "track", "stroke-width": 3.2, fill: "none", "stroke-linecap": "round" }, s);
    const arc = svg("path", { class: "arc", "stroke-width": 3.2, fill: "none", "stroke-linecap": "round" }, s);
    const ptr = svg("line", { class: "ptr", "stroke-width": 2, "stroke-linecap": "round" }, s);
    const lab = el("div", "label", root);
    lab.textContent = label;
    const val = el("div", "value", root);
    const bipolar = (m.type === "float" || m.type === "int") && m.min < 0 && m.max > 0;

    WT.onParam(id, (v) => {
      const n = WT.toNorm(id, v);
      const a = -135 + 270 * n;
      const from = bipolar ? -135 + 270 * WT.toNorm(id, 0) : -135;
      arc.setAttribute("d", from <= a ? arcPath(cx, cy, rArc, from, a) : arcPath(cx, cy, rArc, a, from));
      const [x0, y0] = polar(cx, cy, (size / 2 - 4.5) * 0.25, a);
      const [x1, y1] = polar(cx, cy, size / 2 - 7, a);
      ptr.setAttribute("x1", x0); ptr.setAttribute("y1", y0);
      ptr.setAttribute("x2", x1); ptr.setAttribute("y2", y1);
      val.textContent = WT.fmt(id, v);
    });

    root.title = m.name;
    verticalDrag(root, id);
    return root;
  }

  // ---------- Select (custom menu) ----------
  let openMenu = null;
  function closeMenu() { if (openMenu) { openMenu.remove(); openMenu = null; } }
  document.addEventListener("pointerdown", (e) => { if (openMenu && !openMenu.contains(e.target)) closeMenu(); });

  function optionsFor(id) {
    const m = WT.META[id];
    if (m.type === "choice") return m.choices.map((c, i) => [i, c]);
    if (m.type === "int") { const o = []; for (let v = m.min; v <= m.max; v++) o.push([v, String(v)]); return o; }
    return [];
  }

  function Select(host, id, opts = {}) {
    const root = el("div", "select" + (opts.wide ? " wide" : ""), host);
    if (opts.caption) { const c = el("span", "cap", root); c.textContent = opts.caption; }
    const txt = el("span", "", root);
    const fmtLabel = opts.format || ((v) => (WT.META[id].type === "choice" ? WT.META[id].choices[Math.round(v)] : String(v)));
    WT.onParam(id, (v) => { txt.textContent = fmtLabel(v); });
    root.title = WT.META[id].name;

    root.addEventListener("pointerdown", (e) => {
      e.stopPropagation();
      if (openMenu) { closeMenu(); return; }
      const menu = el("div", "menu", document.body);
      for (const [v, name] of optionsFor(id)) {
        const item = el("div", Math.round(WT.get(id)) === v ? "on" : "", menu);
        item.textContent = name;
        item.addEventListener("pointerdown", (ev) => { ev.stopPropagation(); WT.commit(id, v); closeMenu(); });
      }
      const r = root.getBoundingClientRect();
      menu.style.transformOrigin = "0 0";
      menu.style.transform = `scale(${UI.scale})`;
      menu.style.left = r.left + "px";
      const below = r.bottom + 3;
      menu.style.top = below + "px";
      openMenu = menu;
      const mr = menu.getBoundingClientRect();
      if (mr.bottom > window.innerHeight - 4) menu.style.top = Math.max(4, r.top - mr.height - 3) + "px";
    });
    return root;
  }

  // ---------- Segmented choice ----------
  function Seg(host, id, labels, opts = {}) {
    const root = el("div", "seg" + (opts.big ? " big" : ""), host);
    const m = WT.META[id];
    const names = labels || m.choices;
    const buttons = names.map((name, i) => {
      const b = el("button", "", root);
      b.textContent = name;
      b.addEventListener("click", () => WT.commit(id, i));
      return b;
    });
    WT.onParam(id, (v) => buttons.forEach((b, i) => b.classList.toggle("on", i === Math.round(v))));
    root.title = m.name;
    return root;
  }

  // ---------- Toggle ----------
  function Toggle(host, id, label) {
    const root = el("div", "toggle", host);
    el("i", "", root);
    const t = el("span", "", root);
    t.textContent = label;
    WT.onParam(id, (v) => root.classList.toggle("on", !!v));
    root.addEventListener("click", () => WT.commit(id, WT.get(id) ? 0 : 1));
    root.title = WT.META[id].name;
    return root;
  }

  // ---------- DragValue (numeric text you drag) ----------
  function DragValue(host, id, caption) {
    const root = el("div", "select drag", host);
    const c = el("span", "cap", root);
    c.textContent = caption;
    const t = el("span", "", root);
    WT.onParam(id, (v) => { t.textContent = WT.fmt(id, v); });
    root.title = WT.META[id].name + " (drag, shift = fine, double-click = reset)";
    verticalDrag(root, id, { sensitivity: 400 });
    return root;
  }

  // ---------- canvas ----------
  /** Resizes a canvas backing store for the current scale; returns {ctx, w, h} in CSS px. */
  function fit(canvas) {
    const w = canvas.clientWidth, h = canvas.clientHeight;
    const k = UI.dpr * UI.scale;
    const bw = Math.max(1, Math.round(w * k)), bh = Math.max(1, Math.round(h * k));
    if (canvas.width !== bw || canvas.height !== bh) { canvas.width = bw; canvas.height = bh; }
    const ctx = canvas.getContext("2d");
    ctx.setTransform(k, 0, 0, k, 0, 0);
    return { ctx, w, h };
  }

  /** Canvas-relative pointer position in CSS px (undoing the stage scale). */
  function localPoint(canvas, e) {
    const r = canvas.getBoundingClientRect();
    return [(e.clientX - r.left) / UI.scale, (e.clientY - r.top) / UI.scale];
  }

  const COLORS = {
    accent: "#ffc93c", accentHot: "#ffe08a", accentGlow: "rgba(255,201,60,0.55)",
    blue: "#5fb8ff", blueGlow: "rgba(95,184,255,0.5)", grid: "rgba(110,160,220,0.09)",
    gridStrong: "rgba(110,160,220,0.18)", text: "#7690b0", faint: "#47607e", red: "#ff6363",
  };

  Object.assign(UI, { Knob, Select, Seg, Toggle, DragValue, fit, localPoint, COLORS, el, defaultValue });
})();
