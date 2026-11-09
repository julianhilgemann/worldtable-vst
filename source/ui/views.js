/* The instrument layer's live pictures (spec/06, T6.1). Each view redraws only when what it shows
 * has changed (or, for the moving ones, only while its tab is visible), and every change glides. */
(function () {
  "use strict";
  const UI = window.UI, C = UI.C, rgba = UI.rgba, MONO = UI.MONO;
  const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
  const ease = (t) => (t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2);
  const LAYERS = ["red", "green", "violet"];
  const now = () => performance.now() / 1000;

  // ================================================================ voicing
  /** Every resonator voice on a pitch axis, one lane per layer. On a chord change the new voices
   *  glide in from the nearest old ones (the voice-leading, made visible) while the old fade. */
  function VoicingView(canvas) {
    const LO = 24, HI = 108, GLIDE = 3.2;
    let marks = [];
    let signature = "";
    let emission = [0.5, 0.5, 0.5], drawnEmission = [-1, -1, -1];
    let levels = [1, 0.7, 0.4];
    let animating = true, lastW = 0;

    function setChord(notes) {
      if (!notes) return;
      const sig = notes.map((l) => [...new Set(l)].sort((a, b) => a - b).join(",")).join("|");
      if (sig === signature) return;
      signature = sig;
      const t = now();
      const old = marks.filter((m) => !m.dying);
      for (const m of old) { m.pos = posOf(m, t); m.alpha = alphaOf(m, t); m.from = m.pos; m.to = m.pos; m.a0 = m.alpha; m.a1 = 0; m.t0 = t; m.dying = true; }
      const fresh = [];
      notes.forEach((layerNotes, l) => {
        const counts = new Map();
        for (const n of layerNotes) counts.set(n, (counts.get(n) || 0) + 1);
        const total = layerNotes.length || 1;
        for (const [n, c] of counts) {
          let from = n, best = 8;
          for (const m of old) if (m.layer === l && Math.abs(m.to - n) < best) { best = Math.abs(m.to - n); from = m.to; }
          fresh.push({ layer: l, from, to: n, a0: 0, a1: 0.55 + 0.45 * Math.min(1, (c / total) * 3), t0: t, dying: false });
        }
      });
      marks = marks.filter((m) => m.dying && alphaOf(m, t) > 0.01).concat(fresh);
      animating = true;
    }
    const k = (m, t) => ease(clamp((t - m.t0) / GLIDE, 0, 1));
    const posOf = (m, t) => m.from + (m.to - m.from) * k(m, t);
    const alphaOf = (m, t) => m.a0 + (m.a1 - m.a0) * k(m, t);

    function setEmission(e, lv) {
      emission = e;
      levels = lv;
    }

    function draw() {
      const t = now();
      const moved = emission.some((e, i) => Math.abs(e - drawnEmission[i]) > 0.02);
      if (!animating && !moved && canvas.clientWidth === lastW) return;
      lastW = canvas.clientWidth;
      drawnEmission = emission.slice();
      const { ctx, w, h } = UI.fit(canvas);
      ctx.clearRect(0, 0, w, h);
      const x0 = 16, x1 = w - 4;
      const x = (n) => x0 + (x1 - x0) * (n - LO) / (HI - LO);
      const lane = [[35, 48], [19, 32], [3, 16]];   // red low, green, violet high

      // semitone ruler and octave lines
      ctx.font = MONO(8);
      ctx.textBaseline = "alphabetic";
      for (let n = LO; n <= HI; n++) {
        const isC = n % 12 === 0, xx = Math.round(x(n)) + 0.5;
        ctx.strokeStyle = isC ? C.hair : C.hairSoft;
        ctx.beginPath();
        if (isC) { ctx.moveTo(xx, 2); ctx.lineTo(xx, 50); } else { ctx.moveTo(xx, 49); ctx.lineTo(xx, 51); }
        ctx.stroke();
        if (isC && n < HI) { ctx.fillStyle = C.faint; ctx.fillText("C" + (n / 12 - 1), xx + 3, h - 1); }
      }
      LAYERS.forEach((name, l) => {
        const yy = Math.round((lane[l][0] + lane[l][1]) / 2) + 0.5;
        ctx.strokeStyle = rgba(name, 0.07);
        ctx.beginPath(); ctx.moveTo(x0, yy); ctx.lineTo(x1, yy); ctx.stroke();
        ctx.fillStyle = rgba(name, 0.7);
        ctx.beginPath(); ctx.arc(5, (lane[l][0] + lane[l][1]) / 2, 1.8, 0, 2 * Math.PI); ctx.fill();
      });

      // the voices
      let busy = false;
      ctx.lineCap = "round";
      for (const m of marks) {
        const kk = k(m, t);
        if (kk < 1) busy = true;
        const a = alphaOf(m, t) * (0.3 + 0.7 * emission[m.layer]) * (0.35 + 0.65 * levels[m.layer]);
        if (a < 0.01) continue;
        const name = LAYERS[m.layer], xx = x(posOf(m, t)), [y0, y1] = lane[m.layer];
        ctx.strokeStyle = rgba(name, clamp(a * 1.15, 0, 1));
        ctx.lineWidth = 2.5;
        ctx.beginPath(); ctx.moveTo(xx, y0 + 1); ctx.lineTo(xx, y1 - 1); ctx.stroke();
      }
      marks = marks.filter((m) => !(m.dying && k(m, t) >= 1));
      animating = busy;
    }

    return { setChord, setEmission, draw };
  }

  // ================================================================ phase track and ticker
  function PhaseTrack(root) {
    const cells = [...root.children];
    const NAMES = ["quiet", "growth", "onset", "recovery"];
    let current = "";
    return {
      set(phase, progress) {
        if (phase !== current) {
          cells.forEach((c, i) => c.classList.toggle("on", NAMES[i] === phase));
          current = phase;
        }
        const i = NAMES.indexOf(phase);
        if (i >= 0) cells[i].querySelector("i").style.width = (clamp(progress, 0, 1) * 100).toFixed(1) + "%";
      },
    };
  }

  function Ticker(ol, max = 3) {
    return {
      push(html) {
        const li = document.createElement("li");
        li.className = "fresh";
        li.innerHTML = html;
        ol.insertBefore(li, ol.firstChild);
        while (ol.children.length > max) ol.removeChild(ol.lastChild);
      },
    };
  }

  // ================================================================ magnetogram
  const PHASE_FILL = ["rgba(138,150,173,0.22)", "rgba(143,123,255,0.55)", "rgba(92,242,160,0.95)", "rgba(92,242,160,0.38)"];

  function MagnetogramView(canvas) {
    const base = document.createElement("canvas");
    let day = null, dirty = true, lastX = -1, lastW = 0, lastK = 0;
    const L = 118, PAD_R = 10;
    const lanes = { mag: [6, 37], load: [40, 48], phase: [51, 56] };

    function setDay(d) { day = d; dirty = true; }

    function drawBase(w, h, k) {
      base.width = Math.round(w * k); base.height = Math.round(h * k);
      const ctx = base.getContext("2d");
      ctx.setTransform(k, 0, 0, k, 0, 0);
      ctx.clearRect(0, 0, w, h);
      const R = w - PAD_R, n = day.phase.length;
      const x = (i) => L + (R - L) * (i / Math.max(1, n - 1));

      ctx.font = MONO(8);
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
      ctx.font = MONO(10);
      ctx.fillStyle = C.ink; ctx.fillText("Magnetogram", 10, 14);
      ctx.fillStyle = C.muted; ctx.fillText(`ΔH ±${Math.round(max)} nT`, 10, 26);
      ctx.fillStyle = C.green; ctx.fillText("— " + (day.station ? day.station.code : "CMO"), 10, 37);
      ctx.fillStyle = C.violet; ctx.fillText("— model", 52, 37);
      ctx.fillStyle = C.ink; ctx.fillText("energy", 10, 48);
      ctx.fillStyle = C.red; ctx.fillText("E", 56, 48);
      ctx.fillStyle = C.muted; ctx.fillText("phase", 10, 58);
      dirty = false;
    }

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
    }

    return { setDay, draw };
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
        ctx.font = MONO(8); ctx.fillStyle = C.faint; ctx.strokeStyle = C.hairSoft;
        for (const t of [1, 5, 10, 30]) {
          const xx = Math.round(xt(t)) + 0.5;
          ctx.beginPath(); ctx.moveTo(xx, top); ctx.lineTo(xx, bot); ctx.stroke();
          ctx.fillText(t + (t === 30 ? " s" : ""), xx - (t === 30 ? 14 : 2), h - 2);
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
          ctx.strokeStyle = C.red; ctx.lineWidth = 1.25; ctx.stroke();
          const lx = clamp(level, 0, 1);
          ctx.fillStyle = C.red;
          ctx.beginPath(); ctx.arc(cx + lx * r, cy - f(lx) * r, 2, 0, 2 * Math.PI); ctx.fill();
        });
      },
    };
  }

  const LOGF = (f, w) => (Math.log2(f / 20) / Math.log2(1000)) * w;

  /** The filter (T3.7): its response now (after the sky) over the spectrum, the knob's cutoff as a
   *  tick, so the sky's opening shows as the distance between them. The SVF's analog magnitude. */
  function FilterView(canvas) {
    const paint = cache(canvas);
    const DB_TOP = 15, DB_BOT = -39;
    function mag(type, k, x) {
      const d = Math.sqrt((1 - x * x) ** 2 + (k * x) ** 2);
      const lp = 1 / d;
      if (type === 0) return lp;
      if (type === 1) return lp / Math.sqrt((1 - x * x) ** 2 + 2 * x * x);
      if (type === 2) return (k * x) / d;
      return (x * x) / d;
    }
    return {
      draw(p, spectrum) {
        const spec = spectrum ? spectrum.length : 0;
        const sig = [p.type, p.hz.toPrecision(3), p.base.toPrecision(3), p.reso.toFixed(2), spec ? spectrum[20] + spectrum[60] + spectrum[100] : 0].join();
        paint(sig, (ctx, w, h) => {
          const top = 3, bot = h - 3;
          const y = (db) => top + (bot - top) * clamp((DB_TOP - db) / (DB_TOP - DB_BOT), 0, 1);
          if (spec) {
            ctx.beginPath(); ctx.moveTo(0, bot);
            spectrum.forEach((db, i) => ctx.lineTo((i / (spec - 1)) * w, bot - clamp((db + 90) / 80, 0, 1) * (bot - top)));
            ctx.lineTo(w, bot); ctx.closePath(); ctx.fillStyle = "rgba(201,211,230,0.07)"; ctx.fill();
          }
          for (const f of [100, 1000, 10000]) { const xx = Math.round(LOGF(f, w)) + 0.5; ctx.strokeStyle = C.hairSoft; ctx.beginPath(); ctx.moveTo(xx, top); ctx.lineTo(xx, bot); ctx.stroke(); }
          const y0 = Math.round(y(0)) + 0.5;
          ctx.setLineDash([2, 3]); ctx.strokeStyle = C.hair; ctx.beginPath(); ctx.moveTo(0, y0); ctx.lineTo(w, y0); ctx.stroke(); ctx.setLineDash([]);

          const k = 2 - 1.75 * clamp(p.reso, 0, 1);
          ctx.beginPath();
          for (let i = 0; i <= 160; i++) {
            const f = 20 * Math.pow(1000, i / 160);
            const db = 20 * Math.log10(Math.max(1e-6, mag(p.type, k, f / p.hz)));
            i ? ctx.lineTo((i / 160) * w, y(db)) : ctx.moveTo(0, y(db));
          }
          ctx.strokeStyle = C.violet; ctx.lineWidth = 1.25; ctx.stroke();

          // the knob's cutoff (green tick) and where the sky has moved it (the violet dot)
          if (p.base < 19950) {
            const xb = Math.round(LOGF(p.base, w)) + 0.5;
            ctx.strokeStyle = rgba("green", 0.7); ctx.beginPath(); ctx.moveTo(xb, bot - 6); ctx.lineTo(xb, bot); ctx.stroke();
          }
          if (p.hz < 19950) {
            const xh = LOGF(p.hz, w), atCut = p.type === 2 ? 0 : 20 * Math.log10(mag(p.type, k, 1));
            ctx.fillStyle = C.violet; ctx.beginPath(); ctx.arc(xh, y(atCut), 2, 0, 2 * Math.PI); ctx.fill();
          }
          ctx.font = MONO(9); ctx.fillStyle = C.faint; ctx.textAlign = "right";
          ctx.fillText(p.hz >= 19950 ? "open" : p.hz >= 1000 ? (p.hz / 1000).toFixed(2) + " kHz" : Math.round(p.hz) + " Hz", w - 2, top + 9);
          ctx.textAlign = "left";
        });
      },
    };
  }

  /** Play mode's keyboard strip (T3.7), C1 to C8 like the voicing view: held notes bright, released
   *  ones fading with their envelope, the low resonator's note as a dot under its key. */
  function KeysView(canvas) {
    const LO = 24, HI = 108;
    const BLACK = [1, 3, 6, 8, 10];
    const paint = cache(canvas);
    return {
      draw(voices, lowNote) {
        const sig = (voices || []).map((v) => v[0] + ":" + (v[1] * 20 | 0) + (v[2] ? "h" : "")).join() + "|" + lowNote;
        paint(sig, (ctx, w, h) => {
          const kb = h - 9;
          const whites = [];
          for (let n = LO; n <= HI; n++) if (!BLACK.includes(n % 12)) whites.push(n);
          const ww = w / whites.length;
          const xOfWhite = new Map(whites.map((n, i) => [n, i * ww]));
          const keyRect = (n) => BLACK.includes(n % 12)
            ? [xOfWhite.get(n - 1) + ww * 0.68, 0, ww * 0.64, kb * 0.6]
            : [xOfWhite.get(n), 0, ww, kb];
          const lit = new Map();
          for (const [note, level, held] of voices || []) {
            if (note < LO || note > HI) continue;
            const a = held ? 0.45 + 0.55 * clamp(level, 0, 1) : 0.6 * clamp(level, 0, 1);
            lit.set(note, Math.max(lit.get(note) || 0, a));
          }
          // white keys, then black keys over them
          for (const n of whites) {
            const [x, y0, kw, kh] = keyRect(n);
            const a = lit.get(n) || 0;
            if (a > 0.01) { ctx.fillStyle = rgba("violet", a * 0.8); ctx.fillRect(x + 0.5, y0, kw - 1, kh); }
            ctx.strokeStyle = C.hairSoft; ctx.strokeRect(Math.round(x) + 0.5, 0.5, Math.round(kw), kh - 1);
          }
          for (let n = LO; n <= HI; n++) {
            if (!BLACK.includes(n % 12)) continue;
            const [x, y0, kw, kh] = keyRect(n);
            const a = lit.get(n) || 0;
            ctx.fillStyle = a > 0.01 ? rgba("violet", 0.25 + a * 0.75) : "rgba(201,211,230,0.13)";
            ctx.fillRect(x, y0, kw, kh);
          }
          if (lowNote >= LO && lowNote <= HI) {
            const [x, , kw] = keyRect(lowNote);
            ctx.fillStyle = C.ink; ctx.beginPath(); ctx.arc(x + kw / 2, kb - 4, 1.6, 0, 2 * Math.PI); ctx.fill();
          }
          ctx.font = MONO(8); ctx.fillStyle = C.faint;
          for (let n = LO; n < HI; n += 12) ctx.fillText("C" + (n / 12 - 1), xOfWhite.get(n) + 2, h - 1);
        });
      },
    };
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
        ctx.strokeStyle = C.violet; ctx.lineWidth = 1.25; ctx.stroke();
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
          ctx.strokeStyle = rgba("violet", 0.25 + 0.6 * p.mix * (1 - v * 0.22));
          ctx.lineWidth = 1.1; ctx.stroke();
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
          ctx.font = MONO(8); ctx.fillStyle = C.faint; ctx.fillText("L", w - 9, 9); ctx.fillText("R", w - 9, h - 3);
          ctx.strokeStyle = C.ink; ctx.lineWidth = 1.5;
          ctx.beginPath(); ctx.moveTo(x(0) + 0.5, mid - (mid - 4)); ctx.lineTo(x(0) + 0.5, mid + (mid - 4)); ctx.stroke();
          let a = 1;
          for (let n = 1; n * p.time <= span && a > 0.02; n++) {
            const xx = Math.round(x(n * p.time)) + 0.5, len = (mid - 4) * a;
            ctx.strokeStyle = rgba("green", clamp(0.35 + 0.65 * p.mix, 0, 1) * (0.4 + 0.6 * a));
            ctx.lineWidth = 1.5;
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
          tail(p.decay, "rgba(201,211,230,0.85)", `rgba(201,211,230,${0.04 + 0.08 * p.mix})`);
          tail(p.decay * (1 - 0.75 * p.damp), C.violet, null);
          // shimmer: the octave and the fifth, rising out of the tail
          const sh = Math.round(14 * p.shimmer);
          for (let i = 0; i < sh; i++) {
            const tt = t0 + span * (0.1 + 0.8 * ((i * 0.618) % 1)), yy = y(-12 - 40 * ((i * 0.381) % 1));
            ctx.fillStyle = rgba("violet", 0.35 + 0.5 * p.shimmer);
            ctx.beginPath(); ctx.arc(x(tt), yy, 1.1, 0, 2 * Math.PI); ctx.fill();
          }
          // live wet level
          ctx.fillStyle = C.hair; ctx.fillRect(w - 5, top, 2, bot - top);
          const lv = clamp(wet * 2, 0, 1);
          ctx.fillStyle = C.violet; ctx.fillRect(w - 5, bot - lv * (bot - top), 2, lv * (bot - top));
        });
      },
    };
  }

  const dbOf = (g) => (g > 1e-5 ? 20 * Math.log10(g) : -100);

  function OutputView(canvas) {
    return {
      draw(peakL, peakR, gr, ceiling) {
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const x0 = 12, x1 = w - 4, MIN = -48;
        const x = (db) => x0 + (x1 - x0) * clamp((db - MIN) / -MIN, 0, 1);
        ctx.font = MONO(8); ctx.fillStyle = C.faint;
        for (const db of [-36, -24, -12, 0]) { const xx = Math.round(x(db)) + 0.5; ctx.fillStyle = C.faint; ctx.fillText(String(db).replace("-", "−"), xx - (db === 0 ? 5 : 8), h - 1); ctx.strokeStyle = C.hairSoft; ctx.beginPath(); ctx.moveTo(xx, 3); ctx.lineTo(xx, h - 10); ctx.stroke(); }
        const bar = (y, g, label) => {
          ctx.fillStyle = C.faint; ctx.fillText(label, 1, y + 5);
          ctx.fillStyle = C.hair; ctx.fillRect(x0, y, x1 - x0, 4);
          const xx = x(dbOf(g));
          const grad = ctx.createLinearGradient(x0, 0, x1, 0);
          grad.addColorStop(0, "rgba(143,123,255,0.7)"); grad.addColorStop(0.75, C.green); grad.addColorStop(1, C.green);
          ctx.fillStyle = grad; ctx.fillRect(x0, y, Math.max(0, xx - x0), 4);
        };
        bar(6, peakL, "L"); bar(16, peakR, "R");
        const grDb = -dbOf(gr);
        if (grDb > 0.05) { const xc = x(ceiling); ctx.fillStyle = rgba("red", 0.8); ctx.fillRect(xc - (x1 - x0) * Math.min(1, grDb / 12), 26, (x1 - x0) * Math.min(1, grDb / 12), 3); }
        const xc = Math.round(x(ceiling)) + 0.5;
        ctx.strokeStyle = C.red; ctx.beginPath(); ctx.moveTo(xc, 3); ctx.lineTo(xc, 30); ctx.stroke();
      },
    };
  }

  function HeaderMeter(canvas) {
    let hold = [0, 0], holdT = [0, 0];
    return {
      draw(peakL, peakR, ceiling) {
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const MIN = -48, x = (db) => w * clamp((db - MIN) / -MIN, 0, 1), t = now();
        [peakL, peakR].forEach((g, i) => {
          const y = 8 + i * 6, db = dbOf(g);
          if (db > hold[i] || t - holdT[i] > 1.5) { hold[i] = db; holdT[i] = t; }
          ctx.fillStyle = C.hair; ctx.fillRect(0, y, w, 2);
          ctx.fillStyle = db > ceiling - 0.5 ? C.red : C.green; ctx.fillRect(0, y, x(db), 2);
          ctx.fillStyle = C.ink; ctx.fillRect(Math.max(0, x(hold[i]) - 1), y - 1, 1, 4);
        });
        ctx.fillStyle = C.red; ctx.fillRect(Math.round(x(ceiling)), 5, 1, 12);
      },
    };
  }

  Object.assign(UI, { VoicingView, PhaseTrack, Ticker, MagnetogramView, LayersView, DriveView, FilterView, KeysView, PhaserView, ChorusView, DelayView, ReverbView, OutputView, HeaderMeter, dbOf });
})();
