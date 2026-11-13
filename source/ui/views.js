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

  Object.assign(UI, { MagnetogramView, LayersView, DriveView, FilterView, KeyboardView, PhaserView, ChorusView, DelayView, ReverbView, OutputView, HeaderMeter, dbOf });
})();
