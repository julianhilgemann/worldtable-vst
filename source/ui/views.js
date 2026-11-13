/* The instrument layer's live pictures (spec/06, T6.1). Each view redraws only when what it shows
 * has changed (or, for the moving ones, only while its tab is visible), and every change glides.
 * T6.7 (spec/09 §2 rule 2): red, green and violet only where a picture shows the bands (layers, the
 * keyboard strip) or the sky's own light (magnetogram); the effect pictures are drawn in ink.
 * Axis numbers are at least 10 px, words in pictures 11 px. */
(function () {
  "use strict";
  const UI = window.UI, C = UI.C, rgba = UI.rgba, MONO = UI.MONO;
  const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
  const LAYERS = ["red", "green", "violet"];
  const now = () => performance.now() / 1000;

  // ================================================================ magnetogram
  const PHASE_FILL = ["rgba(138,150,173,0.22)", "rgba(143,123,255,0.55)", "rgba(92,242,160,0.95)", "rgba(92,242,160,0.38)"];

  function MagnetogramView(canvas) {
    const base = document.createElement("canvas");
    let day = null, dirty = true, lastX = -1, lastW = 0, lastK = 0, hoverX = null;
    const PHASES = ["quiet", "growth", "onset", "recovery"];
    const L = 118, PAD_R = 10;
    // lanes and legend laid out for 64 px and stretched to the strip's height (92 since T6.8)
    let lanes = { mag: [6, 37], load: [40, 48], phase: [51, 56] }, ky = 1;

    function setDay(d) { day = d; dirty = true; }

    function drawBase(w, h, k) {
      base.width = Math.round(w * k); base.height = Math.round(h * k);
      const ctx = base.getContext("2d");
      ctx.setTransform(k, 0, 0, k, 0, 0);
      ctx.clearRect(0, 0, w, h);
      ky = h / 64;
      lanes = { mag: [6 * ky, 37 * ky], load: [40 * ky, 48 * ky], phase: [51 * ky, 56 * ky] };
      const R = w - PAD_R, n = day.phase.length;
      const x = (i) => L + (R - L) * (i / Math.max(1, n - 1));

      ctx.font = MONO(10);
      ctx.textBaseline = "alphabetic";
      // 6-hour grid
      for (let m = 0; m < n; m += 360) {
        const xx = Math.round(x(m)) + 0.5;
        ctx.strokeStyle = C.hairSoft; ctx.lineWidth = 1;
        ctx.beginPath(); ctx.moveTo(xx, 3); ctx.lineTo(xx, h - 3); ctx.stroke();
        const d = new Date((day.t0 + m * day.step) * 1000);
        const label = m === 0 ? d.toLocaleDateString("en-GB", { day: "numeric", month: "short", timeZone: "UTC" }) : d.toISOString().slice(11, 16);
        ctx.fillStyle = C.faint; ctx.fillText(label, xx + 3, 12);
      }

      // magnetogram: measured (green) over the model (violet), symmetric scale
      let max = 50;
      for (let i = 0; i < n; i++) max = Math.max(max, Math.abs(day.dh[i]), Math.abs(day.dhModel[i]));
      const [m0, m1] = lanes.mag, mid = (m0 + m1) / 2, sy = (m1 - m0) / 2 / max;
      ctx.strokeStyle = C.hair;
      ctx.beginPath(); ctx.moveTo(L, mid + 0.5); ctx.lineTo(R, mid + 0.5); ctx.stroke();
      const line = (arr, color, width) => {
        ctx.strokeStyle = color; ctx.lineWidth = width; ctx.beginPath();
        for (let i = 0; i < n; i++) { const y = mid - arr[i] * sy; i ? ctx.lineTo(x(i), y) : ctx.moveTo(x(i), y); }
        ctx.stroke();
      };
      line(day.dhModel, "rgba(143,123,255,0.8)", 1);
      line(day.dh, C.green, 1);

      // load: coupling (red area), reservoir (ink) against its threshold
      const [l0, l1] = lanes.load;
      let emax = 2, wmax = 1.5;
      for (let i = 0; i < n; i++) { emax = Math.max(emax, day.coupling[i]); wmax = Math.max(wmax, day.energy[i], day.threshold[i]); }
      ctx.beginPath();
      for (let i = 0; i < n; i++) { const y = l1 - (day.coupling[i] / emax) * (l1 - l0); i ? ctx.lineTo(x(i), y) : ctx.moveTo(x(i), y); }
      ctx.lineTo(R, l1); ctx.lineTo(L, l1); ctx.closePath();
      ctx.fillStyle = "rgba(255,107,107,0.26)"; ctx.fill();
      const lane = (arr, color) => {
        ctx.strokeStyle = color; ctx.lineWidth = 1; ctx.beginPath();
        for (let i = 0; i < n; i++) { const y = l1 - (arr[i] / wmax) * (l1 - l0); i ? ctx.lineTo(x(i), y) : ctx.moveTo(x(i), y); }
        ctx.stroke();
      };
      lane(day.threshold, "rgba(201,211,230,0.22)");
      lane(day.energy, "rgba(201,211,230,0.8)");

      // phase band and onsets
      const [p0, p1] = lanes.phase, cw = (R - L) / n + 0.6;
      for (let i = 0; i < n; i++) { ctx.fillStyle = PHASE_FILL[day.phase[i]] || PHASE_FILL[0]; ctx.fillRect(x(i), p0, cw, p1 - p0); }
      for (const e of day.onsets) {
        const i = (e.utc - day.t0) / day.step;
        if (i < 0 || i > n) continue;
        ctx.fillStyle = e.kind === "forced" ? C.red : e.kind === "pseudo" ? C.muted : C.green;
        ctx.beginPath(); ctx.moveTo(x(i), p0 - 0.5); ctx.lineTo(x(i) - 2.6, p0 - 4.5); ctx.lineTo(x(i) + 2.6, p0 - 4.5); ctx.fill();
      }

      // labels
      // the legend stays at 10 px: five lanes in 64 px leave no room (T6.10 replaces this picture)
      ctx.font = MONO(10);
      ctx.fillStyle = C.inkHi; ctx.fillText("Magnetogram", 10, 14 * ky);
      ctx.fillStyle = C.muted; ctx.fillText(`ΔH ±${Math.round(max)} nT`, 10, 26 * ky);
      ctx.fillStyle = C.green; ctx.fillText("— " + (day.station ? day.station.code : "CMO"), 10, 37 * ky);
      ctx.fillStyle = C.violet; ctx.fillText("— model", 52, 37 * ky);
      ctx.fillStyle = C.ink; ctx.fillText("energy", 10, 48 * ky);
      ctx.fillStyle = C.red; ctx.fillText("E", 56, 48 * ky);
      ctx.fillStyle = C.muted; ctx.fillText("phase", 10, 58 * ky);
      dirty = false;
    }

    /** T6.6: the moment under x (CSS px in the canvas), or null over the labels. */
    function utcAt(x) {
      if (!day) return null;
      const R = canvas.clientWidth - PAD_R, n = day.phase.length;
      if (x < L - 4 || x > R + 4) return null;
      return day.t0 + clamp((x - L) / (R - L), 0, 1) * (n - 1) * day.step;
    }
    function phaseAt(utc) { return day ? PHASES[day.phase[clamp(Math.round((utc - day.t0) / day.step), 0, day.phase.length - 1)]] || "" : ""; }
    function setHover(x) { if (x !== hoverX) { hoverX = x; lastX = -1; } }

    function draw(nowUtc) {
      if (!day) return;
      const w = canvas.clientWidth, h = canvas.clientHeight, k = Math.min(3, UI.dpr * UI.scale);
      if (dirty || w !== lastW || k !== lastK) { drawBase(w, h, k); lastW = w; lastK = k; lastX = -1; }
      const R = w - PAD_R, n = day.phase.length;
      const xNow = L + (R - L) * clamp((nowUtc - day.t0) / day.step / Math.max(1, n - 1), 0, 1);
      if (Math.abs(xNow - lastX) < 0.25) return;
      lastX = xNow;
      const { ctx } = UI.fit(canvas);
      ctx.clearRect(0, 0, w, h);
      ctx.drawImage(base, 0, 0, w, h);
      // what is still ahead in the replayed day, dimmed
      ctx.fillStyle = "rgba(5,7,13,0.55)";
      ctx.fillRect(xNow, 2, R - xNow + 2, h - 4);
      ctx.strokeStyle = C.red; ctx.lineWidth = 1; ctx.setLineDash([2, 3]);
      ctx.beginPath(); ctx.moveTo(Math.round(xNow) + 0.5, 3); ctx.lineTo(Math.round(xNow) + 0.5, h - 3); ctx.stroke();
      ctx.setLineDash([]);

      // T6.6: where a click would jump to
      const utc = hoverX === null ? null : utcAt(hoverX);
      if (utc !== null) {
        const xh = Math.round(L + (R - L) * clamp((utc - day.t0) / day.step / Math.max(1, n - 1), 0, 1)) + 0.5;
        ctx.strokeStyle = C.ink; ctx.globalAlpha = 0.7;
        ctx.beginPath(); ctx.moveTo(xh, 3); ctx.lineTo(xh, h - 3); ctx.stroke();
        ctx.globalAlpha = 1;
        const d = new Date(utc * 1000);
        const label = `${d.toLocaleDateString("en-GB", { day: "numeric", month: "short", timeZone: "UTC" })} ${d.toISOString().slice(11, 16)} UT`;
        const sub = `${phaseAt(utc)}, click to jump`;
        ctx.font = MONO(11);
        const tw = Math.max(ctx.measureText(label).width, ctx.measureText(sub).width) + 12;
        const bx = xh + tw + 6 > R ? xh - tw - 4 : xh + 4;
        ctx.fillStyle = "rgba(5,7,13,0.9)"; ctx.fillRect(bx, 3, tw, 31);
        ctx.fillStyle = C.inkHi; ctx.fillText(label, bx + 6, 16);
        ctx.fillStyle = C.muted; ctx.fillText(sub, bx + 6, 29);
      }
    }

    return { setDay, draw, utcAt, phaseAt, setHover };
  }

  // ================================================================ layers: level and ring-out
  function LayersView(canvas) {
    let key = "";
    return {
      draw(levels, t60) {
        const k = levels.map((v) => v.toFixed(3)).join() + t60.map((v) => v.toFixed(2)).join() + canvas.clientWidth + UI.scale;
        if (k === key) return;
        key = k;
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const x0 = 4, x1 = w - 4, top = 6, bot = h - 14, SPAN = 30;
        const xt = (t) => x0 + (x1 - x0) * Math.sqrt(t / SPAN);
        const yd = (db) => top + (bot - top) * clamp(-db / 60, 0, 1);
        ctx.font = MONO(10); ctx.fillStyle = C.faint; ctx.strokeStyle = C.hairSoft;
        for (const t of [1, 5, 10, 30]) {
          const xx = Math.round(xt(t)) + 0.5;
          ctx.beginPath(); ctx.moveTo(xx, top); ctx.lineTo(xx, bot); ctx.stroke();
          ctx.fillText(t + (t === 30 ? " s" : ""), xx - (t === 30 ? 22 : t === 10 ? 6 : 3), h - 3);
        }
        ctx.strokeStyle = C.hair;
        ctx.beginPath(); ctx.moveTo(x0, bot + 0.5); ctx.lineTo(x1, bot + 0.5); ctx.stroke();
        // levels/t60: [red, green, violet] or [low, red, green, violet] (T3.7: low in ink, underneath)
        const names = levels.length > 3 ? ["ink"].concat(LAYERS) : LAYERS;
        names.forEach((name, l) => {
          const L0 = 20 * Math.log10(Math.max(1e-3, levels[l]));
          const db = (t) => L0 - 60 * t / t60[l];
          ctx.beginPath();
          for (let i = 0; i <= 60; i++) { const t = SPAN * (i / 60) ** 2; const y = yd(db(t)); i ? ctx.lineTo(xt(t), y) : ctx.moveTo(xt(t), y); }
          ctx.lineWidth = 1.25; ctx.strokeStyle = rgba(name, name === "ink" ? 0.55 : 0.9); ctx.stroke();
          ctx.lineTo(xt(SPAN), bot); ctx.lineTo(x0, bot); ctx.closePath();
          ctx.fillStyle = rgba(name, name === "ink" ? 0.04 : 0.07); ctx.fill();
          const tEnd = Math.min(SPAN, t60[l] * (L0 + 60) / 60);
          ctx.fillStyle = rgba(name, 1);
          ctx.beginPath(); ctx.arc(xt(tEnd), yd(-60), 1.8, 0, 2 * Math.PI); ctx.fill();
        });
      },
    };
  }

  // ================================================================ FX pictures
  function cache(canvas) {
    let key = "";
    return (k, paint) => {
      const kk = k + "|" + canvas.clientWidth + "|" + UI.scale;
      if (kk === key) return;
      key = kk;
      const { ctx, w, h } = UI.fit(canvas);
      ctx.clearRect(0, 0, w, h);
      paint(ctx, w, h);
    };
  }

  const LOGF = (f, w) => (Math.log2(f / 20) / Math.log2(1000)) * w;

  // ================================================================ the EQ (T6.14)
  /** The tone module's picture is the whole control of the EQ (the mockup's drawEq): log frequency
   *  20 Hz … 20 kHz, gain ±12 dB, the drive lane at the bottom. Behind: the spectrum, a glow over each
   *  driven band. The curve as set (thin) and as it is now (bright, filled to 0 dB: the sky's opening
   *  of the cut, later T4.2's modulation), from UI.eqDb. A numbered dot per band (drag: frequency
   *  sideways, gain up and down; scroll over a bell: its width), a drive bar under each (pull it up),
   *  the sky's reach on the top edge (drag its right end: eq_sky) with the cut now as a white dot.
   *  Each of the 14 parameters has one focusable proxy on its handle (spec/09 §5), placed here; the
   *  pointer works on the canvas. The title row reads out the band pointed at or selected. Ink only. */
  function EqView(canvas, well, say) {
    const WT = window.WT;
    const BANDS = [
      { kind: 0, name: "low shelf", f: "eq1_freq", g: "eq1_gain", q: null, d: "eq1_drive" },
      { kind: 1, name: "bell", f: "eq2_freq", g: "eq2_gain", q: "eq2_q", d: "eq2_drive" },
      { kind: 1, name: "bell", f: "eq3_freq", g: "eq3_gain", q: "eq3_q", d: "eq3_drive" },
      { kind: 2, name: "high cut", f: "eq4_freq", g: null, q: null, d: "eq4_drive" },
    ];
    const SKY = 4, OPEN = 19980, FX0 = Math.log(20), FX1 = Math.log(20000), OCT = 3;   // Sources.cpp: kEqSkyOctaves
    const P = (id) => WT.get(id);
    /** The picture's frame (the mockup's eqG): the drive lane is the bottom 32 px, 0 dB halfway above it. */
    function geo() {
      const w = canvas.clientWidth, h = canvas.clientHeight, bh = 22, by = h - 5, floor = by - bh - 5;
      const mid = 8 + (floor - 8) * 0.5, span = mid - 12;
      return {
        w, h, bh, by, floor, mid, span,
        X: (f) => (Math.log(f) - FX0) / (FX1 - FX0) * w,
        F: (x) => Math.exp(FX0 + (FX1 - FX0) * x / w),
        Y: (db) => mid - db / 12 * span,
        G: (y) => (mid - y) / span * 12,
      };
    }
    const reachOf = () => Math.min(20000, P("eq4_freq") * Math.pow(2, OCT * P("eq_sky")));
    /** Where each handle is drawn (CSS px in the canvas). */
    const dotAt = (g, k) => { const b = BANDS[k]; return { x: clamp(g.X(P(b.f)), 8, g.w - 8), y: b.kind === 2 ? g.Y(-3) : g.Y(P(b.g)) }; };
    const skyAt = (g) => ({ x: clamp(g.X(reachOf()), 4, g.w - 4), y: 5 });

    let sel = 1, hot = null, drag = null, field = null, sig = "", said = "", hinted = "";
    let tel = {}, live = {};

    // ---- the proxies: one focusable element per parameter, on its handle (spec/09 §5)
    const NAMES = ["Band 1, low shelf", "Band 2, bell", "Band 3, bell", "Band 4, high cut"];
    const proxies = [];
    function proxy(id, role, k, label, hint, tab, keys) {
      const e = UI.el("div", "eq-proxy eq-" + role, well);
      e.dataset.param = id;
      e.dataset.hint = hint;
      e.dataset.role = role;
      e.dataset.band = k;
      if (keys) e.dataset.keys = keys;   // the keys that move this one (the UI check presses them)
      e.tabIndex = tab ? 0 : -1;
      e.setAttribute("role", "slider");
      e.setAttribute("aria-label", label);
      e.setAttribute("aria-valuemin", String(WT.META[id].min));
      e.setAttribute("aria-valuemax", String(WT.META[id].max));
      WT.onParam(id, (v) => {
        e.setAttribute("aria-valuenow", String(v));
        e.setAttribute("aria-valuetext", UI.format(id, v));
      });
      e.addEventListener("focus", () => { sel = k; });
      proxies.push(e);
      return e;
    }
    BANDS.forEach((b, k) => {
      const keysHint = b.kind === 2 ? "arrows move where the highs are cut; at the top it is open."
        : `← → frequency, ↑ ↓ gain${b.q ? ", [ ] width" : ""}.`;
      proxy(b.f, "dot", k, NAMES[k] + ", frequency", `${NAMES[k]}: ${keysHint}`, true, "ArrowRight,ArrowLeft");
      if (b.g) proxy(b.g, "dot", k, NAMES[k] + ", gain", UI.hintOf(b.g), false);
      if (b.q) proxy(b.q, "dot", k, NAMES[k] + ", width", UI.hintOf(b.q), false, "],[");
      proxy(b.d, "bar", k, NAMES[k] + ", drive", UI.hintOf(b.d), true);
    });
    proxy("eq_sky", "sky", SKY, "The sky's opening of the cut", UI.hintOf("eq_sky"), true);
    const proxyOf = (role, k) => proxies.find((e) => e.dataset.role === role && +e.dataset.band === k);
    let placed = "";
    function place(g) {
      const pos = proxies.map((e) => {
        const k = +e.dataset.band, role = e.dataset.role;
        const p = role === "sky" ? skyAt(g) : dotAt(g, k);
        return role === "bar" ? [p.x - 6.5, g.by - g.bh - 2] : role === "sky" ? [p.x - 7, p.y - 7] : [p.x - 9, p.y - 9];
      });
      const s = pos.map((p) => p[0].toFixed(1) + "," + p[1].toFixed(1)).join(" ");
      if (s === placed) return;
      placed = s;
      proxies.forEach((e, i) => { e.style.left = pos[i][0].toFixed(1) + "px"; e.style.top = pos[i][1].toFixed(1) + "px"; });
    }

    // ---- the keyboard: on a dot, ← → frequency, ↑ ↓ gain (the cut: frequency), [ ] width; on a bar
    // or the sky's end, the arrows; on any, Shift fine, Home/End, Backspace the default, Enter to type
    well.addEventListener("keydown", (e) => {
      const t = e.target;
      if (!t.classList.contains("eq-proxy")) return;
      const id = t.dataset.param, k = +t.dataset.band;
      if (e.key === "Enter") { e.preventDefault(); openField(t, id); return; }
      if (t.dataset.role === "dot") {
        const b = BANDS[k];
        let to = null, key = e.key;
        if (key === "ArrowLeft" || key === "ArrowRight") to = b.f;
        else if (key === "ArrowUp" || key === "ArrowDown") to = b.g || b.f;
        else if ((key === "[" || key === "]") && b.q) { to = b.q; key = key === "]" ? "ArrowUp" : "ArrowDown"; }
        if (to) { e.preventDefault(); UI.sliderKey(to, { key, shiftKey: e.shiftKey }); return; }
      }
      if (UI.sliderKey(id, e)) e.preventDefault();
    });
    function openField(t, id) {
      if (field) return;
      const f = (field = UI.el("input", "knob-field eq-field", well));
      f.type = "text";
      f.value = UI.format(id, P(id));
      f.spellcheck = false;
      f.setAttribute("aria-label", t.getAttribute("aria-label") + ", type a value");
      const top = parseFloat(t.style.top) || 0;
      f.style.left = clamp((parseFloat(t.style.left) || 0) + t.offsetWidth / 2, 40, canvas.clientWidth - 40) + "px";
      f.style.top = (top > 30 ? top - 22 : top + t.offsetHeight + 4) + "px";
      const close = (commit) => {
        if (field !== f) return;
        field = null;
        if (commit) { const v = UI.parse(id, f.value); if (v !== null && isFinite(v)) UI.commit(id, v); }
        f.remove();
        t.focus({ preventScroll: true });
      };
      f.addEventListener("keydown", (e) => {
        e.stopPropagation();
        if (e.key === "Enter") { e.preventDefault(); close(true); } else if (e.key === "Escape") { e.preventDefault(); close(false); }
      });
      f.addEventListener("blur", () => close(false));
      f.focus({ preventScroll: true });
      f.select();
    }

    // ---- the pointer
    const local = (e) => {
      const r = canvas.getBoundingClientRect(), k = r.width / Math.max(1, canvas.clientWidth);
      return { x: (e.clientX - r.left) / k, y: (e.clientY - r.top) / k };
    };
    /** The handle under p: a dot (nearest within 11 px), else a drive bar (padded to 24 px), else the sky's end. */
    function hitAt(p) {
      const g = geo();
      let best = null, dBest = 11;
      for (let k = 0; k < 4; k++) {
        const d = dotAt(g, k), dist = Math.hypot(p.x - d.x, p.y - d.y);
        if (dist < dBest || (dist === dBest && k === sel)) { best = { role: "dot", k }; dBest = dist; }
      }
      if (best) return best;
      if (p.y > g.by - g.bh - 4) {
        let dx = 12;
        for (let k = 0; k < 4; k++) { const d = Math.abs(p.x - dotAt(g, k).x); if (d < dx) { dx = d; best = { role: "bar", k }; } }
        if (best) return best;
      }
      const s = skyAt(g);
      if (P("eq4_freq") < OPEN && Math.abs(p.x - s.x) < 12 && p.y < s.y + 10) return { role: "sky", k: SKY };
      return null;
    }
    const HINT = "Four bands, each with its own gain and drive. The top edge: how far the sky opens the cut.";
    canvas.dataset.hint = HINT;
    function hintFor(h) {
      if (!h) return HINT;
      const b = BANDS[h.k];
      if (h.role === "dot") return `Band ${h.k + 1}, ${b.name}: drag sideways for frequency${b.kind === 2 ? "" : ", up and down for gain"}${b.q ? ", scroll for width" : ""}.`;
      if (h.role === "bar") return `Drive for band ${h.k + 1}: pull it up to saturate only this part of the sound.`;
      return "Drag the end: how far the sky's activity opens the cut, up to three octaves.";
    }
    function setHint(text) {
      if (text === hinted) return;
      hinted = text;
      const n = document.getElementById("hint");
      if (n && n.textContent !== text) n.textContent = text;
    }
    canvas.addEventListener("pointermove", (e) => {
      if (drag) { drag.move(local(e), e.shiftKey); return; }
      const h = hitAt(local(e));
      hot = h;
      canvas.style.cursor = !h ? "default" : h.role === "dot" ? "grab" : h.role === "bar" ? "ns-resize" : "ew-resize";
      setHint(hintFor(h));
    });
    canvas.addEventListener("pointerleave", () => { if (!drag) hot = null; hinted = ""; });
    canvas.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      const p = local(e), h = hitAt(p);
      if (!h) return;
      e.preventDefault();
      try { canvas.setPointerCapture(e.pointerId); } catch (err) { /* a synthetic pointer (the UI check) */ }
      const px = proxyOf(h.role, h.k);
      if (px) px.focus({ preventScroll: true });
      sel = h.k;
      hot = h;
      const g = geo(), b = BANDS[h.k];
      let last = p;
      if (h.role === "dot") {
        // relative: the dot keeps its offset from the pointer; Shift for fine
        const m = WT.META[b.f], ids = b.g ? [b.f, b.g] : [b.f];
        const run = UI.drag(ids);
        let vx = g.X(P(b.f)), vy = b.g ? g.Y(P(b.g)) : 0;
        canvas.style.cursor = "grabbing";
        drag = {
          run,
          move(q, fine) {
            const k = fine ? 0.15 : 1;
            vx = clamp(vx + (q.x - last.x) * k, g.X(m.min), g.X(m.max) + 8);
            vy = clamp(vy + (q.y - last.y) * k, g.Y(12), g.Y(-12));
            last = q;
            run.set(b.f, b.kind === 2 && vx >= g.w - 6 ? 20000 : g.F(vx));   // the cut opens at the right edge
            if (b.g) run.set(b.g, Math.round(g.G(vy) * 10) / 10);
          },
        };
      } else if (h.role === "bar") {
        // absolute, a fader: the bar's top follows the pointer
        const run = UI.drag([b.d]), set = (q) => run.set(b.d, clamp((g.by - q.y) / g.bh, 0, 1));
        set(p);
        drag = { run, move: set };
      } else {
        // relative, in octaves of reach; past the edge too (the reach is drawn up to 20 kHz)
        const run = UI.drag(["eq_sky"]), cut = P("eq4_freq");
        let vx = g.X(cut * Math.pow(2, OCT * P("eq_sky")));
        drag = {
          run,
          move(q, fine) {
            vx = clamp(vx + (q.x - last.x) * (fine ? 0.15 : 1), g.X(cut), g.X(cut * Math.pow(2, OCT)));
            last = q;
            run.set("eq_sky", Math.log2(g.F(vx) / cut) / OCT);
          },
        };
      }
    });
    const release = () => {
      if (!drag) return;
      drag.run.end();
      drag = null;
      canvas.style.cursor = hot ? (hot.role === "dot" ? "grab" : hot.role === "bar" ? "ns-resize" : "ew-resize") : "default";
    };
    canvas.addEventListener("pointerup", release);
    canvas.addEventListener("pointercancel", release);
    canvas.addEventListener("lostpointercapture", release);
    canvas.addEventListener("dblclick", (e) => {
      const h = hitAt(local(e));
      if (!h) return;
      const b = BANDS[h.k];
      const id = h.role === "sky" ? "eq_sky" : h.role === "bar" ? b.d : b.g || b.f;   // a dot: its gain (the cut: open)
      UI.commit(id, UI.defaultValue(id));
    });
    // scroll over a bell (or with a bell selected): its width
    canvas.addEventListener("wheel", (e) => {
      const h = hitAt(local(e)), k = h && h.role === "dot" ? h.k : sel;
      const b = BANDS[k];
      if (!b || !b.q) return;
      e.preventDefault();
      UI.step(b.q, () => WT.set(b.q, P(b.q) * Math.exp(clamp(-e.deltaY, -400, 400) * (e.shiftKey ? 0.0002 : 0.0011))));
    }, { passive: false });

    // ---- the read-out in the title row: the band pointed at, dragged or selected
    const hz = (f) => (f >= OPEN ? "open" : f >= 1000 ? (f / 1000).toFixed(f >= 10000 ? 1 : 2) + " kHz" : Math.round(f) + " Hz");
    function readout() {
      const k = drag ? sel : hot ? hot.k : sel;
      if (k === SKY) {
        const now = live.eq4_freq;
        return `sky <b>+${(OCT * P("eq_sky")).toFixed(1)} oct</b> cut now <b>${hz(now)}</b>`;
      }
      const b = BANDS[k], gain = b.g ? P(b.g) : 0;
      return `band <b>${k + 1}</b> ${b.name} <b>${hz(P(b.f))}</b>`
        + (b.g ? ` <b>${gain >= 0 ? "+" : "−"}${Math.abs(gain).toFixed(1)} dB</b>` : "")
        + (b.q ? ` Q <b>${P(b.q).toFixed(2)}</b>` : "")
        + ` drive <b>${Math.round(P(b.d) * 100)} %</b>`;
    }

    // ---- drawing
    function draw(t, extra) {
      tel = t || {};
      const fs = tel.sampleRate || 48000;
      // the values now: the sky's cut (T3.9) and, from T4.2, whatever modulation moves
      live = Object.assign({ eq4_freq: tel.eqCutHz || P("eq4_freq") }, extra || {});
      const g = geo(), w = g.w, h = g.h;
      const ids = ["eq1_freq", "eq1_gain", "eq1_drive", "eq2_freq", "eq2_gain", "eq2_q", "eq2_drive", "eq3_freq", "eq3_gain", "eq3_q", "eq3_drive", "eq4_freq", "eq4_drive", "eq_sky"];
      const s = ids.map(P).join() + JSON.stringify(live) + (hot ? hot.role + hot.k : "") + sel + !!drag + w + "x" + h + UI.scale
        + (tel.spectrumIn || tel.spectrum || []).join();
      const text = readout();
      if (text !== said) { said = text; say.innerHTML = text; }
      place(g);
      if (s === sig) return;
      sig = s;
      const { ctx } = UI.fit(canvas);
      const c = ctx;
      c.clearRect(0, 0, w, h);
      // the grid: 100, 1k, 10k, and 0 dB
      c.font = MONO(10); c.textAlign = "left"; c.textBaseline = "alphabetic";
      [100, 1000, 10000].forEach((f, i) => {
        const x = Math.round(g.X(f)) + 0.5;
        c.strokeStyle = C.hairSoft; c.lineWidth = 1;
        c.beginPath(); c.moveTo(x, 0); c.lineTo(x, g.floor); c.stroke();
        c.fillStyle = C.faint; c.fillText(["100", "1k", "10k"][i], x + 4, 13);
      });
      c.strokeStyle = "rgba(201,211,230,0.16)";
      c.beginPath(); c.moveTo(0, Math.round(g.mid) + 0.5); c.lineTo(w, Math.round(g.mid) + 0.5); c.stroke();

      // behind: the sound. T6.12's pre-EQ spectrum shaped by the curve now; until then the output's,
      // which the EQ has already shaped
      const spec = tel.spectrumIn || tel.spectrum;
      if (spec && spec.length) {
        const n = spec.length, top = g.floor - (g.floor - 12) * 0.82;
        c.beginPath(); c.moveTo(0, g.floor);
        for (let i = 0; i < n; i++) {
          const x = (i + 0.5) / n * w, db = spec[i] + (tel.spectrumIn ? UI.eqDb(g.F(x), live, fs) : 0);
          c.lineTo(i ? x : 0, g.floor - (g.floor - top) * clamp((db + 90) / 80, 0, 1));
        }
        c.lineTo(w, g.floor); c.closePath(); c.fillStyle = "rgba(201,211,230,0.11)"; c.fill();
      }
      // drive: a glow rising from the lane under each driven band
      BANDS.forEach((b, k) => {
        const dr = clamp(b.d in live ? live[b.d] : P(b.d), 0, 1);
        if (dr < 0.01) return;
        const x = clamp(g.X(b.f in live ? live[b.f] : P(b.f)), 8, w - 8);
        const rg = c.createRadialGradient(x, g.floor, 2, x, g.floor, 24 + 52 * dr);
        rg.addColorStop(0, `rgba(238,242,250,${(0.1 + 0.3 * dr).toFixed(3)})`); rg.addColorStop(1, "rgba(238,242,250,0)");
        c.fillStyle = rg; c.fillRect(x - 80, 0, 160, g.floor);
      });
      // the curve: as set (thin) and as it is now (bright, filled to 0 dB)
      const path = (p) => {
        c.beginPath();
        for (let x = 0; x <= w; x += 2) { const y = clamp(g.Y(UI.eqDb(g.F(x), p, fs)), 2, g.floor); if (x) c.lineTo(x, y); else c.moveTo(x, y); }
      };
      path(live); c.lineTo(w, g.mid); c.lineTo(0, g.mid); c.closePath(); c.fillStyle = "rgba(201,211,230,0.13)"; c.fill();
      path(null); c.strokeStyle = "rgba(201,211,230,0.32)"; c.lineWidth = 1; c.stroke();
      path(live); c.strokeStyle = C.inkHi; c.lineWidth = 1.8; c.stroke();

      // the sky's reach on the top edge: from the cut as set to the cut at full activity; its end is
      // the handle for eq_sky; the white dot is the cut now
      const cut = P("eq4_freq");
      if (cut < OPEN) {
        const x0 = g.X(cut), sk = skyAt(g), on = sel === SKY || (hot && hot.role === "sky");
        c.fillStyle = "rgba(201,211,230,0.38)"; c.fillRect(x0, 4, Math.max(0, sk.x - x0), 2);
        c.fillStyle = C.night; c.beginPath(); c.arc(sk.x, sk.y, 3.5, 0, 2 * Math.PI); c.fill();
        c.strokeStyle = on ? "#fff" : C.ink; c.lineWidth = on ? 1.6 : 1.3; c.stroke();
        c.fillStyle = "#fff"; c.beginPath(); c.arc(clamp(g.X(live.eq4_freq), 3, w - 3), 5, 2.6, 0, 2 * Math.PI); c.fill();
      }

      // the dots and the drive bars
      c.fillStyle = C.faint; c.font = UI.SANS(11); c.textAlign = "left"; c.textBaseline = "middle";
      c.fillText("drive", 6, g.by - g.bh / 2);
      BANDS.forEach((b, k) => {
        const d = dotAt(g, k), on = k === sel, hh = hot && hot.k === k;
        // what moves it: a white dot beside the handle, joined by a hairline (spec/09 rule 3)
        const xl = b.f in live ? clamp(g.X(live[b.f]), 8, w - 8) : d.x, yl = b.g && b.g in live ? g.Y(live[b.g]) : d.y;
        if (Math.abs(xl - d.x) + Math.abs(yl - d.y) > 1.5) {
          c.strokeStyle = "rgba(255,255,255,0.4)"; c.lineWidth = 1;
          c.beginPath(); c.moveTo(d.x, d.y); c.lineTo(xl, yl); c.stroke();
          c.fillStyle = "#fff"; c.beginPath(); c.arc(xl, yl, 2.6, 0, 2 * Math.PI); c.fill();
        }
        c.fillStyle = C.night; c.beginPath(); c.arc(d.x, d.y, on || (hh && hot.role === "dot") ? 8 : 7, 0, 2 * Math.PI); c.fill();
        c.strokeStyle = on ? "#fff" : C.ink; c.lineWidth = on ? 2 : 1.6; c.stroke();
        c.fillStyle = on ? "#fff" : C.ink; c.font = UI.SANS(10, 600); c.textAlign = "center"; c.textBaseline = "middle";
        c.fillText(String(k + 1), d.x, d.y + 0.5);
        const top = g.by - g.bh, fill = g.bh * clamp(P(b.d), 0, 1);
        c.fillStyle = hh && hot.role === "bar" ? "#283248" : "#182031";
        c.beginPath(); c.roundRect(d.x - 4.5, top, 9, g.bh, 3); c.fill();
        if (fill > 0.5) { c.fillStyle = on ? "#fff" : C.arc; c.beginPath(); c.roundRect(d.x - 4.5, g.by - fill, 9, fill, 3); c.fill(); }
      });
      c.textAlign = "left"; c.textBaseline = "alphabetic";
    }

    return {
      draw,
      /** For the UI check: where a handle is, in client px. role: dot, bar (its bottom), sky. */
      at(role, k) {
        const g = geo(), r = canvas.getBoundingClientRect(), s = r.width / Math.max(1, g.w);
        const p = role === "sky" ? skyAt(g) : role === "bar" ? { x: dotAt(g, k).x, y: g.by - 1 } : dotAt(g, k);
        return { x: r.left + p.x * s, y: r.top + p.y * s, scale: s, bh: g.bh, span: g.span, w: g.w };
      },
    };
  }

  /** The notes sounding, as keys from C1 to B6 (T6.8, the mockup's drawKbd; it replaces T6.1's voicing
   *  lanes and T3.7's Play keyboard). `lit` maps a MIDI note to [band, strength 0…1], band one of
   *  "ink" (the low resonator), "red", "green", "violet": in Drone mode the chord's voices, in Play
   *  mode the keys you hold and their release. Every key eases in and out (CLAUDE.md rule 4). */
  function KeyboardView(canvas) {
    const LO = 24, HI = 96, TAU = 0.3;
    const WHITE = [0, 2, 4, 5, 7, 9, 11];
    const keys = new Map();   // note → { band, a: shown, t: target }
    let at = 0, shown = "";
    const smooth = (a) => Math.min(1, a / 0.15);
    function draw(lit) {
      const t = now(), e = at ? 1 - Math.exp(-Math.min(0.25, t - at) / TAU) : 1;
      at = t;
      for (const [n, [band, v]] of lit) {
        const k = keys.get(n);
        if (k) { k.band = band; k.t = v; } else keys.set(n, { band, a: 0, t: v });
      }
      let sig = canvas.clientWidth + "|" + UI.scale;
      for (const [n, k] of keys) {
        if (!lit.has(n)) k.t = 0;
        k.a += (k.t - k.a) * e;
        if (k.t === 0 && k.a < 0.01) { keys.delete(n); continue; }
        sig += `|${n}${k.band[0]}${Math.round(k.a * 60)}`;
      }
      if (sig === shown) return;
      shown = sig;
      const { ctx, w, h } = UI.fit(canvas);
      ctx.clearRect(0, 0, w, h);
      let whites = 0;
      for (let m = LO; m < HI; m++) if (WHITE.includes(m % 12)) whites++;
      const ww = (w - 12) / whites, kh = h - 12, bh = kh * 0.6;
      const lift = (m, base, top) => {
        const k = keys.get(m);
        if (!k || k.a < 0.005) return;
        ctx.fillStyle = rgba(k.band, (base + top * k.a) * smooth(k.a));
        return true;
      };
      let i = 0;
      for (let m = LO; m < HI; m++) {
        if (!WHITE.includes(m % 12)) continue;
        const x = 6 + i * ww;
        ctx.fillStyle = "#141a28";
        ctx.beginPath(); ctx.roundRect(x + 0.5, 6, ww - 1, kh, 2); ctx.fill();
        const on = lift(m, 0.35, 0.6);
        if (on) { ctx.beginPath(); ctx.roundRect(x + 0.5, 6, ww - 1, kh, 2); ctx.fill(); }
        if (m % 12 === 0) {
          const k = keys.get(m);
          ctx.fillStyle = k && k.a > 0.4 ? "#05070d" : C.faint;
          ctx.font = MONO(10); ctx.textAlign = "center"; ctx.textBaseline = "alphabetic";
          ctx.fillText("C" + (m / 12 - 1), x + ww / 2, h - 9);
        }
        i++;
      }
      i = 0;
      for (let m = LO; m < HI; m++) {
        if (WHITE.includes(m % 12)) { i++; continue; }
        const x = 6 + i * ww - ww * 0.3;
        ctx.fillStyle = C.well; ctx.fillRect(x - 1, 6, ww * 0.6 + 2, bh + 1);
        ctx.fillStyle = "#232b3d";
        ctx.beginPath(); ctx.roundRect(x, 6, ww * 0.6, bh, 1.5); ctx.fill();
        if (lift(m, 0.5, 0.5)) { ctx.beginPath(); ctx.roundRect(x, 6, ww * 0.6, bh, 1.5); ctx.fill(); }
      }
      ctx.textAlign = "left";
    }
    return { draw };
  }

  function PhaserView(canvas) {
    return {
      draw(t, p, spectrum) {
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const top = 3, bot = h - 3;
        if (spectrum && spectrum.length) {
          ctx.beginPath(); ctx.moveTo(0, bot);
          spectrum.forEach((db, i) => ctx.lineTo((i / (spectrum.length - 1)) * w, bot - clamp((db + 90) / 80, 0, 1) * (bot - top)));
          ctx.lineTo(w, bot); ctx.closePath(); ctx.fillStyle = "rgba(201,211,230,0.07)"; ctx.fill();
        }
        for (const f of [100, 1000, 10000]) { const xx = Math.round(LOGF(f, w)) + 0.5; ctx.strokeStyle = C.hairSoft; ctx.beginPath(); ctx.moveTo(xx, top); ctx.lineTo(xx, bot); ctx.stroke(); }
        const notches = Math.max(1, Math.round(p.stages / 2));
        const sweep = 0.5 + 0.5 * p.depth * Math.sin(2 * Math.PI * p.rate * t);
        const c0 = Math.log2(120) + sweep * Math.log2(30), width = 0.55 - 0.4 * p.fb;
        ctx.beginPath();
        for (let i = 0; i <= 120; i++) {
          const x = (i / 120) * w, lf = Math.log2(20) + (i / 120) * Math.log2(1000);
          let r = 1;
          for (let k = 0; k < notches; k++) {
            const c = c0 + k * 1.3;
            r *= 1 - 0.92 * p.mix * Math.exp(-(((lf - c) / width) ** 2));
            r += 0.35 * p.fb * p.mix * Math.exp(-(((lf - c - 0.65) / width) ** 2));
          }
          const y = top + (bot - top) * (1 - clamp(r, 0, 1.3) / 1.3);
          i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
        }
        ctx.strokeStyle = C.inkHi; ctx.lineWidth = 1.4; ctx.stroke();
      },
    };
  }

  function ChorusView(canvas) {
    return {
      draw(t, p) {
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const mid = h / 2, amp = (p.depth / 10) * (h / 2 - 5) + 1;
        ctx.strokeStyle = C.hair; ctx.beginPath(); ctx.moveTo(0, mid + 0.5); ctx.lineTo(w, mid + 0.5); ctx.stroke();
        for (let v = 0; v < 3; v++) {
          ctx.beginPath();
          for (let i = 0; i <= 48; i++) {
            const x = (i / 48) * w;
            const y = mid + amp * Math.sin(2 * Math.PI * (p.rate * t + (i / 48) * 0.9 + v / 3));
            i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
          }
          ctx.strokeStyle = rgba("hi", 0.25 + 0.65 * p.mix * (1 - v * 0.22));
          ctx.lineWidth = 1.3; ctx.stroke();
        }
      },
    };
  }

  function DelayView(canvas) {
    const paint = cache(canvas);
    return {
      draw(p) {
        paint([p.time, p.fb, p.tone, p.mix, p.ping].map((v) => (+v).toFixed(3)).join(), (ctx, w, h) => {
          const mid = h / 2, x0 = 6, x1 = w - 14, span = Math.max(2, Math.min(16, p.time * 7));
          const x = (t) => x0 + (x1 - x0) * (t / span);
          ctx.strokeStyle = C.hair; ctx.beginPath(); ctx.moveTo(x0, mid + 0.5); ctx.lineTo(x1, mid + 0.5); ctx.stroke();
          ctx.font = MONO(10); ctx.fillStyle = C.faint; ctx.fillText("L", w - 11, 12); ctx.fillText("R", w - 11, h - 4);
          ctx.strokeStyle = C.inkHi; ctx.lineWidth = 2;
          ctx.beginPath(); ctx.moveTo(x(0) + 0.5, mid - (mid - 4)); ctx.lineTo(x(0) + 0.5, mid + (mid - 4)); ctx.stroke();
          let a = 1;
          for (let n = 1; n * p.time <= span && a > 0.02; n++) {
            const xx = Math.round(x(n * p.time)) + 0.5, len = (mid - 4) * a;
            ctx.strokeStyle = rgba("hi", clamp(0.35 + 0.65 * p.mix, 0, 1) * (0.4 + 0.6 * a));
            ctx.lineWidth = 2;
            ctx.beginPath();
            if (p.ping) { const up = n % 2 === 1; ctx.moveTo(xx, mid); ctx.lineTo(xx, up ? mid - len : mid + len); }
            else { ctx.moveTo(xx, mid - len); ctx.lineTo(xx, mid + len); }
            ctx.stroke();
            a *= p.fb * (0.75 + 0.25 * p.tone);
          }
        });
      },
    };
  }

  function ReverbView(canvas) {
    const paint = cache(canvas);
    return {
      draw(p, wet) {
        paint([p.size, p.decay, p.damp, p.pre, p.shimmer, p.mix].map((v) => v.toFixed(3)).join() + (wet * 30 | 0), (ctx, w, h) => {
          const x0 = 4, x1 = w - 12, top = 4, bot = h - 4, span = Math.max(1.5, p.decay * 1.15 + p.pre / 1000);
          const x = (t) => x0 + (x1 - x0) * (t / span);
          const y = (db) => top + (bot - top) * clamp(-db / 60, 0, 1);
          const t0 = p.pre / 1000;
          ctx.strokeStyle = C.hairSoft;
          for (let s = 1; s < span; s *= 2) { const xx = Math.round(x(s)) + 0.5; ctx.beginPath(); ctx.moveTo(xx, top); ctx.lineTo(xx, bot); ctx.stroke(); }
          // early reflections: denser with size
          const er = 6 + Math.round(18 * p.size);
          ctx.strokeStyle = "rgba(201,211,230,0.35)"; ctx.lineWidth = 1;
          for (let i = 0; i < er; i++) {
            const tt = t0 + (0.004 + 0.09 * (0.4 + p.size) * (i / er) ** 1.4), yy = y(-6 - 30 * (i / er));
            const xx = Math.round(x(tt)) + 0.5;
            ctx.beginPath(); ctx.moveTo(xx, bot); ctx.lineTo(xx, yy); ctx.stroke();
          }
          const tail = (t60, color, fill) => {
            ctx.beginPath(); ctx.moveTo(x(t0), y(0));
            ctx.lineTo(x(t0 + t60), y(-60));
            ctx.strokeStyle = color; ctx.lineWidth = 1.25; ctx.stroke();
            if (fill) { ctx.lineTo(x(t0), bot); ctx.closePath(); ctx.fillStyle = fill; ctx.fill(); }
          };
          tail(p.decay, "rgba(201,211,230,0.55)", `rgba(201,211,230,${0.05 + 0.1 * p.mix})`);
          tail(p.decay * (1 - 0.75 * p.damp), C.inkHi, null);
          // shimmer: the octave and the fifth, rising out of the tail
          const sh = Math.round(14 * p.shimmer);
          for (let i = 0; i < sh; i++) {
            const tt = t0 + span * (0.1 + 0.8 * ((i * 0.618) % 1)), yy = y(-12 - 40 * ((i * 0.381) % 1));
            ctx.fillStyle = rgba("hi", 0.35 + 0.55 * p.shimmer);
            ctx.beginPath(); ctx.arc(x(tt), yy, 1.1, 0, 2 * Math.PI); ctx.fill();
          }
          // live wet level
          ctx.fillStyle = C.hair; ctx.fillRect(w - 5, top, 2, bot - top);
          const lv = clamp(wet * 2, 0, 1);
          ctx.fillStyle = C.inkHi; ctx.fillRect(w - 5, bot - lv * (bot - top), 2, lv * (bot - top));
        });
      },
    };
  }

  const dbOf = (g) => (g > 1e-5 ? 20 * Math.log10(g) : -100);

  /** Output (T6.8: upright, the mockup's drawOut, for the 98 px module): the two channels' peaks, the
   *  limiter's ceiling as a white line, and its gain reduction as a thin bar hanging from it. */
  function OutputView(canvas) {
    return {
      draw(peakL, peakR, gr, ceiling) {
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const MIN = -48, top = 10, bot = h - 16, x1 = w - 30;
        const Y = (db) => top + (bot - top) * clamp(db / MIN, 0, 1);
        ctx.font = MONO(10); ctx.textAlign = "right"; ctx.textBaseline = "middle";
        for (const db of [0, -12, -24, -36]) {
          ctx.fillStyle = C.faint; ctx.fillText(String(db).replace("-", "−"), w - 4, Y(db));
          ctx.fillStyle = C.hairSoft; ctx.fillRect(8, Math.round(Y(db)), x1 - 8, 1);
        }
        const grad = ctx.createLinearGradient(0, Y(0), 0, Y(MIN));
        grad.addColorStop(0, C.inkHi); grad.addColorStop(1, "rgba(201,211,230,0.35)");
        [peakL, peakR].forEach((g, ch) => {
          const x = 12 + ch * 18, y = Y(clamp(dbOf(g), MIN, 0));
          ctx.fillStyle = C.track; ctx.fillRect(x, Y(0), 12, Y(MIN) - Y(0));
          ctx.fillStyle = grad; ctx.fillRect(x, y, 12, Y(MIN) - y);
        });
        const yc = Math.round(Y(ceiling));
        ctx.fillStyle = "#fff"; ctx.fillRect(8, yc - 1, x1 - 8, 1.5);
        const grDb = -dbOf(gr);
        if (grDb > 0.05) { ctx.fillStyle = rgba("hi", 0.8); ctx.fillRect(x1 - 4, yc, 2, Math.min(bot, Y(ceiling - grDb)) - yc); }
        ctx.fillStyle = C.ink; ctx.textAlign = "center"; ctx.textBaseline = "alphabetic";
        ctx.fillText("L", 18, h - 4); ctx.fillText("R", 36, h - 4);
        ctx.textAlign = "left";
      },
    };
  }

  /** The top bar's meter (120 × 22): two rounded bars, a peak hold each, the ceiling as a tick. */
  function HeaderMeter(canvas) {
    let hold = [0, 0], holdT = [0, 0];
    return {
      draw(peakL, peakR, ceiling) {
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const MIN = -48, x = (db) => w * clamp((db - MIN) / -MIN, 0, 1), t = now();
        [peakL, peakR].forEach((g, i) => {
          const y = Math.round(h / 2) - 6 + i * 8, db = dbOf(g);
          if (db > hold[i] || t - holdT[i] > 1.5) { hold[i] = db; holdT[i] = t; }
          ctx.fillStyle = C.track; ctx.beginPath(); ctx.roundRect(0, y, w, 4, 2); ctx.fill();
          ctx.fillStyle = db > ceiling - 0.5 ? "#fff" : C.arc; ctx.beginPath(); ctx.roundRect(0, y, Math.max(0.5, x(db)), 4, 2); ctx.fill();
          ctx.fillStyle = C.inkHi; ctx.fillRect(Math.max(0, x(hold[i]) - 1), y - 1, 1, 6);
        });
        ctx.fillStyle = "#fff"; ctx.fillRect(Math.round(x(ceiling)), 3, 1.5, h - 6);
      },
    };
  }

  Object.assign(UI, { MagnetogramView, LayersView, EqView, KeyboardView, PhaserView, ChorusView, DelayView, ReverbView, OutputView, HeaderMeter, dbOf });
})();
