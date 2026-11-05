/* Canvas visualisations. Each view exposes draw(state); app.js calls them per frame. */
(function () {
  "use strict";
  const WT = window.WT, UI = window.UI, C = UI.COLORS;

  const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
  const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
  UI.noteName = (n) => NOTE_NAMES[((n % 12) + 12) % 12] + (Math.floor (n / 12) - 1);

  function fmtUtc(utc, withDate) {
    const d = new Date(utc * 1000);
    const hh = String(d.getUTCHours()).padStart(2, "0"), mm = String(d.getUTCMinutes()).padStart(2, "0");
    if (!withDate) return `${hh}:${mm}`;
    const mon = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"][d.getUTCMonth()];
    return `${d.getUTCDate()} ${mon} ${hh}:${mm}`;
  }
  UI.fmtTime = fmtUtc;

  const LAYER_COLORS = ["#ff7a5c", "#6be3a4", "#a98bff"];
  const LAYER_NAMES = ["RED", "GREEN", "VIOLET"];

  // ======================================================================
  // Chord + voices — the big display of the harmony panel (T3.6)
  // ======================================================================
  function ChordView(canvas) {
    return {
      draw(S) {
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const chord = S.tel.chord;

        // pitch axis: MIDI 24 … 100, octave grid
        const pad = 12, lo = 24, hi = 100;
        const X = (note) => pad + ((note - lo) / (hi - lo)) * (w - 2 * pad);

        ctx.font = "8px -apple-system, sans-serif";
        ctx.textAlign = "center";
        for (let n = 24; n <= 96; n += 12) {
          const x = X(n);
          ctx.strokeStyle = C.grid;
          ctx.beginPath(); ctx.moveTo(x, 60); ctx.lineTo(x, h - 10); ctx.stroke();
          ctx.fillStyle = C.faint;
          ctx.fillText(UI.noteName(n), x, h - 2);
        }
        ctx.textAlign = "left";

        // the chord itself
        ctx.fillStyle = C.accentHot;
        ctx.font = "700 26px -apple-system, sans-serif";
        ctx.fillText(chord && chord.label ? chord.label : "—", pad, 34);
        if (chord && chord.roman) {
          ctx.fillStyle = C.text;
          ctx.font = "11px -apple-system, sans-serif";
          ctx.fillText(chord.roman, pad, 50);
        }
        if (chord && (chord.latched || chord.pending)) {
          const badge = chord.latched ? "MIDI" : "→ NEXT";
          const bw = ctx.measureText(badge).width + 12;
          ctx.fillStyle = chord.latched ? "rgba(95,184,255,0.16)" : "rgba(255,201,60,0.14)";
          ctx.strokeStyle = chord.latched ? "rgba(95,184,255,0.5)" : "rgba(255,201,60,0.45)";
          ctx.fillRect(w - pad - bw, 14, bw, 18);
          ctx.strokeRect(w - pad - bw, 14, bw, 18);
          ctx.fillStyle = chord.latched ? C.blue : C.accent;
          ctx.font = "9px -apple-system, sans-serif";
          ctx.fillText(badge, w - pad - bw + 6, 27);
        }

        if (!chord || !chord.notes || !chord.notes.length) {
          ctx.fillStyle = C.faint;
          ctx.font = "10px -apple-system, sans-serif";
          ctx.fillText("waiting for the sky…", pad, h / 2 + 20);
          return;
        }

        // the three layers: one lane per emission colour, a bar per voice at its pitch
        const laneTop = 74, laneGap = Math.max(20, (h - laneTop - 24) / 3);
        for (let l = 0; l < 3; l++) {
          const notes = chord.notes[l] || [];
          const y = laneTop + l * laneGap;
          ctx.strokeStyle = C.grid;
          ctx.beginPath(); ctx.moveTo(pad, y); ctx.lineTo(w - pad, y); ctx.stroke();

          ctx.fillStyle = LAYER_COLORS[l];
          ctx.globalAlpha = 0.35;
          ctx.font = "7.5px -apple-system, sans-serif";
          ctx.fillText(LAYER_NAMES[l], pad, y - 3);
          ctx.globalAlpha = 1;

          for (let i = 0; i < notes.length; i++) {
            const note = notes[i];
            const x = X(note);
            const a = 0.25 + 0.65 * (1 - i / Math.max(1, notes.length - 1));
            ctx.globalAlpha = a;
            ctx.fillStyle = LAYER_COLORS[l];
            ctx.fillRect(x - 1, y - 7, 2.2, 13);
          }
          ctx.globalAlpha = 1;
        }

        // chord tones as faint vertical bands over the lanes
        if (chord.label && chord.notes[0]) {
          const lane = chord.notes[0];
          for (let i = 0; i < lane.length; i++) {
            const pc = ((lane[i] % 12) + 12) % 12;
            ctx.fillStyle = (pc === 0 || pc === 7) ? "rgba(255,201,60,0.07)" : "rgba(255,201,60,0.035)";
            ctx.fillRect(X(lane[i]) - 3, 60, 6, h - 70);
          }
        }
      },
    };
  }

  // ======================================================================
  // Delay taps
  // ======================================================================
  function DelayView(canvas) {
    return {
      draw() {
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const t = Math.max (0.02, WT.get("dly_time"));
        const fb = WT.get("dly_fb"), mix = WT.get("dly_mix"), pp = WT.get("dly_pingpong") > 0.5;
        const tone = WT.get("dly_tone");
        const span = Math.max (4 * t, t * 8.5);
        const mid = h / 2, pad = 8, amp = h / 2 - 12;
        const X = (sec) => pad + (sec / span) * (w - 2 * pad);

        ctx.strokeStyle = C.gridStrong;
        ctx.beginPath(); ctx.moveTo(pad, mid); ctx.lineTo(w - pad, mid); ctx.stroke();

        ctx.fillStyle = "rgba(95,184,255,0.7)";
        ctx.fillRect(X(0) - 1, mid - amp * 0.9, 2, amp * 1.8);

        let level = 1;
        ctx.globalAlpha = 0.35 + 0.65 * Math.min(1, mix * 2);
        for (let k = 1; k <= 24 && t * k < span; k++) {
          const up = !pp || k % 2 === 1;
          const hgt = level * amp * 0.9 * Math.max(0.2, 1 - 0.05 * (1 - tone) * k);
          if (hgt < 0.5) break;
          const x = X(t * k);
          ctx.shadowColor = C.accentGlow; ctx.shadowBlur = 8;
          ctx.fillStyle = C.accent;
          if (pp) ctx.fillRect(x - 1.5, up ? mid - hgt : mid, 3, hgt);
          else ctx.fillRect(x - 1.5, mid - hgt, 3, hgt * 2);
          ctx.shadowBlur = 0;
          level *= fb;
        }
        ctx.globalAlpha = 1;
        ctx.font = "9px -apple-system, sans-serif";
        ctx.fillStyle = C.text;
        ctx.fillText(`${(t * 1000).toFixed(0)} ms · ${fb.toFixed(2)} fb`, pad + 2, 12);
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
    const xOf = (f, w) => (Math.log (f / fMin) / Math.log (fMax / fMin)) * w;
    const yOf = (t, h) => h - 14 - (Math.log (t / tMin) / Math.log (tMax / tMin)) * (h - 26);

    return {
      draw(S) {
        const { ctx, w, h } = UI.fit(canvas);
        ctx.clearRect(0, 0, w, h);
        const decay = WT.get("rev_decay"), damp = WT.get("rev_damp"), shimmer = WT.get("rev_shimmer");
        const mix = WT.get("rev_mix");
        const fd = 1200 * Math.pow (15, 1 - damp);
        const level = clamp ((S.tel.verb || 0) * 4, 0, 1);

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

        const rt = (f) => decay / Math.sqrt (1 + Math.pow (f / fd, 2)) * (1 + 0.15 * Math.exp (-f / 200));
        const top = ctx.createLinearGradient(0, 0, 0, h);
        top.addColorStop(0, `rgba(255,201,60,${(0.22 + 0.25 * level * mix).toFixed(3)})`);
        top.addColorStop(1, "rgba(255,201,60,0.0)");
        ctx.beginPath(); ctx.moveTo(0, h);
        for (let x = 0; x <= w; x += 2) ctx.lineTo(x, yOf(clamp (rt(fMin * Math.pow (fMax / fMin, x / w)), tMin, tMax), h));
        ctx.lineTo(w, h); ctx.closePath();
        ctx.fillStyle = top; ctx.fill();
        ctx.shadowColor = C.accentGlow; ctx.shadowBlur = 12;
        ctx.strokeStyle = C.accent; ctx.lineWidth = 2;
        ctx.beginPath();
        for (let x = 0; x <= w; x += 2) { const y = yOf(clamp (rt(fMin * Math.pow (fMax / fMin, x / w)), tMin, tMax), h); x ? ctx.lineTo(x, y) : ctx.moveTo(x, y); }
        ctx.stroke();
        ctx.shadowBlur = 0;

        if (shimmer > 0.01) {
          const rate = shimmer * (0.4 + 2.5 * level);
          if (Math.random() < rate) {
            const f = 600 * Math.pow (20, Math.random());
            sparks.push({ x: xOf(f, w), y: yOf(clamp (rt(f), tMin, tMax), h), vy: -0.25 - Math.random() * 0.5, vx: 0.25 + Math.random() * 0.4, life: 1 });
          }
        }
        for (let i = sparks.length - 1; i >= 0; i--) {
          const s = sparks[i];
          s.x += s.vx; s.y += s.vy; s.life -= 0.012;
          if (s.life <= 0 || s.y < 0) { sparks.splice(i, 1); continue; }
          ctx.fillStyle = `rgba(255,236,170,${(s.life * 0.9).toFixed(3)})`;
          ctx.beginPath(); ctx.arc(s.x, s.y, 1.2 + s.life, 0, Math.PI * 2); ctx.fill();
        }

        ctx.fillStyle = C.text; ctx.font = "9px -apple-system, sans-serif";
        ctx.textAlign = "right";
        ctx.fillText(`${decay.toFixed(1)} s · size ${Math.round(WT.get("rev_size") * 100)}% · pre ${WT.get("rev_predelay").toFixed(0)} ms`, w - 6, 12);
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
        const Y = (db) => top + (clamp (db, dbFloor, 0) / dbFloor) * (bottom - top);
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
        ctx.fillStyle = "#081322"; ctx.fillRect(32, top, 7, bottom - top);
        const grH = clamp (gr / 12, 0, 1) * (bottom - top);
        ctx.shadowColor = C.accentGlow; ctx.shadowBlur = 6;
        ctx.fillStyle = C.accent; ctx.fillRect(32, top, 7, grH);
        ctx.shadowBlur = 0;

        const ceil = WT.get("lim_ceiling");
        ctx.strokeStyle = "rgba(255,99,99,0.8)";
        ctx.beginPath(); ctx.moveTo(4, Y(ceil)); ctx.lineTo(29, Y(ceil)); ctx.stroke();

        ctx.font = "8px -apple-system, sans-serif"; ctx.fillStyle = C.faint;
        for (const db of [0, -12, -24, -36]) ctx.fillText(String(db), 42, Y(db) + 3);
        ctx.fillText("L R GR", 4, h - 3);
      },
    };
  }

  Object.assign(UI, { ChordView, DelayView, ReverbView, MeterView });
})();
