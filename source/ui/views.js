/* Canvas visualisations. Each view exposes draw(state); app.js calls them per frame. */
(function () {
  "use strict";
  const WT = window.WT, UI = window.UI, C = UI.COLORS;

  const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
  const lerp = (a, b, t) => a + (b - a) * t;

  function glow(ctx, color, blur) { ctx.shadowColor = color; ctx.shadowBlur = blur; }
  function noGlow(ctx) { ctx.shadowBlur = 0; ctx.shadowColor = "transparent"; }

  function fmtTime(utc, withDate) {
    const d = new Date(utc * 1000);
    const hh = String(d.getUTCHours()).padStart(2, "0"), mm = String(d.getUTCMinutes()).padStart(2, "0");
    if (!withDate) return `${hh}:${mm}`;
    const mon = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"][d.getUTCMonth()];
    return `${d.getUTCDate()} ${mon} ${hh}:${mm}`;
  }
  UI.fmtTime = fmtTime;

  /** Interpolated row of a frames×n array at fractional frame position. */
  function rowAt(arr, frames, n, fpos) {
    const f0 = clamp(Math.floor(fpos), 0, frames - 1), f1 = Math.min(f0 + 1, frames - 1), t = fpos - f0;
    const out = new Array(n);
    for (let i = 0; i < n; i++) out[i] = lerp(arr[f0 * n + i], arr[f1 * n + i], t);
    return out;
  }

  function emptyMessage(ctx, w, h, text) {
    ctx.fillStyle = C.faint;
    ctx.font = "10px -apple-system, sans-serif";
    ctx.textAlign = "center";
    ctx.fillText(text, w / 2, h / 2);
    ctx.textAlign = "left";
  }

  // ======================================================================
  // ③ Wavetable — 3D stack, newest frame in front, the past receding
  // ======================================================================
  function WavetableView(canvas) {
    const off = document.createElement("canvas");
    let key = "";

    function geom(w, h) {
      return { x0: w * 0.05, base: h * 0.74, width: w * 0.60, amp: h * 0.17, dx: w * 0.31, dy: -h * 0.50 };
    }
    function pt(g, z, i, n, v) {
      const s = 1 - 0.2 * z;
      return [g.x0 + g.dx * z + (i / (n - 1)) * g.width * s, g.base + g.dy * z - v * g.amp * (1 - 0.3 * z)];
    }

    function renderStatic(t, w, h) {
      const k = UI.dpr * UI.scale;
      off.width = Math.round(w * k); off.height = Math.round(h * k);
      const ctx = off.getContext("2d");
      ctx.setTransform(k, 0, 0, k, 0, 0);
      const g = geom(w, h), F = t.frames, n = t.wavePoints;

      // floor plane
      ctx.strokeStyle = C.grid; ctx.lineWidth = 1;
      for (const zz of [0, 0.25, 0.5, 0.75, 1]) {
        const [ax, ay] = pt(g, zz, 0, n, 0), [bx, by] = pt(g, zz, n - 1, n, 0);
        ctx.beginPath(); ctx.moveTo(ax, ay); ctx.lineTo(bx, by); ctx.stroke();
      }
      for (const ii of [0, n - 1]) {
        const [ax, ay] = pt(g, 0, ii, n, 0), [bx, by] = pt(g, 1, ii, n, 0);
        ctx.beginPath(); ctx.moveTo(ax, ay); ctx.lineTo(bx, by); ctx.stroke();
      }

      for (let f = 0; f < F; f++) {
        const z = F > 1 ? 1 - f / (F - 1) : 0;
        const a = 0.07 + 0.30 * Math.pow(1 - z, 1.6) + (f % 8 === 0 ? 0.06 : 0);
        ctx.strokeStyle = `rgba(95,184,255,${a.toFixed(3)})`;
        ctx.lineWidth = f % 8 === 0 ? 1.1 : 0.8;
        ctx.beginPath();
        for (let i = 0; i < n; i++) {
          const [x, y] = pt(g, z, i, n, t.wave[f * n + i]);
          i ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
        }
        ctx.stroke();
      }

      // time ticks along the right edge of the floor
      ctx.font = "9px -apple-system, sans-serif";
      ctx.fillStyle = C.faint;
      const newest = t.times[F - 1];
      for (let f = F - 1; f >= 0; f -= 16) {
        const z = F > 1 ? 1 - f / (F - 1) : 0;
        const [x, y] = pt(g, z, n - 1, n, 0);
        const hrs = Math.round((newest - t.times[f]) / 3600);
        ctx.fillText(f === F - 1 ? "now" : `−${hrs} h`, x + 6, y + 3);
      }
      const [ox, oy] = pt(g, 1, n - 1, n, 0);
      ctx.fillText(fmtTime(t.times[0], true), ox + 6, oy + 3);
    }

    return {
      draw(S) {
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const t = S.table;
        if (!t || !t.frames) { emptyMessage(ctx, w, h, "listening for the world…"); return; }

        const k = `${t.generation}|${w}|${h}|${UI.scale}|${UI.dpr}`;
        if (k !== key) { renderStatic(t, w, h); key = k; }
        ctx.drawImage(off, 0, 0, w, h);

        const g = geom(w, h), F = t.frames, n = t.wavePoints;
        const pos = clamp(S.tel.pos ?? WT.get("osc_pos"), 0, 1);
        const fpos = pos * (F - 1);
        const z = F > 1 ? 1 - fpos / (F - 1) : 0;
        const row = rowAt(t.wave, F, n, fpos);

        // fill
        const pts = row.map((v, i) => pt(g, z, i, n, v));
        const [bx0, by0] = pt(g, z, 0, n, 0), [bx1, by1] = pt(g, z, n - 1, n, 0);
        const grad = ctx.createLinearGradient(0, by0 - g.amp, 0, by0 + g.amp);
        grad.addColorStop(0, "rgba(255,201,60,0.30)");
        grad.addColorStop(0.5, "rgba(255,201,60,0.04)");
        grad.addColorStop(1, "rgba(255,201,60,0.28)");
        ctx.beginPath();
        ctx.moveTo(bx0, by0);
        for (const [x, y] of pts) ctx.lineTo(x, y);
        ctx.lineTo(bx1, by1);
        ctx.closePath();
        ctx.fillStyle = grad;
        ctx.fill();

        // baseline + line
        ctx.strokeStyle = "rgba(255,201,60,0.35)"; ctx.lineWidth = 1;
        ctx.beginPath(); ctx.moveTo(bx0, by0); ctx.lineTo(bx1, by1); ctx.stroke();
        glow(ctx, C.accentGlow, 12);
        ctx.strokeStyle = C.accent; ctx.lineWidth = 2;
        ctx.beginPath();
        pts.forEach(([x, y], i) => (i ? ctx.lineTo(x, y) : ctx.moveTo(x, y)));
        ctx.stroke();
        noGlow(ctx);

        // marker on the depth axis
        const [mx, my] = pt(g, z, n - 1, n, 0);
        ctx.fillStyle = C.accent;
        glow(ctx, C.accentGlow, 8);
        ctx.beginPath(); ctx.arc(mx, my, 3, 0, Math.PI * 2); ctx.fill();
        noGlow(ctx);

        const fi = Math.round(fpos);
        const note = document.getElementById("wt-note");
        if (note) note.textContent = `frame ${fi + 1}/${F} · ${fmtTime(t.times[fi], true)} UTC`;
      },
    };
  }

  // ======================================================================
  // ① Data — the raw measurement behind the current frame
  // ======================================================================
  function DataView(canvas) {
    return {
      draw(S) {
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const t = S.table;
        if (!t || !t.frames) return;
        const n = t.rawPoints, F = t.frames;
        const pad = 8, top = 22, bottom = h - 16;
        const fpos = clamp(S.tel.pos ?? 0, 0, 1) * (F - 1);

        // envelope of all frames (max), faint
        if (!t._env) {
          t._env = new Array(n).fill(0);
          for (let f = 0; f < F; f++) for (let i = 0; i < n; i++) t._env[i] = Math.max(t._env[i], t.raw[f * n + i]);
        }
        const X = (i) => pad + (i / (n - 1)) * (w - 2 * pad);
        const Y = (v) => bottom - clamp(v, 0, 1) * (bottom - top);

        ctx.strokeStyle = C.grid;
        ctx.beginPath(); ctx.moveTo(pad, bottom); ctx.lineTo(w - pad, bottom); ctx.stroke();

        ctx.fillStyle = "rgba(95,184,255,0.07)";
        ctx.beginPath(); ctx.moveTo(X(0), bottom);
        t._env.forEach((v, i) => ctx.lineTo(X(i), Y(v)));
        ctx.lineTo(X(n - 1), bottom); ctx.fill();

        const row = rowAt(t.raw, F, n, fpos);
        const grad = ctx.createLinearGradient(0, top, 0, bottom);
        grad.addColorStop(0, "rgba(95,184,255,0.45)");
        grad.addColorStop(1, "rgba(95,184,255,0.02)");
        ctx.beginPath(); ctx.moveTo(X(0), bottom);
        row.forEach((v, i) => ctx.lineTo(X(i), Y(v)));
        ctx.lineTo(X(n - 1), bottom); ctx.closePath();
        ctx.fillStyle = grad; ctx.fill();
        glow(ctx, C.blueGlow, 8);
        ctx.strokeStyle = C.blue; ctx.lineWidth = 1.5;
        ctx.beginPath(); row.forEach((v, i) => (i ? ctx.lineTo(X(i), Y(v)) : ctx.moveTo(X(i), Y(v)))); ctx.stroke();
        noGlow(ctx);

        ctx.fillStyle = C.faint;
        ctx.font = "8.5px -apple-system, sans-serif";
        ctx.fillText(t.rawAxis || "", pad, h - 4);
        ctx.textAlign = "right";
        const peak = t.isSpectrum ? `${(t.rawMax).toFixed(1)} ${t.rawUnit}` : `${t.rawMin.toFixed(0)}–${t.rawMax.toFixed(0)} ${t.rawUnit}`;
        ctx.fillText(peak, w - pad, 14);
        ctx.textAlign = "left";
      },
    };
  }

  // ======================================================================
  // ② Harmonics — partial amplitudes after the transform
  // ======================================================================
  function HarmonicsView(canvas) {
    return {
      draw(S) {
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const t = S.table;
        if (!t || !t.frames) return;
        const n = t.harmonicCount, F = t.frames;
        const row = rowAt(t.harmonics, F, n, clamp(S.tel.pos ?? 0, 0, 1) * (F - 1));
        const pad = 8, top = 22, bottom = h - 8;
        const bw = (w - 2 * pad) / n;
        const grad = ctx.createLinearGradient(0, top, 0, bottom);
        grad.addColorStop(0, "rgba(255,201,60,0.95)");
        grad.addColorStop(1, "rgba(255,201,60,0.08)");
        for (let i = 0; i < n; i++) {
          const v = Math.pow(clamp(row[i], 0, 1), 0.6);   // perceptual-ish
          const bh = v * (bottom - top);
          if (bh < 0.5) continue;
          const x = pad + i * bw;
          ctx.fillStyle = grad;
          ctx.fillRect(x + 0.6, bottom - bh, Math.max(1, bw - 1.4), bh);
          ctx.fillStyle = C.accentHot;
          ctx.fillRect(x + 0.6, bottom - bh, Math.max(1, bw - 1.4), 1.2);
        }
      },
    };
  }

  // ======================================================================
  // Filter — response curve over a live analyzer, draggable node
  // ======================================================================
  function FilterView(canvas) {
    const fMin = 20, fMax = 20000, dbTop = 24, dbBottom = -48;
    let drag = false, hover = false;

    const xOf = (f, w) => (Math.log(f / fMin) / Math.log(fMax / fMin)) * w;
    const fOf = (x, w) => fMin * Math.pow(fMax / fMin, clamp(x / w, 0, 1));
    const yOf = (db, h) => ((dbTop - db) / (dbTop - dbBottom)) * h;

    function damping() { return 2 - 1.94 * WT.get("flt_reso"); }
    function mag(f, fc, type, k) {
      const w = f / fc, w2 = w * w;
      const d = Math.sqrt((1 - w2) * (1 - w2) + k * k * w2);
      switch (type) {
        case 1: { const db = Math.sqrt((1 - w2) * (1 - w2) + 2 * w2); return (1 / db) * (1 / d); }
        case 2: return (w / d);
        case 3: return (w2 / d);
        default: return 1 / d;
      }
    }
    function curve(ctx, w, h, fc) {
      const type = Math.round(WT.get("flt_type")), k = damping();
      ctx.beginPath();
      for (let x = 0; x <= w; x += 2) {
        const db = 20 * Math.log10(Math.max(1e-6, mag(fOf(x, w), fc, type, k)));
        const y = yOf(clamp(db, dbBottom - 6, dbTop + 6), h);
        x ? ctx.lineTo(x, y) : ctx.moveTo(x, y);
      }
    }
    function nodeY(h) {
      const r = WT.get("flt_reso");
      return yOf(lerp(0, dbTop - 6, r), h);
    }

    canvas.addEventListener("pointerdown", (e) => {
      const w = canvas.clientWidth, h = canvas.clientHeight;
      drag = true;
      canvas.setPointerCapture(e.pointerId);
      WT.begin("flt_cutoff"); WT.begin("flt_reso");
      const move = (ev) => {
        const [mx, my] = UI.localPoint(canvas, ev);
        WT.set("flt_cutoff", fOf(clamp(mx, 0, w), w));
        // y: 0 dB line → reso 0, top region → reso 1
        const db = dbTop - (clamp(my, 0, h) / h) * (dbTop - dbBottom);
        WT.set("flt_reso", clamp(db / (dbTop - 6), 0, 1));
        UI.showTip(canvas, `${WT.fmt("flt_cutoff", WT.get("flt_cutoff"))} · Q ${WT.fmt("flt_reso", WT.get("flt_reso"))}`);
      };
      move(e);
      const up = () => {
        drag = false;
        canvas.removeEventListener("pointermove", move);
        canvas.removeEventListener("pointerup", up);
        WT.end("flt_cutoff"); WT.end("flt_reso");
        UI.hideTip();
      };
      canvas.addEventListener("pointermove", move);
      canvas.addEventListener("pointerup", up);
    });
    canvas.addEventListener("pointerenter", () => (hover = true));
    canvas.addEventListener("pointerleave", () => (hover = false));
    canvas.addEventListener("wheel", (e) => {
      e.preventDefault();
      WT.begin("flt_reso");
      WT.set("flt_reso", WT.get("flt_reso") - e.deltaY * 0.001);
      WT.end("flt_reso");
    }, { passive: false });

    return {
      draw(S) {
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);

        // grid
        ctx.font = "8.5px -apple-system, sans-serif";
        for (const f of [50, 100, 200, 500, 1000, 2000, 5000, 10000]) {
          const x = xOf(f, w);
          ctx.strokeStyle = f === 100 || f === 1000 || f === 10000 ? C.gridStrong : C.grid;
          ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke();
          ctx.fillStyle = C.faint;
          ctx.fillText(f >= 1000 ? f / 1000 + "k" : String(f), x + 3, h - 4);
        }
        for (const db of [12, 0, -12, -24, -36]) {
          const y = yOf(db, h);
          ctx.strokeStyle = db === 0 ? C.gridStrong : C.grid;
          ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
        }

        // analyzer (−90..0 dBFS mapped to the lower 80% of the display)
        const spec = S.tel.spectrum;
        if (spec && spec.length) {
          const n = spec.length;
          const ay = (db) => h - clamp((db + 90) / 90, 0, 1) * h * 0.85;
          const grad = ctx.createLinearGradient(0, 0, 0, h);
          grad.addColorStop(0, "rgba(95,184,255,0.30)");
          grad.addColorStop(1, "rgba(95,184,255,0.03)");
          ctx.beginPath(); ctx.moveTo(0, h);
          for (let i = 0; i < n; i++) ctx.lineTo(((i + 0.5) / n) * w, ay(spec[i]));
          ctx.lineTo(w, h); ctx.closePath();
          ctx.fillStyle = grad; ctx.fill();
          ctx.strokeStyle = "rgba(95,184,255,0.45)"; ctx.lineWidth = 1;
          ctx.beginPath();
          for (let i = 0; i < n; i++) { const x = ((i + 0.5) / n) * w; i ? ctx.lineTo(x, ay(spec[i])) : ctx.moveTo(x, ay(spec[i])); }
          ctx.stroke();
        }

        // static curve (parameter) and live curve (with envelope + LFO)
        const base = WT.get("flt_cutoff");
        ctx.setLineDash([3, 3]);
        ctx.strokeStyle = "rgba(255,201,60,0.35)"; ctx.lineWidth = 1;
        curve(ctx, w, h, base); ctx.stroke();
        ctx.setLineDash([]);

        const live = clamp(S.tel.cutoff || base, 20, 20000);
        curve(ctx, w, h, live);
        ctx.lineTo(w, h); ctx.lineTo(0, h); ctx.closePath();
        const fill = ctx.createLinearGradient(0, 0, 0, h);
        fill.addColorStop(0, "rgba(255,201,60,0.22)");
        fill.addColorStop(1, "rgba(255,201,60,0.0)");
        ctx.fillStyle = fill; ctx.fill();
        glow(ctx, C.accentGlow, 10);
        ctx.strokeStyle = C.accent; ctx.lineWidth = 2;
        curve(ctx, w, h, live); ctx.stroke();
        noGlow(ctx);

        // node
        const nx = xOf(base, w), ny = nodeY(h);
        ctx.beginPath(); ctx.arc(nx, ny, drag || hover ? 7 : 6, 0, Math.PI * 2);
        ctx.fillStyle = "rgba(255,201,60,0.18)"; ctx.fill();
        ctx.lineWidth = 1.5; ctx.strokeStyle = C.accentHot; ctx.stroke();
        ctx.beginPath(); ctx.arc(nx, ny, 2, 0, Math.PI * 2); ctx.fillStyle = C.accentHot; ctx.fill();

        ctx.fillStyle = C.text;
        ctx.textAlign = "right";
        ctx.fillText(`${WT.fmt("flt_cutoff", live)}`, w - 6, 12);
        ctx.textAlign = "left";
      },
    };
  }

  // ======================================================================
  // ADSR — draggable envelope with live level
  // ======================================================================
  function EnvView(canvas, prefix, color, glowColor, levelKey) {
    const ids = { a: prefix + "a", d: prefix + "d", s: prefix + "s", r: prefix + "r" };
    let handles = [], active = null;

    function widthFor(t) { return 0.08 + Math.log10(1 + t * 6) / Math.log10(1 + 30 * 6); }

    function layout(w, h) {
      const pad = 8, top = 10, bottom = h - 10;
      const wa = widthFor(WT.get(ids.a)), wd = widthFor(WT.get(ids.d)), ws = 0.22, wr = widthFor(WT.get(ids.r));
      const total = wa + wd + ws + wr;
      const span = w - 2 * pad, sc = span / total;
      const x0 = pad, xa = x0 + wa * sc, xd = xa + wd * sc, xs = xd + ws * sc, xr = xs + wr * sc;
      const S = WT.get(ids.s);
      const yTop = top, yBot = bottom, yS = lerp(yBot, yTop, S);
      return { x0, xa, xd, xs, xr, yTop, yBot, yS, sc };
    }

    function path(ctx, L) {
      const steps = 24;
      ctx.moveTo(L.x0, L.yBot);
      for (let i = 1; i <= steps; i++) {     // attack: concave rise
        const t = i / steps, v = 1 - Math.pow(1 - t, 1.8);
        ctx.lineTo(lerp(L.x0, L.xa, t), lerp(L.yBot, L.yTop, v));
      }
      for (let i = 1; i <= steps; i++) {     // decay: exponential
        const t = i / steps, v = Math.exp(-5 * t);
        ctx.lineTo(lerp(L.xa, L.xd, t), lerp(L.yS, L.yTop, v));
      }
      ctx.lineTo(L.xs, L.yS);
      for (let i = 1; i <= steps; i++) {     // release
        const t = i / steps, v = Math.exp(-5 * t) - Math.exp(-5) * t;
        ctx.lineTo(lerp(L.xs, L.xr, t), lerp(L.yBot, L.yS, v));
      }
    }

    canvas.addEventListener("pointerdown", (e) => {
      const [x, y] = UI.localPoint(canvas, e);
      let best = null, bd = 18;
      for (const hd of handles) { const d = Math.hypot(hd.x - x, hd.y - y); if (d < bd) { bd = d; best = hd; } }
      if (!best) return;
      active = best;
      canvas.setPointerCapture(e.pointerId);
      best.ids.forEach((id) => WT.begin(id));
      const startX = x, startY = y;
      const start = best.ids.map((id) => WT.toNorm(id, WT.get(id)));
      const w = canvas.clientWidth, h = canvas.clientHeight;
      const move = (ev) => {
        const [mx, my] = UI.localPoint(canvas, ev);
        const fine = ev.shiftKey ? 0.2 : 1;
        WT.set(best.ids[0], WT.fromNorm(best.ids[0], start[0] + (mx - startX) / (w * 0.6) * fine));
        if (best.ids[1]) WT.set(best.ids[1], WT.fromNorm(best.ids[1], start[1] - (my - startY) / (h * 0.8) * fine));
        UI.showTip(canvas, best.ids.map((id) => WT.fmt(id, WT.get(id))).join(" · "));
      };
      const up = () => {
        active = null;
        canvas.removeEventListener("pointermove", move);
        canvas.removeEventListener("pointerup", up);
        best.ids.forEach((id) => WT.end(id));
        UI.hideTip();
      };
      canvas.addEventListener("pointermove", move);
      canvas.addEventListener("pointerup", up);
    });

    return {
      draw(S) {
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const L = layout(w, h);

        ctx.strokeStyle = C.grid;
        for (const x of [L.xa, L.xd, L.xs]) { ctx.beginPath(); ctx.moveTo(x, L.yTop); ctx.lineTo(x, L.yBot); ctx.stroke(); }
        ctx.beginPath(); ctx.moveTo(0, L.yBot); ctx.lineTo(w, L.yBot); ctx.stroke();

        ctx.beginPath(); path(ctx, L); ctx.lineTo(L.xr, L.yBot); ctx.closePath();
        const grad = ctx.createLinearGradient(0, L.yTop, 0, L.yBot);
        grad.addColorStop(0, color.replace("1)", "0.28)"));
        grad.addColorStop(1, color.replace("1)", "0.02)"));
        ctx.fillStyle = grad; ctx.fill();

        glow(ctx, glowColor, 10);
        ctx.strokeStyle = color; ctx.lineWidth = 1.8;
        ctx.beginPath(); path(ctx, L); ctx.stroke();
        noGlow(ctx);

        // live level
        const lvl = clamp(S.tel[levelKey] || 0, 0, 1);
        const ly = lerp(L.yBot, L.yTop, lvl);
        if (lvl > 0.001) {
          ctx.strokeStyle = color.replace("1)", "0.5)");
          ctx.setLineDash([2, 3]);
          ctx.beginPath(); ctx.moveTo(L.x0, ly); ctx.lineTo(w - 6, ly); ctx.stroke();
          ctx.setLineDash([]);
          glow(ctx, glowColor, 8);
          ctx.fillStyle = color;
          ctx.beginPath(); ctx.arc(w - 6, ly, 2.5, 0, Math.PI * 2); ctx.fill();
          noGlow(ctx);
        }

        handles = [
          { x: L.xa, y: L.yTop, ids: [ids.a] },
          { x: L.xd, y: L.yS, ids: [ids.d, ids.s] },
          { x: L.xr, y: L.yBot, ids: [ids.r] },
        ];
        for (const hd of handles) {
          ctx.beginPath(); ctx.arc(hd.x, hd.y, active === hd ? 5 : 4, 0, Math.PI * 2);
          ctx.fillStyle = "#08121f"; ctx.fill();
          ctx.strokeStyle = color; ctx.lineWidth = 1.5; ctx.stroke();
        }
      },
    };
  }

  // ======================================================================
  // LFO — one cycle of the shape, phase dot
  // ======================================================================
  function LfoView(canvas, n, color, glowColor) {
    function shape(s, p) {
      switch (s) {
        case 0: return Math.sin(2 * Math.PI * p);
        case 1: { const q = (p + 0.25) % 1; return 1 - 4 * Math.abs(q - 0.5); }
        case 2: return 2 * p - 1;
        case 3: return p < 0.5 ? 1 : -1;
        case 4: { const steps = [0.6, -0.3, 0.9, -0.8, 0.2, -0.5, 0.75, -0.1]; return steps[Math.floor(p * 8) % 8]; }
        default: { const pts = [0, 0.7, -0.2, 0.5, -0.8, 0.1, 0.6, -0.4, 0]; const x = p * 8, i = Math.floor(x), t = x - i, s2 = t * t * (3 - 2 * t); return lerp(pts[i], pts[i + 1], s2); }
      }
    }
    return {
      draw(S) {
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const mid = h / 2, amp = h * 0.36, pad = 8;
        const s = Math.round(WT.get(`lfo${n}_shape`));
        const amt = WT.get(`lfo${n}_amt`);

        ctx.strokeStyle = C.gridStrong;
        ctx.beginPath(); ctx.moveTo(pad, mid); ctx.lineTo(w - pad, mid); ctx.stroke();

        const X = (p) => pad + p * (w - 2 * pad);
        const scaleA = 0.25 + 0.75 * Math.abs(amt);
        ctx.beginPath();
        for (let i = 0; i <= 160; i++) {
          const p = i / 160, y = mid - shape(s, p) * amp * scaleA * Math.sign(amt || 1);
          i ? ctx.lineTo(X(p), y) : ctx.moveTo(X(p), y);
        }
        glow(ctx, glowColor, 10);
        ctx.strokeStyle = color; ctx.lineWidth = 1.8; ctx.stroke();
        noGlow(ctx);

        const ph = S.tel[`lfo${n}Phase`] ?? 0;
        const v = S.tel[`lfo${n}`] ?? 0;
        const px = X(ph), py = mid - v * amp * scaleA * Math.sign(amt || 1);
        ctx.strokeStyle = color.replace("1)", "0.25)");
        ctx.beginPath(); ctx.moveTo(px, pad); ctx.lineTo(px, h - pad); ctx.stroke();
        glow(ctx, glowColor, 12);
        ctx.fillStyle = "#fff6d8";
        ctx.beginPath(); ctx.arc(px, py, 3.2, 0, Math.PI * 2); ctx.fill();
        noGlow(ctx);

        ctx.font = "9px -apple-system, sans-serif";
        ctx.fillStyle = C.text;
        ctx.textAlign = "right";
        ctx.fillText(`→ ${WT.fmt(`lfo${n}_dest`, WT.get(`lfo${n}_dest`))}  ${WT.fmt(`lfo${n}_amt`, amt)}`, w - 8, 13);
        ctx.textAlign = "left";
      },
    };
  }

  // ======================================================================
  // Euclid ring
  // ======================================================================
  const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
  UI.noteName = (n) => NOTE_NAMES[n % 12] + (Math.floor(n / 12) - 1);

  function euclidHit(i, steps, pulses, rot) {
    if (pulses <= 0) return false;
    if (pulses >= steps) return true;
    const j = (((i + rot) % steps) + steps) % steps;
    return (j * pulses) % steps < pulses;
  }

  function EuclidView(canvas) {
    let lastHits = 0, flash = 0;
    return {
      draw(S) {
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const mode = Math.round(WT.get("trig_mode"));
        const steps = Math.round(WT.get("eu_steps")), pulses = Math.min(steps, Math.round(WT.get("eu_pulses")));
        const rot = Math.round(WT.get("eu_rotate"));
        const cx = w / 2, cy = h / 2, R = Math.min(w, h) * 0.40;
        const active = mode === 1;
        const step = active ? S.tel.step : -1;

        if ((S.tel.hits || 0) !== lastHits) { lastHits = S.tel.hits || 0; if (active) flash = 1; }
        flash *= 0.9;

        ctx.globalAlpha = active ? 1 : 0.35;
        ctx.strokeStyle = C.gridStrong; ctx.lineWidth = 1;
        ctx.beginPath(); ctx.arc(cx, cy, R, 0, Math.PI * 2); ctx.stroke();

        const P = (i, r) => { const a = -Math.PI / 2 + (i / steps) * Math.PI * 2; return [cx + Math.cos(a) * r, cy + Math.sin(a) * r]; };

        // polygon through the hits
        const hitIdx = [];
        for (let i = 0; i < steps; i++) if (euclidHit(i, steps, pulses, rot)) hitIdx.push(i);
        if (hitIdx.length > 1) {
          ctx.beginPath();
          hitIdx.forEach((i, k) => { const [x, y] = P(i, R); k ? ctx.lineTo(x, y) : ctx.moveTo(x, y); });
          ctx.closePath();
          ctx.fillStyle = "rgba(255,201,60,0.06)"; ctx.fill();
          ctx.strokeStyle = "rgba(255,201,60,0.35)"; ctx.stroke();
        }

        for (let i = 0; i < steps; i++) {
          const [x, y] = P(i, R);
          const hit = euclidHit(i, steps, pulses, rot);
          const cur = i === step;
          if (hit) {
            if (cur) glow(ctx, C.accentGlow, 16);
            ctx.fillStyle = cur ? C.accentHot : C.accent;
            ctx.beginPath(); ctx.arc(x, y, cur ? 5.5 + flash * 2 : 4.2, 0, Math.PI * 2); ctx.fill();
            noGlow(ctx);
          } else {
            ctx.fillStyle = cur ? C.text : "#24405f";
            ctx.beginPath(); ctx.arc(x, y, cur ? 3 : 2.2, 0, Math.PI * 2); ctx.fill();
          }
        }
        if (active && step >= 0) {
          const [x, y] = P(step, R - 12);
          ctx.strokeStyle = "rgba(255,224,138,0.5)";
          ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(x, y); ctx.stroke();
        }
        if (flash > 0.02) {
          ctx.strokeStyle = `rgba(255,201,60,${(flash * 0.5).toFixed(3)})`;
          ctx.beginPath(); ctx.arc(cx, cy, R * (1.05 + (1 - flash) * 0.2), 0, Math.PI * 2); ctx.stroke();
        }
        ctx.globalAlpha = 1;

        // centre text
        ctx.textAlign = "center";
        if (active) {
          ctx.font = "600 18px -apple-system, sans-serif";
          ctx.fillStyle = C.accentHot;
          ctx.fillText(`${pulses}`, cx, cy + 2);
          ctx.font = "9px -apple-system, sans-serif";
          ctx.fillStyle = C.text;
          ctx.fillText(`of ${steps} · ${WT.fmt("eu_rate", WT.get("eu_rate"))}`, cx, cy + 16);
        } else {
          ctx.font = "600 11px -apple-system, sans-serif";
          ctx.fillStyle = C.accent;
          ctx.fillText(mode === 0 ? "DRONE" : "MIDI", cx, cy - 2);
          ctx.font = "9px -apple-system, sans-serif";
          ctx.fillStyle = C.text;
          const notes = (S.tel.notes || []).slice(0, 5).map(UI.noteName).join(" ");
          ctx.fillText(notes || "—", cx, cy + 13);
        }
        ctx.textAlign = "left";
      },
    };
  }

  // ======================================================================
  // Delay taps
  // ======================================================================
  const DLY_Q = [0.25, 1 / 3, 0.5, 0.75, 2 / 3, 1, 1.5, 2, 4];
  function DelayView(canvas) {
    return {
      draw(S) {
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const bpm = S.tel.bpm || WT.get("bpm");
        const t = DLY_Q[Math.round(WT.get("dly_time"))] * 60 / bpm;
        const fb = WT.get("dly_fb"), mix = WT.get("dly_mix"), pp = WT.get("dly_pingpong") > 0.5;
        const tone = WT.get("dly_tone");
        const span = Math.max(2.0, t * 8.5);
        const mid = h / 2, pad = 8, amp = h / 2 - 12;
        const X = (sec) => pad + (sec / span) * (w - 2 * pad);

        ctx.strokeStyle = C.gridStrong;
        ctx.beginPath(); ctx.moveTo(pad, mid); ctx.lineTo(w - pad, mid); ctx.stroke();

        // dry
        ctx.fillStyle = "rgba(95,184,255,0.7)";
        ctx.fillRect(X(0) - 1, mid - amp * 0.9, 2, amp * 1.8);

        // Heights normalised so the pattern stays readable; mix sets brightness.
        let level = 1;
        ctx.globalAlpha = 0.35 + 0.65 * Math.min(1, mix * 2);
        for (let k = 1; k <= 24 && t * k < span; k++) {
          const up = !pp || k % 2 === 1;
          const hgt = level * amp * 0.9 * Math.max(0.2, 1 - 0.05 * (1 - tone) * k);
          if (hgt < 0.5) break;
          const x = X(t * k);
          glow(ctx, C.accentGlow, 8);
          ctx.fillStyle = C.accent;
          if (pp) ctx.fillRect(x - 1.5, up ? mid - hgt : mid, 3, hgt);
          else ctx.fillRect(x - 1.5, mid - hgt, 3, hgt * 2);
          noGlow(ctx);
          level *= fb;
        }
        ctx.globalAlpha = 1;
        ctx.font = "9px -apple-system, sans-serif";
        ctx.fillStyle = C.text;
        ctx.fillText(`${(t * 1000).toFixed(0)} ms`, pad + 2, 12);
        if (pp) { ctx.fillStyle = C.faint; ctx.fillText("L", w - 14, 12); ctx.fillText("R", w - 14, h - 5); }
      },
    };
  }

  // ======================================================================
  // Space (reverb) — decay time over frequency, shimmer sparks
  // ======================================================================
  function ReverbView(canvas) {
    const sparks = [];
    const fMin = 30, fMax = 20000, tMin = 0.2, tMax = 40;
    const xOf = (f, w) => (Math.log(f / fMin) / Math.log(fMax / fMin)) * w;
    const yOf = (t, h) => h - 14 - (Math.log(t / tMin) / Math.log(tMax / tMin)) * (h - 26);

    return {
      draw(S) {
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const decay = WT.get("rev_decay"), damp = WT.get("rev_damp"), shimmer = WT.get("rev_shimmer");
        const size = WT.get("rev_size"), mix = WT.get("rev_mix");
        const fd = 1200 * Math.pow(15, 1 - damp);
        const level = clamp((S.tel.verb || 0) * 4, 0, 1);

        ctx.font = "8.5px -apple-system, sans-serif";
        for (const f of [100, 1000, 10000]) {
          const x = xOf(f, w);
          ctx.strokeStyle = C.grid; ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, h); ctx.stroke();
          ctx.fillStyle = C.faint; ctx.fillText(f >= 1000 ? f / 1000 + "k" : String(f), x + 3, h - 3);
        }
        for (const t of [1, 10]) {
          const y = yOf(t, h);
          ctx.strokeStyle = C.grid; ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(w, y); ctx.stroke();
          ctx.fillStyle = C.faint; ctx.fillText(t + " s", 4, y - 3);
        }

        const rt = (f) => decay / Math.sqrt(1 + Math.pow(f / fd, 2)) * (1 + 0.15 * Math.exp(-f / 200));
        const top = ctx.createLinearGradient(0, 0, 0, h);
        top.addColorStop(0, `rgba(255,201,60,${(0.22 + 0.25 * level * mix).toFixed(3)})`);
        top.addColorStop(1, "rgba(255,201,60,0.0)");
        ctx.beginPath(); ctx.moveTo(0, h);
        for (let x = 0; x <= w; x += 2) ctx.lineTo(x, yOf(clamp(rt(fMin * Math.pow(fMax / fMin, x / w)), tMin, tMax), h));
        ctx.lineTo(w, h); ctx.closePath();
        ctx.fillStyle = top; ctx.fill();
        glow(ctx, C.accentGlow, 12);
        ctx.strokeStyle = C.accent; ctx.lineWidth = 2;
        ctx.beginPath();
        for (let x = 0; x <= w; x += 2) { const y = yOf(clamp(rt(fMin * Math.pow(fMax / fMin, x / w)), tMin, tMax), h); x ? ctx.lineTo(x, y) : ctx.moveTo(x, y); }
        ctx.stroke();
        noGlow(ctx);

        // shimmer: sparks drifting upward from the high band, an octave above
        if (shimmer > 0.01) {
          const rate = shimmer * (0.4 + 2.5 * level);
          if (Math.random() < rate) {
            const f = 600 * Math.pow(20, Math.random());
            sparks.push({ x: xOf(f, w), y: yOf(clamp(rt(f), tMin, tMax), h), vy: -0.25 - Math.random() * 0.5, vx: 0.25 + Math.random() * 0.4, life: 1 });
          }
        }
        for (let i = sparks.length - 1; i >= 0; i--) {
          const s = sparks[i];
          s.x += s.vx; s.y += s.vy; s.life -= 0.012;
          if (s.life <= 0 || s.y < 0) { sparks.splice(i, 1); continue; }
          ctx.fillStyle = `rgba(255,236,170,${(s.life * 0.9).toFixed(3)})`;
          glow(ctx, C.accentGlow, 6);
          ctx.beginPath(); ctx.arc(s.x, s.y, 1.2 + s.life, 0, Math.PI * 2); ctx.fill();
        }
        noGlow(ctx);

        ctx.fillStyle = C.text; ctx.font = "9px -apple-system, sans-serif";
        ctx.textAlign = "right";
        ctx.fillText(`${WT.fmt("rev_decay", decay)} · size ${Math.round(size * 100)}% · pre ${WT.get("rev_predelay").toFixed(0)} ms`, w - 6, 12);
        ctx.textAlign = "left";
      },
    };
  }

  // ======================================================================
  // Output meters
  // ======================================================================
  function MeterView(canvas) {
    let holdL = 0, holdR = 0;
    const dbFloor = -48;
    return {
      draw(S) {
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const top = 4, bottom = h - 14;
        const Y = (db) => top + (clamp(db, dbFloor, 0) / dbFloor) * (bottom - top);
        const toDb = (g) => 20 * Math.log10(Math.max(1e-6, g));
        const l = toDb(S.tel.peakL || 0), r = toDb(S.tel.peakR || 0);
        holdL = Math.max(l, holdL - 0.4); holdR = Math.max(r, holdR - 0.4);
        const gr = -toDb(S.tel.gr || 1);

        const bars = [[6, l, holdL], [18, r, holdR]];
        for (const [x, db, hold] of bars) {
          ctx.fillStyle = "#081322"; ctx.fillRect(x, top, 9, bottom - top);
          const grad = ctx.createLinearGradient(0, bottom, 0, top);
          grad.addColorStop(0, "#2b6fb3"); grad.addColorStop(0.7, C.blue); grad.addColorStop(0.92, C.accent); grad.addColorStop(1, C.red);
          ctx.fillStyle = grad;
          ctx.fillRect(x, Y(db), 9, bottom - Y(db));
          ctx.fillStyle = C.accentHot; ctx.fillRect(x, Y(hold), 9, 1.5);
        }
        // gain reduction (grows downward from the top)
        ctx.fillStyle = "#081322"; ctx.fillRect(32, top, 7, bottom - top);
        const grH = clamp(gr / 12, 0, 1) * (bottom - top);
        glow(ctx, C.accentGlow, 6);
        ctx.fillStyle = C.accent; ctx.fillRect(32, top, 7, grH);
        noGlow(ctx);

        const ceil = WT.get("lim_ceiling");
        ctx.strokeStyle = "rgba(255,99,99,0.8)";
        ctx.beginPath(); ctx.moveTo(4, Y(ceil)); ctx.lineTo(29, Y(ceil)); ctx.stroke();

        ctx.font = "8px -apple-system, sans-serif"; ctx.fillStyle = C.faint;
        for (const db of [0, -12, -24, -36]) ctx.fillText(String(db), 42, Y(db) + 3);
        ctx.fillText("L R GR", 4, h - 3);
      },
    };
  }

  Object.assign(UI, { WavetableView, DataView, HarmonicsView, FilterView, EnvView, LfoView, EuclidView, DelayView, ReverbView, MeterView });
})();
