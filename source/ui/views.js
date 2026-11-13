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

  function DriveView(canvas) {
    const paint = cache(canvas);
    return {
      draw(drive, level) {
        paint(drive.toFixed(3) + (level * 40 | 0), (ctx, w, h) => {
          const cx = w / 2, cy = h / 2, r = Math.min(w, h) / 2 - 3;
          ctx.strokeStyle = C.hairSoft;
          ctx.beginPath(); ctx.moveTo(cx - r, cy + 0.5); ctx.lineTo(cx + r, cy + 0.5); ctx.moveTo(cx + 0.5, cy - r); ctx.lineTo(cx + 0.5, cy + r); ctx.stroke();
          ctx.setLineDash([2, 3]); ctx.strokeStyle = C.hair;
          ctx.beginPath(); ctx.moveTo(cx - r, cy + r); ctx.lineTo(cx + r, cy - r); ctx.stroke(); ctx.setLineDash([]);
          const g = 0.4 + 7 * drive, f = (x) => Math.tanh(g * x) / Math.tanh(g);
          ctx.beginPath();
          for (let i = 0; i <= 40; i++) { const x = -1 + i / 20; const px = cx + x * r, py = cy - f(x) * r; i ? ctx.lineTo(px, py) : ctx.moveTo(px, py); }
          ctx.strokeStyle = C.inkHi; ctx.lineWidth = 1.4; ctx.stroke();
          const lx = clamp(level, 0, 1);
          ctx.fillStyle = "#fff";
          ctx.beginPath(); ctx.arc(cx + lx * r, cy - f(lx) * r, 2.6, 0, 2 * Math.PI); ctx.fill();
        });
      },
    };
  }

  const LOGF = (f, w) => (Math.log2(f / 20) / Math.log2(1000)) * w;

  /** The filter (T3.7; animated in T6.6): its response now (after the sky) over the spectrum. What
   *  it draws eases like the DSP glides (log cutoff, resonance, and the type as a crossfade), so the
   *  slope moves with every knob and with the sky. The lit body under the slope is the passband; on
   *  the axis, the knob's cutoff (a tick) and the sky's reach at full activity (a band). Ink (T6.7). */
  function FilterView(canvas) {
    const DB_TOP = 15, DB_BOT = -39, TAU = 0.12;
    const st = { lhz: Math.log2(20000), k: 1.8, w: [1, 0, 0, 0], at: 0, primed: false };
    function mag(type, k, x) {
      const x2 = x * x, d = Math.sqrt((1 - x2) ** 2 + (k * x) ** 2);
      if (type === 0) return 1 / d;
      if (type === 1) return 1 / (d * Math.sqrt((1 - x2) ** 2 + 2 * x2));
      if (type === 2) return (k * x) / d;
      return x2 / d;
    }
    const response = (f) => {
      const x = f / Math.pow(2, st.lhz);
      let m = 0;
      for (let t = 0; t < 4; t++) if (st.w[t] > 1e-3) m += st.w[t] * mag(t, st.k, x);
      return m;
    };
    return {
      draw(p, spectrum) {
        const t = now(), dt = st.primed ? Math.min(0.25, t - st.at) : 1e3;
        st.at = t; st.primed = true;
        const e = 1 - Math.exp(-dt / TAU);
        const target = Math.log2(clamp(p.hz, 20, 20000));
        st.lhz += (target - st.lhz) * e;
        st.k += ((2 - 1.75 * clamp(p.reso, 0, 1)) - st.k) * e;
        for (let i = 0; i < 4; i++) st.w[i] += ((i === p.type ? 1 : 0) - st.w[i]) * e;

        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const top = 3, bot = h - 4;
        const y = (db) => top + (bot - top) * clamp((DB_TOP - db) / (DB_TOP - DB_BOT), 0, 1);
        if (spectrum && spectrum.length) {
          ctx.beginPath(); ctx.moveTo(0, bot);
          spectrum.forEach((db, i) => ctx.lineTo((i / (spectrum.length - 1)) * w, bot - clamp((db + 90) / 80, 0, 1) * (bot - top)));
          ctx.lineTo(w, bot); ctx.closePath(); ctx.fillStyle = "rgba(201,211,230,0.06)"; ctx.fill();
        }
        for (const f of [100, 1000, 10000]) { const xx = Math.round(LOGF(f, w)) + 0.5; ctx.strokeStyle = C.hairSoft; ctx.beginPath(); ctx.moveTo(xx, top); ctx.lineTo(xx, bot); ctx.stroke(); }
        const y0 = Math.round(y(0)) + 0.5;
        ctx.setLineDash([2, 3]); ctx.strokeStyle = C.hair; ctx.beginPath(); ctx.moveTo(0, y0); ctx.lineTo(w, y0); ctx.stroke(); ctx.setLineDash([]);

        // the slope, and the lit passband under it
        const N = Math.max(80, Math.round(w / 1.5));
        const pts = [];
        for (let i = 0; i <= N; i++) {
          const f = 20 * Math.pow(1000, i / N);
          pts.push([(i / N) * w, y(20 * Math.log10(Math.max(1e-6, response(f))))]);
        }
        const path = () => { ctx.beginPath(); pts.forEach(([x, yy], i) => (i ? ctx.lineTo(x, yy) : ctx.moveTo(x, yy))); };
        path(); ctx.lineTo(w, bot); ctx.lineTo(0, bot); ctx.closePath();
        const g = ctx.createLinearGradient(0, top, 0, bot);
        g.addColorStop(0, rgba("hi", 0.24)); g.addColorStop(1, rgba("hi", 0.02));
        ctx.fillStyle = g; ctx.fill();
        path(); ctx.strokeStyle = rgba("hi", 0.14); ctx.lineWidth = 3; ctx.stroke();
        path(); ctx.strokeStyle = C.inkHi; ctx.lineWidth = 1.4; ctx.stroke();

        // the knob's cutoff, the sky's reach, and where the filter is now
        const hz = Math.pow(2, st.lhz);
        if (p.base < 19950) {
          const xb = LOGF(p.base, w);
          if (p.sky > 0.005) {
            const reach = clamp(p.base * Math.pow(2, (p.type === 3 ? -3 : 3) * p.sky), 20, 20000);
            const xr = LOGF(reach, w);
            ctx.fillStyle = rgba("ink", 0.3); ctx.fillRect(Math.min(xb, xr), bot - 2, Math.abs(xr - xb), 2);
          }
          ctx.strokeStyle = C.ink; ctx.beginPath(); ctx.moveTo(Math.round(xb) + 0.5, bot - 7); ctx.lineTo(Math.round(xb) + 0.5, bot); ctx.stroke();
        }
        if (hz < 19950) {
          const xh = LOGF(hz, w), yy = y(20 * Math.log10(Math.max(1e-6, response(hz))));
          ctx.fillStyle = "#fff"; ctx.beginPath(); ctx.arc(xh, yy, 2.8, 0, 2 * Math.PI); ctx.fill();
        }
        ctx.font = MONO(11); ctx.fillStyle = C.muted; ctx.textAlign = "right";
        ctx.fillText(hz >= 19950 ? "open" : hz >= 1000 ? (hz / 1000).toFixed(2) + " kHz" : Math.round(hz) + " Hz", w - 5, top + 11);
        ctx.textAlign = "left";
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

  // ================================================================ pictures as controls (T6.12)
  // Each effect's picture shows what the effect does to the sound now and is where its main values
  // are set (spec/09 rule 4; the mockup's drawPha, drawCho, drawDly, drawRev, drawOut). A view sizes
  // itself from its canvas, takes its parameters by ID (`ids`) and the focusable proxies of the
  // values it sets (`handles`, UI.Handle, placed on the handles every frame), and eases what it draws
  // (rule 4). All ink (rule 2).
  const DEFAULT_FS = 48000;
  const reducedMotion = !!(window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches);
  const norm = (id) => WT.toNorm(id, WT.get(id));
  /** The value a parameter has now with what moves it (the macro rings' dot), else the value set. */
  const liveOf = (id) => { const k = UI.knobs && UI.knobs[id]; return k && k.mod ? k.mod.now : WT.get(id); };
  /** Eases named values toward their targets once per frame (τ in seconds); snaps when there. */
  function Easer(tau) {
    const v = {};
    let at = 0, k = 1;
    return {
      tick() { const t = now(), dt = at ? Math.min(0.25, t - at) : 1e3; at = t; k = 1 - Math.exp(-dt / tau); return dt > 100 ? 0 : dt; },
      get(name, target) {
        if (!(name in v) || !isFinite(v[name])) v[name] = target;
        v[name] += (target - v[name]) * k;
        if (Math.abs(target - v[name]) < 1e-5 * (1 + Math.abs(target))) v[name] = target;
        return v[name];
      },
    };
  }
  const handleRing = (ctx, x, y, r = 5.5) => {
    ctx.fillStyle = C.night; ctx.beginPath(); ctx.arc(x, y, r, 0, 2 * Math.PI); ctx.fill();
    ctx.strokeStyle = C.inkHi; ctx.lineWidth = 1.8; ctx.stroke();
  };
  const tag = (ctx, text, x, y, align = "right") => {
    ctx.font = MONO(10.5); ctx.textAlign = align; ctx.textBaseline = "alphabetic"; ctx.fillStyle = C.ink; ctx.fillText(text, x, y); ctx.textAlign = "left";
  };

  /** The phaser's gain at `hz` with its break frequency at `fc`: dsp::Phaser::magnitude (fx/Phaser.h)
   *  line for line, the filter's exact response (bilinear all-passes, the feedback one sample late,
   *  dry/wet and the make-up gain). A unit test holds it to the Phaser on a sine sweep. */
  function phaserResponse(hz, fc, fs, stages, feedback, mix) {
    const n = clamp(Math.round(stages), 2, 8), fb = clamp(feedback, 0, 0.8), m = clamp(mix, 0, 1);
    const w = 2 * Math.PI * clamp(hz, 0, 0.4999 * fs) / fs;
    const th = -2 * n * Math.atan(Math.tan(0.5 * w) / Math.tan(Math.PI * fc / fs));
    const ar = Math.cos(th), ai = Math.sin(th);
    const dr = 1 - fb * Math.cos(th - w), di = -fb * Math.sin(th - w), dd = dr * dr + di * di;
    const wr = (ar * dr + ai * di) / dd, wi = (ai * dr - ar * di) / dd;
    const or = (1 - m) + m * wr, oi = m * wi;
    return Math.sqrt(or * or + oi * oi) / Math.sqrt(Math.max(1e-6, (1 - m) * (1 - m) + m * m));
  }
  /** The break frequency for an LFO phase (dsp::Phaser::breakHz): 200 Hz … 2 kHz at depth 1. */
  const phaserBreak = (depth, phase) => 200 * Math.pow(10, 0.5 + 0.5 * clamp(depth, 0, 1) * Math.sin(2 * Math.PI * phase));

  /** Phaser: the sources' spectrum shaped by the tone stage (outline: what goes in) and the same ×
   *  the phaser's response (bright: what comes out), so the notches are seen eating the sound; dotted
   *  lines at the notches; the sweep range on the bottom edge with a dot at the break frequency now.
   *  The LFO phase is the engine's (telemetry `phaser`), run on between frames. Drag: sideways the
   *  depth, up and down the feedback. `tone(f, tel)` is the tone stage's response in dB. */
  function PhaserView(canvas, { ids = {}, handles = {}, tone = () => 0 } = {}) {
    const id = Object.assign({ rate: "ph_rate", depth: "ph_depth", feedback: "ph_feedback", mix: "ph_mix", stages: "ph_stages" }, ids);
    const E = Easer(0.1);
    // heights are amplitude^½ against an eased reference (the loudest band): the spectrum's shape
    // reads, and a notch of −10 dB takes half the height away, as the ear hears it eat the sound
    const LIFT = 40;
    let phase = 0, primed = false, ref = -30, drag = false;
    const spec = new Float32Array(120).fill(-100);
    const X = (f, w) => (Math.log(f / 20) / Math.log(1000)) * w;
    UI.dragPicture(canvas, (p) => {
      const d0 = norm(id.depth), f0 = norm(id.feedback);
      drag = true;
      return {
        ids: [id.depth, id.feedback],
        move(q, ev) { const k = ev.shiftKey ? 0.15 : 1; UI.setNorm(id.depth, d0 + (q.x - p.x) / 80 * k); UI.setNorm(id.feedback, f0 + (p.y - q.y) / 80 * k); },
        end() { drag = false; },
      };
    });
    /** The source's level (dB) at f, interpolated between the analyser's 120 log bands. */
    const specAt = (f) => {
      const x = clamp(Math.log(f / 20) / Math.log(1000) * 120 - 0.5, 0, 119), i = Math.floor(x), j = Math.min(119, i + 1);
      return spec[i] + (spec[j] - spec[i]) * (x - i);
    };
    return {
      draw(tel) {
        const dt = E.tick(), fs = tel.sampleRate || DEFAULT_FS;
        const mixSet = WT.get(id.mix);
        // the engine's LFO: it advances only while the phaser is on (fx/Phaser.h)
        if (mixSet > 0) phase += WT.get(id.rate) * dt;
        if (typeof tel.phaser === "number") {
          if (!primed) phase = tel.phaser;
          else { let e = tel.phaser - phase; e -= Math.round(e); phase += e * 0.15; }
          primed = true;
        }
        phase -= Math.floor(phase);
        const depth = E.get("depth", WT.get(id.depth)), fb = E.get("fb", WT.get(id.feedback)), mix = E.get("mix", liveOf(id.mix));
        const N = Math.round(WT.get(id.stages));
        const fc = phaserBreak(depth, phase), fLo = phaserBreak(depth, 0.75), fHi = phaserBreak(depth, 0.25);
        const src = tel.spectrumIn || tel.spectrum;
        if (src && src.length === 120) for (let i = 0; i < 120; i++) spec[i] += (src[i] - spec[i]) * 0.4;

        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const base = h - 18, H = base - 8, yBar = h - 8;
        // what goes in (the source through the tone stage) and what comes out
        const n = Math.max(40, Math.round(w / 1.5));
        const fin = new Float32Array(n + 1), fout = new Float32Array(n + 1);
        let peak = -100;
        for (let i = 0; i <= n; i++) {
          const f = 20 * Math.pow(1000, i / n);
          fin[i] = specAt(f) + tone(f, tel);
          fout[i] = fin[i] + 20 * Math.log10(Math.max(1e-6, phaserResponse(f, fc, fs, N, fb, mix)));
          peak = Math.max(peak, fin[i]);
        }
        ref += (Math.max(-60, peak) - ref) * (primed ? 0.05 : 1);
        const Y = (db) => base - H * 0.8 * Math.min(1.25, Math.pow(10, (db - ref) / LIFT));
        const path = (arr) => { ctx.beginPath(); ctx.moveTo(0, base); for (let i = 0; i <= n; i++) ctx.lineTo((i / n) * w, Y(arr[i])); ctx.lineTo(w, base); ctx.closePath(); };
        path(fin); ctx.fillStyle = "rgba(201,211,230,0.06)"; ctx.fill(); ctx.strokeStyle = "rgba(201,211,230,0.3)"; ctx.lineWidth = 1; ctx.stroke();
        path(fout);
        const g = ctx.createLinearGradient(0, 8, 0, base);
        g.addColorStop(0, "rgba(238,242,250,0.75)"); g.addColorStop(1, "rgba(201,211,230,0.14)");
        ctx.fillStyle = g; ctx.fill();
        // the notches: where the stages turn the sound half a cycle (refined on the exact response)
        ctx.strokeStyle = "rgba(238,242,250,0.5)"; ctx.lineWidth = 1; ctx.setLineDash([2, 3]);
        const tc = Math.tan(Math.PI * fc / fs);
        for (let k = 0; 2 * k + 1 < N; k++) {
          let f = (fs / Math.PI) * Math.atan(tc * Math.tan((2 * k + 1) * Math.PI / (2 * N))), best = Infinity, at = f;
          for (let s = -12; s <= 12; s++) { const ff = f * Math.pow(2, s / 40), r = phaserResponse(ff, fc, fs, N, fb, 0.5); if (r < best) { best = r; at = ff; } }
          f = at;
          if (f < 20 || f > 20000) continue;
          const x = Math.round(X(f, w)) + 0.5;
          ctx.beginPath(); ctx.moveTo(x, 6); ctx.lineTo(x, base); ctx.stroke();
        }
        ctx.setLineDash([]);
        // the sweep: its range as a bar with end caps, the break frequency now as a dot
        const xl = X(fLo, w), xh = X(fHi, w);
        ctx.fillStyle = "rgba(201,211,230,0.3)"; ctx.fillRect(xl, yBar - 1, xh - xl, 2);
        ctx.fillStyle = C.ink;
        for (const x of [xl, xh]) { ctx.beginPath(); ctx.roundRect(x - 2, yBar - 5, 4, 10, 2); ctx.fill(); }
        ctx.fillStyle = "#fff"; ctx.beginPath(); ctx.arc(X(fc, w), yBar, 3, 0, 2 * Math.PI); ctx.fill();
        const hd = handles[id.depth];
        if (hd) hd.place(canvas, (xl + xh) / 2, yBar, xh - xl + 14, 16);
        if (drag || (hd && hd.focused)) tag(ctx, `depth ${Math.round(WT.get(id.depth) * 100)} %  feed ${WT.get(id.feedback).toFixed(2)}`, w - 6, 15);
      },
    };
  }

  /** Chorus: the dry sound as a centre line, the band the voices swing in (± the depth, dotted edges,
   *  the value in the corner), the three voices (1.0, 1.22, 1.44 × the rate, a third of a cycle apart,
   *  as fx/Chorus.h runs them) scrolling towards "now", where each has a dot; their strength by the
   *  mix. Drag sideways: the rate. */
  function ChorusView(canvas, { ids = {} } = {}) {
    const id = Object.assign({ rate: "ch_rate", depth: "ch_depth", mix: "ch_mix" }, ids);
    const E = Easer(0.1), SCALE = [1, 1.22, 1.44];
    let phase = 0, drag = false;
    UI.dragPicture(canvas, (p) => {
      const r0 = norm(id.rate);
      drag = true;
      return { ids: [id.rate], move(q, ev) { UI.setNorm(id.rate, r0 + (q.x - p.x) / 90 * (ev.shiftKey ? 0.15 : 1)); }, end() { drag = false; } };
    });
    return {
      draw() {
        const dt = E.tick();
        const rate = WT.get(id.rate), depth = E.get("depth", WT.get(id.depth)), mix = E.get("mix", liveOf(id.mix));
        if (!reducedMotion) phase = (phase + rate * dt) % 1000;
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const mid = h / 2 + 5, amp = 7 + Math.max(0, h / 2 - 26) * clamp(depth / 10, 0, 1), xn = w - 20;
        const band = ctx.createLinearGradient(0, 0, xn, 0);
        band.addColorStop(0, "rgba(201,211,230,0)"); band.addColorStop(1, "rgba(201,211,230,0.07)");
        ctx.fillStyle = band; ctx.fillRect(0, mid - amp, xn, 2 * amp);
        ctx.strokeStyle = "rgba(201,211,230,0.2)"; ctx.lineWidth = 1; ctx.setLineDash([2, 4]);
        ctx.beginPath();
        ctx.moveTo(0, Math.round(mid - amp) + 0.5); ctx.lineTo(xn, Math.round(mid - amp) + 0.5);
        ctx.moveTo(0, Math.round(mid + amp) + 0.5); ctx.lineTo(xn, Math.round(mid + amp) + 0.5);
        ctx.stroke(); ctx.setLineDash([]);
        ctx.strokeStyle = "rgba(201,211,230,0.3)"; ctx.beginPath(); ctx.moveTo(0, Math.round(mid) + 0.5); ctx.lineTo(w, Math.round(mid) + 0.5); ctx.stroke();
        ctx.fillStyle = "rgba(255,255,255,0.25)"; ctx.fillRect(xn, mid - amp - 6, 1, 2 * amp + 12);
        // x is time: "now" at xn, 1.25 cycles of the first voice across the picture
        const strong = (0.35 + 0.65 * clamp(mix, 0, 1)).toFixed(3);
        for (let v = 0; v < 3; v++) {
          const y = (x) => mid + amp * Math.sin(2 * Math.PI * (SCALE[v] * (phase - 1.25 * (xn - x) / xn) + v / 3));
          const g = ctx.createLinearGradient(0, 0, xn, 0);
          g.addColorStop(0, "rgba(223,230,243,0)"); g.addColorStop(1, `rgba(238,242,250,${strong})`);
          ctx.beginPath();
          for (let x = 0; x <= xn; x += 2) (x ? ctx.lineTo(x, y(x)) : ctx.moveTo(x, y(x)));
          ctx.strokeStyle = g; ctx.lineWidth = 1.7; ctx.stroke();
          ctx.fillStyle = "#fff"; ctx.beginPath(); ctx.arc(xn + 0.5, y(xn), 3, 0, 2 * Math.PI); ctx.fill();
        }
        ctx.textBaseline = "alphabetic";
        if (drag) tag(ctx, `rate ${UI.format(id.rate, rate)}`, 7, 14, "left");
        else { ctx.font = UI.SANS(11); ctx.fillStyle = C.muted; ctx.fillText("three voices around the dry sound", 7, 14); }
        ctx.font = MONO(10); ctx.textAlign = "right"; ctx.fillStyle = C.ink; ctx.fillText("± " + WT.get(id.depth).toFixed(1) + " ms", w - 6, h - 6);
        ctx.textAlign = "left";
      },
    };
  }

  /** Delay: time 0 … 4 s left to right; the dry hit, then each echo as a bar (left above the line,
   *  right below; ping-pong alternates), heights from the feedback, darker with the tone. A handle on
   *  the first echo: sideways the time, up and down the feedback; the time in the corner. */
  function DelayView(canvas, { ids = {}, handles = {} } = {}) {
    const id = Object.assign({ time: "dly_time", feedback: "dly_fb", tone: "dly_tone", mix: "dly_mix", ping: "dly_pingpong" }, ids);
    const E = Easer(0.1), SPAN = 4;
    let drag = false, shown = "";
    const X = (s, w) => 8 + (s / SPAN) * (w - 14);
    UI.dragPicture(canvas, (p) => {
      const f0 = norm(id.feedback), w = canvas.clientWidth;
      drag = true;
      return {
        ids: [id.time, id.feedback],
        move(q, ev) {
          WT.set(id.time, clamp(((q.x - 8) / (w - 14)) * SPAN, 0.05, SPAN));
          UI.setNorm(id.feedback, f0 + (p.y - q.y) / 80 * (ev.shiftKey ? 0.15 : 1));
        },
        end() { drag = false; },
      };
    });
    return {
      draw() {
        E.tick();
        const T = E.get("time", WT.get(id.time)), fb = E.get("fb", WT.get(id.feedback)), tone = E.get("tone", WT.get(id.tone));
        const mix = E.get("mix", clamp(liveOf(id.mix), 0, 1)), ping = WT.get(id.ping) > 0.5;
        const ht = handles[id.time], hf = handles[id.feedback];
        const hot = drag || (ht && ht.focused) || (hf && hf.focused);
        const sig = [T, fb, tone, mix].map((v) => v.toFixed(4)).join() + ping + hot + canvas.clientWidth + UI.scale;
        if (sig === shown) return;
        shown = sig;
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const mid = h / 2;
        ctx.fillStyle = C.hair; ctx.fillRect(0, Math.round(mid), w, 1);
        ctx.font = MONO(10); ctx.fillStyle = C.faint; ctx.textBaseline = "alphabetic";
        ctx.fillText("L", 5, 14); ctx.fillText("R", 5, h - 6);
        ctx.fillStyle = C.inkHi; ctx.fillRect(X(0, w) - 1, mid - h * 0.36, 2.5, h * 0.72);
        for (let k = 1; k < 64; k++) {
          const s = k * T, a = Math.pow(fb, k - 1) * (0.25 + 0.75 * mix);
          if (s > SPAN || a < 0.03) break;
          const hh = h * 0.36 * a, x = X(s, w) - 1;
          ctx.fillStyle = rgba("ink", 0.35 + 0.6 * (1 - (1 - tone) * Math.min(1, k / 5)));
          if (!ping || k % 2 === 1) ctx.fillRect(x, mid - hh, 2.5, hh);
          if (!ping || k % 2 === 0) ctx.fillRect(x, mid, 2.5, hh);
        }
        handleRing(ctx, X(T, w), mid);
        tag(ctx, WT.get(id.time).toFixed(2) + " s", w - 6, 14);
        if (hot) tag(ctx, `feed ${Math.round(WT.get(id.feedback) * 100)} %`, w - 6, h - 6);
        for (const hd of [ht, hf]) if (hd) hd.place(canvas, X(T, w), mid);
      },
    };
  }

  /** Reverb: the tail's level over time (log time, marks at 1, 5, 20, 60 s) from the decay and the
   *  mix, the shimmer as a brighter layer inside it, the reverb's live level as a dot riding the
   *  curve. A handle on the tail: sideways the decay (written in the title row). */
  function ReverbView(canvas, { ids = {}, handles = {} } = {}) {
    const id = Object.assign({ decay: "rev_decay", mix: "rev_mix", shimmer: "rev_shimmer" }, ids);
    const E = Easer(0.1);
    let drag = false, shown = "";
    UI.dragPicture(canvas, (p) => {
      const d0 = norm(id.decay);
      drag = true;
      return { ids: [id.decay], move(q, ev) { UI.setNorm(id.decay, d0 + (q.x - p.x) / 110 * (ev.shiftKey ? 0.15 : 1)); }, end() { drag = false; } };
    });
    return {
      draw(tel) {
        E.tick();
        const T = E.get("decay", WT.get(id.decay)), mix = E.get("mix", clamp(liveOf(id.mix), 0, 1)), shim = E.get("shim", clamp(liveOf(id.shimmer), 0, 1));
        const live = E.get("live", clamp((tel.verb || 0) * 2, 0.001, 1));
        const hd = handles[id.decay], hot = drag || (hd && hd.focused);
        const sig = [T, mix, shim, live].map((v) => v.toFixed(4)).join() + hot + canvas.clientWidth + UI.scale;
        if (sig === shown) return;
        shown = sig;
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const X = (s) => 6 + (Math.log(1 + s) / Math.log(61)) * (w - 12), base = h - 16;
        const tail = (s, k) => base - (base - 10) * k * Math.pow(10, -3 * s / T / 2.2);
        ctx.font = MONO(10); ctx.textAlign = "center"; ctx.textBaseline = "alphabetic";
        for (const s of [1, 5, 20, 60]) {
          const x = Math.round(X(s)) + 0.5;
          ctx.strokeStyle = C.hairSoft; ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(x, 6); ctx.lineTo(x, base); ctx.stroke();
          ctx.fillStyle = C.faint; ctx.fillText(s + (s === 60 ? " s" : ""), x - (s === 60 ? 8 : 0), h - 4);
        }
        ctx.textAlign = "left";
        const level = 0.35 + 0.65 * mix;
        const area = (k, fill) => {
          ctx.beginPath(); ctx.moveTo(X(0), base);
          for (let s = 0; s <= 60; s += 0.25) ctx.lineTo(X(s), tail(s, k));
          ctx.lineTo(X(60), base); ctx.closePath(); ctx.fillStyle = fill; ctx.fill();
        };
        area(level, "rgba(201,211,230,0.16)");
        if (shim > 0.01) area(level * Math.min(1, shim * 1.4) * 0.9, "rgba(238,242,250,0.32)");
        ctx.beginPath();
        for (let s = 0; s <= 60; s += 0.25) (s ? ctx.lineTo(X(s), tail(s, level)) : ctx.moveTo(X(s), tail(s, level)));
        ctx.strokeStyle = C.inkHi; ctx.lineWidth = 1.6; ctx.stroke();
        // the reverb's level now, where the tail is at that level
        const sl = Math.min(60, -(2.2 * T / 3) * Math.log10(live));
        ctx.fillStyle = "#fff"; ctx.beginPath(); ctx.arc(X(sl), tail(sl, level), 2.5, 0, 2 * Math.PI); ctx.fill();
        const hx = X(T * 0.37), hy = tail(T * 0.37, level);
        handleRing(ctx, hx, hy);
        if (hd) hd.place(canvas, hx, hy);
        if (hot) tag(ctx, UI.format(id.decay, WT.get(id.decay)), w - 6, 15);
      },
    };
  }

  const dbOf = (g) => (g > 1e-5 ? 20 * Math.log10(g) : -100);

  /** Output (upright, the mockup's drawOut): the two channels' peaks on a dB scale, the limiter's
   *  ceiling as a white line that is dragged up and down, and its gain reduction as a thin bar
   *  hanging from it. The auto-gain is in the bottom bar. */
  function OutputView(canvas, { ids = {}, handles = {} } = {}) {
    const id = Object.assign({ ceiling: "lim_ceiling" }, ids);
    const E = Easer(0.1), MIN = -48;
    let drag = false;
    const span = () => canvas.clientHeight - 26;
    UI.dragPicture(canvas, (p) => {
      const c0 = WT.get(id.ceiling);
      drag = true;
      return { ids: [id.ceiling], move(q, ev) { WT.set(id.ceiling, c0 - ((q.y - p.y) / span()) * -MIN * (ev.shiftKey ? 0.15 : 1)); }, end() { drag = false; } };
    });
    return {
      draw(tel) {
        E.tick();
        const ceiling = E.get("ceiling", WT.get(id.ceiling));
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const top = 10, bot = h - 16, x1 = w - 30;
        const Y = (db) => top + (bot - top) * clamp(db / MIN, 0, 1);
        ctx.font = MONO(10); ctx.textAlign = "right"; ctx.textBaseline = "middle";
        for (const db of [0, -12, -24, -36]) {
          ctx.fillStyle = C.faint; ctx.fillText(String(db).replace("-", "−"), w - 4, Y(db));
          ctx.fillStyle = C.hairSoft; ctx.fillRect(8, Math.round(Y(db)), x1 - 8, 1);
        }
        const grad = ctx.createLinearGradient(0, Y(0), 0, Y(MIN));
        grad.addColorStop(0, C.inkHi); grad.addColorStop(1, "rgba(201,211,230,0.35)");
        [tel.peakL || 0, tel.peakR || 0].forEach((g, ch) => {
          const x = 12 + ch * 18, y = Y(clamp(dbOf(g), MIN, 0));
          ctx.fillStyle = C.track; ctx.fillRect(x, Y(0), 12, Y(MIN) - Y(0));
          ctx.fillStyle = grad; ctx.fillRect(x, y, 12, Y(MIN) - y);
        });
        const yc = Math.round(Y(ceiling));
        ctx.fillStyle = "#fff"; ctx.fillRect(8, yc - 1, x1 - 8, 1.5);
        const grDb = -dbOf(tel.gr || 1);
        if (grDb > 0.05) { ctx.fillStyle = rgba("hi", 0.8); ctx.fillRect(x1 - 4, yc, 2, Math.min(bot, Y(ceiling - grDb)) - yc); }
        ctx.fillStyle = C.ink; ctx.textAlign = "center"; ctx.textBaseline = "alphabetic";
        ctx.fillText("L", 18, h - 4); ctx.fillText("R", 36, h - 4);
        ctx.textAlign = "left";
        const hd = handles[id.ceiling];
        if (hd) hd.place(canvas, (8 + x1) / 2, yc - 0.25, x1 - 4, 12);
        if (drag || (hd && hd.focused)) {
          const t = WT.get(id.ceiling).toFixed(1).replace("-", "−");
          ctx.font = MONO(10); ctx.fillStyle = "rgba(5,7,13,0.85)"; ctx.fillRect(8, yc + 3, ctx.measureText(t).width + 6, 13);
          ctx.fillStyle = C.inkHi; ctx.fillText(t, 11, yc + 13);
        }
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

  Object.assign(UI, { MagnetogramView, LayersView, DriveView, FilterView, KeyboardView, PhaserView, ChorusView, DelayView, ReverbView, OutputView, HeaderMeter, dbOf, phaserResponse, phaserBreak });
})();
