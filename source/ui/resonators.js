/* The resonator display (T6.11, spec/06 §2 rule 4): one picture for the resonators on a pitch axis
 * (MIDI 20 … 99). The four bands side by side, each as high as its level; the floor, the three
 * crossovers and the ceiling as grips; the notes ringing as lines inside the bands. Drag a grip
 * sideways (Shift: all five together), drag a band up or down for its level, click a band to select
 * it: the band row under the picture belongs to the selection. Every grip and level has a focusable
 * proxy on its handle (spec/06 §5). The sound is the engine's: the picture only sets parameters. */
(function () {
  "use strict";
  const WT = window.WT, UI = window.UI, C = UI.C, rgba = UI.rgba, MONO = UI.MONO, SANS = UI.SANS, clamp = UI.clamp;

  const EDGES = ["band_floor", "band_x1", "band_x2", "band_x3", "band_ceiling"];
  const BANDS = [
    { key: "low", name: "Low", hue: "ink", level: "low_level" },
    { key: "red", name: "Red", hue: "red", level: "red_level" },
    { key: "green", name: "Green", hue: "green", level: "green_level" },
    { key: "violet", name: "Violet", hue: "violet", level: "violet_level" },
  ];
  const LAYER = ["red", "green", "violet"];   // tel.chord.notes[l] and tel.dome[...]: bands 1 … 3
  const MINW = [12, 5, 5, 5];                 // each band's minimum width (T3.8, dsp::BandEdges)
  const EDGE_MAX = [60, 84, 96, 103, 108];    // each edge's parameter maximum (dsp::clampEdges)
  const LOWEST = 21;                          // the lowest floor
  const M0 = 20, M1 = 99;                     // the axis, C1 … C7 inside it
  const TONES = ["the root", "root and fifth", "the chord", "colour tones", "every tone"];
  const SHIFTS = [0, 7, 12, -12, 5];          // a click on Shift steps through these (the mockup's)
  const GLIDE = 3.2, EASE = 0.12, FADE = 0.6; // s: a chord change glides; the lines ease; old ones fade

  const noteName = (n) => UI.KEY_NAMES[((n % 12) + 12) % 12] + (Math.floor(n / 12) - 1);
  const hz = (m) => 440 * Math.pow(2, (m - 69) / 12);
  const P = (id) => WT.get(id);
  const now = () => performance.now() / 1000;
  const smooth = (t) => (t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2);
  const store = {
    get(k, d) { try { const v = window.localStorage.getItem("wt." + k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } },
    set(k, v) { try { window.localStorage.setItem("wt." + k, JSON.stringify(v)); } catch (e) { /* private window: fine */ } },
  };

  /** The edges as the engine plays them (dsp::clampEdges): the floor in its range, then each edge at
   *  least its band's minimum width above the one below, pushed up from the floor. */
  function clampEdges(want) {
    const e = [clamp(Math.round(want[0]), LOWEST, EDGE_MAX[0])];
    for (let i = 1; i < 5; i++) e.push(clamp(Math.round(want[i]), e[i - 1] + MINW[i - 1], Math.max(EDGE_MAX[i], e[i - 1] + MINW[i - 1])));
    return e;
  }
  const edgesNow = () => clampEdges(EDGES.map(P));
  /** Where edge i may go while the others stay: between its neighbours less the bands' minimum
   *  widths, inside its own range and on the axis. */
  function limitsOf(i, E) {
    const m = WT.META[EDGES[i]];
    const lo = Math.max(m.min, i > 0 ? E[i - 1] + MINW[i - 1] : LOWEST);
    const hi = Math.min(m.max, M1, i < 4 ? E[i + 1] - MINW[i] : M1);
    return [lo, Math.max(lo, hi)];
  }
  /** The live level of a band: the value set, moved by whatever modulates it (the knob's ring). */
  // (T4.2: the grid may drive a level up to 2, +6 dB, past the knob's top; unclamped here)
  const liveLevel = (b) => { const m = UI.modLive && UI.modLive(b.level); return m ? Math.max(0, m.now) : P(b.level); };

  function ResonatorDisplay(root) {
    const canvas = root.querySelector("canvas.res-view"), well = canvas.parentNode;
    let sel = clamp(store.get("res.sel", 2) | 0, 0, 3), hotEdge = -1, focusEdge = -1, drag = null;
    let geo = { X: (m) => m, M: (x) => x, top: 10, base: 100, HH: 80, gy: 50, w: 0, h: 0 };

    // ---- the band row: one block per band, the selected one shown
    const blocks = BANDS.map((b) => root.querySelector("#bn-" + b.key));
    for (const [i, b] of BANDS.entries()) {
      if (i === 0) continue;
      const host = blocks[i].querySelector(".bandsel");
      UI.Chip(host, b.key + "_tones", { label: "Tones", fmt: (v) => TONES[Math.round(v)] || "" });
      UI.Chip(host, b.key + "_shift", { label: "Shift", cycle: SHIFTS });
    }
    function select(k) {
      if (k === sel && !blocks[k].hidden) return;
      sel = k;
      store.set("res.sel", k);
      blocks.forEach((n, i) => { n.hidden = i !== k; });
    }
    blocks.forEach((n, i) => { n.hidden = i !== sel; });

    // ---- the handles' proxies: five grips, four levels (spec/06 §5)
    const grips = EDGES.map((id, i) => {
      const p = UI.Proxy(well, id, { cls: "grip", limits: () => limitsOf(i, edgesNow()) });
      p.addEventListener("focus", () => { focusEdge = i; });
      p.addEventListener("blur", () => { if (focusEdge === i) focusEdge = -1; });
      return p;
    });
    const levels = BANDS.map((b, k) => {
      const p = UI.Proxy(well, b.level, { cls: "level" });
      p.setAttribute("aria-controls", "bn-" + b.key);   // the band row it opens
      p.addEventListener("focus", () => select(k));
      p.addEventListener("click", () => select(k));
      return p;
    });
    const placed = new Map();
    const place = (el, x, y) => {
      const s = Math.round(x) + "," + Math.round(y);
      if (placed.get(el) === s) return;
      placed.set(el, s);
      el.style.left = Math.round(x) + "px";
      el.style.top = Math.round(y) + "px";
    };

    // ---- the notes ringing: one mark per distinct note and band, gliding on a chord change
    let marks = [], signature = "", lastT = now();
    const low = { note: -1, from: -1, t0: 0, a: 0 };
    const posOf = (m, t) => m.from + (m.to - m.from) * smooth(clamp((t - m.t0) / GLIDE, 0, 1));
    function setNotes(chord, dome) {
      if (!chord || !chord.notes) return;
      const sig = chord.notes.map((l) => [...new Set(l)].sort((a, b) => a - b).join(",")).join("|");
      const t = now();
      if (sig !== signature) {
        signature = sig;
        const old = marks.filter((m) => !m.dying);
        for (const m of old) { m.from = m.to = posOf(m, t); m.t0 = t; m.dying = true; }
        const fresh = [];
        chord.notes.forEach((layer, l) => {
          for (const n of new Set(layer)) {
            let near = null;
            for (const m of old) if (m.band === l + 1 && Math.abs(m.to - n) < 8 && (!near || Math.abs(m.to - n) < Math.abs(near.to - n))) near = m;
            fresh.push({ band: l + 1, note: n, from: near ? near.to : n, to: n, t0: t, a: near ? near.a : 0, exc: 0, dying: false });
          }
        });
        marks = marks.filter((m) => m.dying && m.a > 0.004).concat(fresh);
      }
      // the excitation is real: voice i is curtain node i (voicesPerNode 1), so a note's is the sum
      // of its voices' emission, normalised by the band's strongest note
      chord.notes.forEach((layer, l) => {
        const em = dome && dome[LAYER[l]], sum = new Map();
        layer.forEach((n, i) => sum.set(n, (sum.get(n) || 0) + (em && i < em.length ? Math.max(0, em[i]) : 0)));
        let max = 0;
        for (const v of sum.values()) max = Math.max(max, v);
        for (const m of marks) if (!m.dying && m.band === l + 1) m.exc = max > 1e-6 ? (sum.get(m.note) || 0) / max : 0;
      });
    }

    // ---- the picture
    function draw(tel) {
      const t = now(), dt = clamp(t - lastT, 0, 0.25);
      lastT = t;
      setNotes(tel.chord, tel.dome);
      const { ctx: c, w, h } = UI.fit(canvas);
      if (w < 20 || h < 20) return;
      const px = 8, top = 10, base = h - 17, HH = (base - top) * 0.86, gy = (top + base) / 2;
      const X = (m) => px + (m - M0) / (M1 - M0) * (w - 2 * px);
      geo = { X, M: (x) => M0 + (x - px) / (w - 2 * px) * (M1 - M0), top, base, HH, gy, w, h };
      const E = edgesNow(), blend = P("band_blend");
      c.clearRect(0, 0, w, h);

      // octaves
      c.font = MONO(10); c.textAlign = "center"; c.textBaseline = "alphabetic";
      for (let o = 1; o <= 7; o++) {
        const x = Math.round(X(12 * (o + 1))) + 0.5;
        c.strokeStyle = C.hairSoft; c.lineWidth = 1;
        c.beginPath(); c.moveTo(x, top - 4); c.lineTo(x, base); c.stroke();
        c.fillStyle = C.faint; c.fillText("C" + o, x, h - 5);
      }
      // the sound's spectrum behind, on the same axis, faint, in ink
      const spec = tel.spectrum;
      if (spec && spec.length > 1) {
        const n = spec.length, at = (m) => {
          const b = n * Math.log(hz(m) / 20) / Math.log(1000), i = Math.floor(b), f = b - i;
          const a = spec[clamp(i, 0, n - 1)], z = spec[clamp(i + 1, 0, n - 1)];
          return clamp(((a + (z - a) * f) + 90) / 80, 0, 1);
        };
        c.beginPath(); c.moveTo(X(M0), base);
        for (let m = M0; m <= M1; m += 0.5) c.lineTo(X(m), base - HH * 0.8 * at(m));
        c.lineTo(X(M1), base); c.closePath(); c.fillStyle = "rgba(201,211,230,0.07)"; c.fill();
      }

      // the bands: as wide as their range, as high as their level, fading into each other over the blend
      BANDS.forEach((b, k) => {
        const lo = E[k], hi = E[k + 1], col = b.hue, on = k === sel, lv = P(b.level), y = base - HH * lv;
        const x0 = X(lo - blend), x1 = X(hi + blend), e0 = blend > 0 ? (2 * blend) / (hi - lo + 2 * blend) : 0;
        const ramp = (a) => {
          const g = c.createLinearGradient(x0, 0, x1, 0);
          g.addColorStop(0, rgba(col, e0 > 0 ? 0 : a)); g.addColorStop(Math.min(0.49, e0), rgba(col, a));
          g.addColorStop(Math.max(0.51, 1 - e0), rgba(col, a)); g.addColorStop(1, rgba(col, e0 > 0 ? 0 : a));
          return g;
        };
        c.fillStyle = ramp(on ? 0.42 : 0.28); c.fillRect(x0, y, x1 - x0, base - y);
        c.strokeStyle = ramp(on ? 1 : 0.7); c.lineWidth = on ? 2 : 1.5;
        c.beginPath(); c.moveTo(x0, y); c.lineTo(x1, y); c.stroke();
        const xm = (X(lo) + X(hi)) / 2;
        c.fillStyle = rgba(col, 1); c.beginPath(); c.roundRect(xm - 8, y - 2.5, 16, 5, 2.5); c.fill();
        // what moves the level (a macro, later the grid) is a white dot beside the handle (rule 3)
        const lvNow = liveLevel(b);
        if (Math.abs(lvNow - lv) > 0.004) {
          const yl = Math.max(top + 3, base - HH * lvNow);   // above the top it waits at the edge
          c.strokeStyle = "rgba(255,255,255,0.4)"; c.lineWidth = 1;
          c.beginPath(); c.moveTo(xm + 15, y); c.lineTo(xm + 15, yl); c.stroke();
          c.fillStyle = "#fff"; c.beginPath(); c.arc(xm + 15, yl, 2.5, 0, 2 * Math.PI); c.fill();
        }
        if (X(hi) - X(lo) > 34) {
          c.font = SANS(11, on ? 600 : 500); c.textAlign = "left"; c.fillStyle = rgba(col, on ? 1 : 0.8);
          c.fillText(b.name, X(lo) + 6, Math.max(top + 9, y - 5));
        }
        place(levels[k], xm, y);
      });
      const fade = c.createLinearGradient(0, top, 0, base);
      fade.addColorStop(0, "rgba(7,10,18,0)"); fade.addColorStop(1, "rgba(7,10,18,0.55)");
      c.fillStyle = fade; c.fillRect(px, top, w - 2 * px, base - top);

      // the notes ringing: a line per note, as high as the band's level × the note's excitation
      const k1 = 1 - Math.exp(-dt / EASE), kf = 1 - Math.exp(-dt / FADE);
      const lvl = BANDS.map((b) => Math.min(1, liveLevel(b)));
      const line = (x, a, col) => {
        const y = base - HH * a;
        c.strokeStyle = rgba(col, 0.95); c.lineWidth = 2;
        c.beginPath(); c.moveTo(x, base); c.lineTo(x, y); c.stroke();
        c.fillStyle = "#fff"; c.beginPath(); c.arc(x, y, 1.8, 0, 2 * Math.PI); c.fill();
      };
      for (const m of marks) {
        m.a += ((m.dying ? 0 : lvl[m.band] * m.exc) - m.a) * (m.dying ? kf : k1);
        if (m.a > 0.004) line(X(posOf(m, t)), m.a, BANDS[m.band].hue);
      }
      marks = marks.filter((m) => !m.dying || m.a > 0.004);
      // the low resonator: one line at its note, as high as its output now
      const ln = tel.low && tel.low.note >= 0 ? tel.low.note : -1;
      if (ln !== low.note) {
        // a new root glides from where the old one is; the first one simply appears
        if (ln >= 0 && low.note >= 0) { low.from = posOfLow(t); low.t0 = t; } else { low.from = ln; low.t0 = t - GLIDE; }
        low.note = ln;
      }
      low.a += ((ln >= 0 ? clamp((tel.low && tel.low.level) || 0, 0, 1) : 0) - low.a) * k1;
      if (low.a > 0.004 && low.note >= 0) line(X(posOfLow(t)), low.a, "ink");
      c.strokeStyle = "rgba(201,211,230,0.25)"; c.lineWidth = 1;
      c.beginPath(); c.moveTo(px, base + 0.5); c.lineTo(w - px, base + 0.5); c.stroke();

      // the grips: floor, three crossovers, ceiling; a hot one says its note and frequency
      E.forEach((m, i) => {
        const x = Math.round(X(clamp(m, M0, M1))), hot = i === hotEdge || i === focusEdge || (drag && drag.edges.includes(i));
        c.fillStyle = hot ? "rgba(238,242,250,0.8)" : "rgba(201,211,230,0.28)"; c.fillRect(x, top - 4, 1, base - top + 4);
        c.fillStyle = hot ? "#fff" : C.ink; c.beginPath(); c.roundRect(x - 3.5, gy - 11, 8, 22, 4); c.fill();
        c.fillStyle = "#0b1019"; c.fillRect(x - 0.5, gy - 5, 2, 10);
        place(grips[i], x + 0.5, gy);
      });
      const said = drag ? drag.edges[0] : hotEdge >= 0 ? hotEdge : focusEdge;
      if (said >= 0) {
        const m = E[said], x = Math.round(X(clamp(m, M0, M1)));
        const txt = noteName(m) + "  " + Math.round(hz(m)) + " Hz";
        c.font = MONO(10.5);
        const tw = c.measureText(txt).width + 12, bx = clamp(x - tw / 2, 2, w - tw - 2);
        c.fillStyle = C.inkHi; c.beginPath(); c.roundRect(bx, 1, tw, 16, 4); c.fill();
        c.fillStyle = C.night; c.textAlign = "left"; c.textBaseline = "middle"; c.fillText(txt, bx + 6, 9.5);
        c.textBaseline = "alphabetic";
      }
    }
    function posOfLow(t) { return low.from + (low.note - low.from) * smooth(clamp((t - low.t0) / GLIDE, 0, 1)); }

    // ---- pointer: grips sideways, bands up and down, a click selects
    const local = (e) => {
      const r = canvas.getBoundingClientRect(), k = r.width / Math.max(1, canvas.clientWidth);
      return { x: (e.clientX - r.left) / k, y: (e.clientY - r.top) / k };
    };
    const edgeAt = (p) => {
      if (p.y < geo.top - 6 || p.y > geo.base + 2) return -1;
      const E = edgesNow();
      let best = -1, bd = 8;
      E.forEach((m, i) => { const d = Math.abs(geo.X(clamp(m, M0, M1)) - p.x); if (d < bd) { bd = d; best = i; } });
      return best;
    };
    const bandAt = (p) => { const m = geo.M(p.x), E = edgesNow(); for (let k = 0; k < 4; k++) if (m >= E[k] && m < E[k + 1]) return k; return -1; };
    function hintAt(p) {
      const i = edgeAt(p), k = i < 0 ? bandAt(p) : -1, E = edgesNow();
      if (i === 0) return "The floor: the lowest note the resonators reach. Shift-drag moves all five.";
      if (i === 4) return "The ceiling: the highest note the resonators reach. Shift-drag moves all five.";
      if (i > 0) return `Crossover between ${BANDS[i - 1].name} and ${BANDS[i].name}. Drag: one band grows, the other shrinks.`;
      if (k >= 0) {
        const tones = k === 0 ? "the root" : TONES[Math.round(P(BANDS[k].key + "_tones"))];
        return `${BANDS[k].name} band, ${noteName(E[k])} to ${noteName(E[k + 1] - 1)}, ringing ${tones}. Click to select, drag for level.`;
      }
      return "The resonators on a pitch axis. Each line is one note ringing.";
    }
    const HINT0 = canvas.dataset.hint;
    canvas.addEventListener("pointermove", (e) => {
      if (drag) return;
      const p = local(e);
      hotEdge = edgeAt(p);
      canvas.style.cursor = hotEdge >= 0 ? "ew-resize" : bandAt(p) >= 0 ? "ns-resize" : "default";
      const text = hintAt(p);
      if (canvas.dataset.hint !== text) { canvas.dataset.hint = text; if (UI.showHint) UI.showHint(canvas); }
    });
    canvas.addEventListener("pointerleave", () => { if (!drag) { hotEdge = -1; canvas.dataset.hint = HINT0; } });

    canvas.addEventListener("pointerdown", (e) => {
      if (e.button !== 0 || drag) return;
      const p = local(e), i = edgeAt(p), k = i < 0 ? bandAt(p) : -1;
      if (i < 0 && k < 0) return;
      e.preventDefault();
      try { canvas.setPointerCapture(e.pointerId); } catch (err) { /* a synthetic pointer (the UI check) */ }
      const E0 = edgesNow(), m0 = geo.M(p.x);
      if (i >= 0) {
        // Shift: all five move together (what Register was), as far as the outermost allow
        const all = e.shiftKey, ids = all ? EDGES : [EDGES[i]];
        let dLo = -Infinity, dHi = Infinity;
        if (all) EDGES.forEach((id, j) => { const m = WT.META[id]; dLo = Math.max(dLo, (j === 0 ? Math.max(m.min, LOWEST) : m.min) - E0[j]); dHi = Math.min(dHi, Math.min(m.max, M1) - E0[j]); });
        ids.forEach(UI.begin);
        const lim = limitsOf(i, E0);
        drag = { edges: all ? [i, 0, 1, 2, 3, 4].filter((v, n, a) => a.indexOf(v) === n) : [i], ids, move(q) {
          if (all) { const d = clamp(Math.round(geo.M(q.x) - m0), dLo, dHi); EDGES.forEach((id, j) => WT.set(id, E0[j] + d)); }
          else WT.set(EDGES[i], clamp(Math.round(geo.M(q.x)), lim[0], lim[1]));
        } };
      } else {
        select(k);
        const id = BANDS[k].level, L0 = P(id), y0 = p.y;
        let began = false;
        drag = { edges: [], ids: [], move(q, ev) {
          if (!began) { began = true; UI.begin(id); this.ids = [id]; }
          WT.set(id, clamp(L0 + (y0 - q.y) / geo.HH * (ev.shiftKey ? 0.2 : 1), 0, 1));
        } };
      }
      const move = (ev) => drag && drag.move(local(ev), ev);
      const up = (ev) => {
        try { canvas.releasePointerCapture(ev.pointerId); } catch (err) { /* synthetic */ }
        canvas.removeEventListener("pointermove", move);
        canvas.removeEventListener("pointerup", up);
        canvas.removeEventListener("pointercancel", up);
        if (drag) drag.ids.forEach(UI.end);
        drag = null;
      };
      canvas.addEventListener("pointermove", move);
      canvas.addEventListener("pointerup", up);
      canvas.addEventListener("pointercancel", up);
    });
    // a double-click sets the default: a grip's note (between its neighbours), a band's level
    canvas.addEventListener("dblclick", (e) => {
      const p = local(e), i = edgeAt(p), k = i < 0 ? bandAt(p) : -1;
      if (i >= 0) UI.commit(EDGES[i], clamp(UI.defaultValue(EDGES[i]), ...limitsOf(i, edgesNow())));
      else if (k >= 0) UI.commit(BANDS[k].level, UI.defaultValue(BANDS[k].level));
    });

    return {
      draw,
      select,
      get selected() { return sel; },
      /** For the UI check and probes: the axis now, and the lines drawn ([band, note, height]). */
      geom: () => geo,
      lines: () => marks.filter((m) => !m.dying).map((m) => [m.band, m.note, Math.round(m.a * 1000) / 1000]).concat(low.note >= 0 ? [[0, low.note, Math.round(low.a * 1000) / 1000]] : []),
    };
  }

  Object.assign(UI, { ResonatorDisplay });
})();
