/* The instrument layer's live pictures (spec/06, T6.1). Each view redraws only when what it shows
 * has changed (or, for the moving ones, only while its tab is visible), and every change glides.
 * T6.7 (spec/09 §2 rule 2): red, green and violet only where a picture shows the bands (layers, the
 * keyboard strip) or the sky's own light (the day strip's keogram); the effect pictures are drawn in ink.
 * Axis numbers are at least 10 px, words in pictures 11 px. */
(function () {
  "use strict";
  const UI = window.UI, C = UI.C, rgba = UI.rgba, MONO = UI.MONO;
  const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
  const LAYERS = ["red", "green", "violet"];
  const now = () => performance.now() / 1000;

  // ================================================================ the day strip: keogram over the magnetometer (T6.10)
  /** The keogram's constants, tuned by eye in the mockup (spec/mockups/ui-v2.html `buildKeo`). The
   *  picture is the dome's oval shader (dome.js `FS_OVAL`: the diffuse band, the red band, slant,
   *  night, the day shift) along the station's north–south meridian, plus the arc the curtain draws
   *  (green 115 km, red 250 km, violet 100 km). If FS_OVAL changes, change these with it; the
   *  owner's eye tunes them here. */
  const KEO = {
    colS: 300,                       // one column per 5 minutes of the day
    rows: 64,                        // zenith angle −90° (north) … +90° (south)
    earthKm: 6371.2,
    heightKm: { green: 115, red: 250, violet: 100 },
    sigmaDeg: { green: 0.55, red: 1.0, violet: 0.55, perRow: 0.6 },   // the arc's width: max(σ, 0.6 × the row's step)
    diffuse: { base: 0.008, activity: 0.035, unloading: 0.04, gain: 5 },   // FS_OVAL uDiffuse, × 5 for the strip
    arcI: { base: 0.08, activity: 0.95, unloading: 0.8, green: 0.6 },
    red: { storm: 0.055, quiet: 0.006, gain: 5, arc: 0.22, stormArc: 0.6 },  // FS_OVAL uRed, uQuietRed
    violet: { base: 0.1, unloading: 0.9 },
    unloadingMax: 1.5,
    horizonDeg: 7,                   // fade the last 7° of elevation
    tone: 1.7,                       // pixel = ground + fade · (1 − e^(−1.7 x))
    ground: [0.02, 0.027, 0.051],    // the night sky, #05070d
    // how each line's light lands in r, g, b (the mockup's mix, not the pure hues: the strip glows)
    mix: { green: [0.36, 0.95, 0.63], red: [1.0, 0.42, 0.42], violet: [0.56, 0.48, 1.0] },
    traceStepS: 300,                 // the magnetometer is drawn on the same 5-minute grid
  };
  const smooth = (a, b, x) => { const t = clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); };
  const PHASES = ["quiet", "growth", "onset", "recovery"];
  const KEO_CACHE = new Map();   // day id → bitmap; a revisited day (T6.9) is not rebuilt

  /** A per-minute field of the day event at fractional minute index f (entry k is the state at
   *  t0 + step·(k + 1)), linearly interpolated; `wrap` for angles like MLT (period 24). */
  function sample(arr, f, wrap) {
    const n = arr.length, i = clamp(f, 0, n - 1), i0 = Math.floor(i), i1 = Math.min(n - 1, i0 + 1), a = arr[i0];
    let b = arr[i1];
    if (wrap && Math.abs(b - a) > wrap / 2) b += a > b ? wrap : -wrap;
    return a + (b - a) * (i - i0);
  }

  /** The keogram of a loaded day: KEO.colS columns × KEO.rows rows, built once and cached. */
  function buildKeogram(day) {
    const sig = [day.t0, day.phase.length, day.stationMlat, day.dayShift, day.arcPosition].join();
    const hit = KEO_CACHE.get(day.day);
    if (hit && hit.sig === sig) return hit;
    const t0 = performance.now();
    const span = day.phase.length * day.step, n = Math.max(1, Math.round(span / KEO.colS)), H = KEO.rows;
    const Re = KEO.earthKm, R2D = 180 / Math.PI, st = day.stationMlat || 65.66;
    const cv = document.createElement("canvas");
    cv.width = n; cv.height = H;
    const cx = cv.getContext("2d"), img = cx.createImageData(n, H), px = img.data;
    // rows: where each height's light at that zenith angle sits in magnetic latitude
    const rows = [];
    for (let j = 0; j < H; j++) {
      const zeta = -90 + 180 * (j + 0.5) / H, e = (90 - Math.abs(zeta)) / R2D, sgn = zeta < 0 ? 1 : -1;
      const mlat = (h) => {
        const s = -Re * Math.sin(e) + Math.sqrt(Re * Re * Math.sin(e) ** 2 + 2 * Re * h + h * h);
        return st + sgn * Math.atan2(s * Math.cos(e), Re + s * Math.sin(e)) * R2D;
      };
      rows.push({ g: mlat(KEO.heightKm.green), r: mlat(KEO.heightKm.red), v: mlat(KEO.heightKm.violet),
        slant: 1 + 2 * Math.pow(1 - Math.sin(e), 3), fade: smooth(0, KEO.horizonDeg, 90 - Math.abs(zeta)) });
    }
    const S = KEO.sigmaDeg;
    rows.forEach((q, j) => {
      const nb = rows[Math.min(H - 1, j + 1)], pv = rows[Math.max(0, j - 1)];
      q.sg = Math.max(S.green, S.perRow * Math.abs(nb.g - pv.g));
      q.sv = Math.max(S.violet, S.perRow * Math.abs(nb.v - pv.v));
      q.sr = Math.max(S.red, S.perRow * Math.abs(nb.r - pv.r));
    });
    const tone = (x) => 1 - Math.exp(-KEO.tone * x), [mg, mr, mv] = [KEO.mix.green, KEO.mix.red, KEO.mix.violet];
    const D = KEO.diffuse, A = KEO.arcI, RD = KEO.red, V = KEO.violet, half = (day.dayShift || 0) / 2, pos = day.arcPosition ?? 0.5;
    for (let i = 0; i < n; i++) {
      const f = (KEO.colS * (i + 0.5)) / day.step - 1;   // the column's middle, in minute entries
      const mlt = sample(day.mlt, f, 24), c = Math.cos((2 * Math.PI * mlt) / 24);
      const shift = half * (1 - c), night = 0.25 + 0.75 * (0.5 + 0.5 * c);
      const eq = sample(day.eqLat, f) + shift, pole = sample(day.poleLat, f) + shift, arc = eq + pos * (pole - eq);
      const act = sample(day.activity, f), gain = sample(day.gain, f), u = Math.min(KEO.unloadingMax, sample(day.unloading, f));
      const red = sample(day.stormRed, f);
      const dif = D.base + D.activity * act * gain + D.unloading * u, Ia = A.base + A.activity * act + A.unloading * u;
      for (let j = 0; j < H; j++) {
        const q = rows[j];
        const band = smooth(eq - 1.5, eq + 1, q.g) * Math.exp(-Math.max(0, q.g - eq - 1) / 3) * (1 - smooth(pole - 1.5, pole + 2, q.g));
        const bandR = smooth(eq - 6, eq, q.r) * (1 - smooth(eq + 3, eq + 8, q.r));
        const ga = Math.exp(-(((q.g - arc) / q.sg) ** 2)), ra = Math.exp(-(((q.r - arc) / q.sr) ** 2)), va = Math.exp(-(((q.v - arc) / q.sv) ** 2));
        const G = D.gain * dif * band * night * q.slant + A.green * Ia * ga;
        const R = RD.gain * (RD.storm * red + RD.quiet) * bandR * night * q.slant + (RD.arc * Ia + RD.stormArc * red * night) * ra;
        const Vi = Ia * (V.base + V.unloading * u) * va;
        const o = (j * n + i) * 4;
        for (let ch = 0; ch < 3; ch++) px[o + ch] = 255 * clamp(KEO.ground[ch] + q.fade * tone(mg[ch] * G + mr[ch] * R + mv[ch] * Vi), 0, 1);
        px[o + 3] = 255;
      }
    }
    cx.putImageData(img, 0, 0);
    // the magnetometer on the same grid, and its range (zero always inside)
    const trace = [];
    let lo = 0, hi = 0;
    for (let t = 0; t <= span; t += KEO.traceStepS) {
      const v = sample(day.dh, t / day.step - 1);
      trace.push(v); lo = Math.min(lo, v); hi = Math.max(hi, v);
    }
    const k = { sig, cv, cols: n, rows: H, trace, lo, hi: hi > lo ? hi : lo + 1, ms: performance.now() - t0 };
    KEO_CACHE.set(day.day, k);
    return k;
  }

  /** The day strip (spec/09 §2, T6.10; mockup `drawStrip`): the keogram over the magnetometer, hour
   *  marks, onsets as carets on the top edge, the part still to come dimmed, the playhead. It sizes
   *  itself from its canvas box: rows fixed at the bottom (field 27 px, hours), the keogram takes the
   *  rest. The static layer is drawn once per day and size; a frame blits it and adds the playhead. */
  function DayStripView(canvas) {
    const base = document.createElement("canvas");
    let day = null, keo = null, dirty = true, lastW = 0, lastH = 0, lastK = 0, lastSig = "", hoverX = null;
    const span = () => day.phase.length * day.step;
    const geom = (w, h) => ({ x0: 62, x1: w - 12, ky0: 7, ky1: h - 36, ty: h - 9, dy0: h - 31, dy1: h - 4 });
    const xOfUtc = (G, utc) => G.x0 + clamp((utc - day.t0) / span(), 0, 1) * (G.x1 - G.x0);

    function setDay(d) {
      day = d;
      keo = d && d.eqLat ? buildKeogram(d) : null;
      dirty = true;
    }

    function drawBase(w, h, k) {
      base.width = Math.round(w * k); base.height = Math.round(h * k);
      const c = base.getContext("2d"), G = geom(w, h);
      c.setTransform(k, 0, 0, k, 0, 0);
      c.clearRect(0, 0, w, h);
      if (keo) {
        c.imageSmoothingEnabled = true; c.imageSmoothingQuality = "high";
        c.drawImage(keo.cv, G.x0, G.ky0, G.x1 - G.x0, G.ky1 - G.ky0);
      }
      // the rows: north, overhead, south, and the field under them
      c.font = UI.SANS(11); c.textBaseline = "middle"; c.textAlign = "right"; c.fillStyle = C.muted;
      const mid = (G.ky0 + G.ky1) / 2;
      if (G.ky1 - G.ky0 >= 30) { c.fillText("north", G.x0 - 8, G.ky0 + 7); c.fillText("south", G.x0 - 8, G.ky1 - 7); }
      if (G.ky1 - G.ky0 >= 40 || G.ky1 - G.ky0 < 30) c.fillText(G.ky1 - G.ky0 >= 40 ? "overhead" : "sky", G.x0 - 8, mid);
      c.fillText("field", G.x0 - 8, (G.dy0 + G.dy1) / 2);
      c.strokeStyle = "rgba(201,211,230,0.16)"; c.lineWidth = 1; c.setLineDash([2, 4]);
      c.beginPath(); c.moveTo(G.x0, Math.round(mid) + 0.5); c.lineTo(G.x1, Math.round(mid) + 0.5); c.stroke(); c.setLineDash([]);
      // hour marks every 6 h, the date at midnight
      c.textAlign = "left"; c.font = MONO(10);
      for (let u = Math.ceil(day.t0 / 21600) * 21600; u < day.t0 + span(); u += 21600) {
        const x = Math.round(xOfUtc(G, u)), midnight = u % 86400 === 0, d = new Date(u * 1000);
        c.fillStyle = midnight ? "rgba(201,211,230,0.5)" : "rgba(201,211,230,0.2)"; c.fillRect(x, G.ky0, 1, G.dy1 - G.ky0);
        c.fillStyle = midnight ? C.ink : C.muted;
        c.fillText(midnight ? d.toLocaleDateString("en-GB", { day: "numeric", month: "short", timeZone: "UTC" }) : d.toISOString().slice(11, 16), x + 5, G.ty);
      }
      // the magnetometer: ΔH in ink over its zero line
      if (keo) {
        const zy = G.dy0 + (keo.hi / (keo.hi - keo.lo)) * (G.dy1 - G.dy0), Y = (v) => zy - (v / (keo.hi - keo.lo)) * (G.dy1 - G.dy0);
        c.strokeStyle = "rgba(201,211,230,0.16)"; c.beginPath(); c.moveTo(G.x0, Math.round(zy) + 0.5); c.lineTo(G.x1, Math.round(zy) + 0.5); c.stroke();
        c.strokeStyle = C.arc; c.lineWidth = 1.2; c.lineJoin = "round"; c.beginPath();
        keo.trace.forEach((v, i) => { const x = xOfUtc(G, day.t0 + i * KEO.traceStepS), y = Y(v); if (i) c.lineTo(x, y); else c.moveTo(x, y); });
        c.stroke();
      }
      // onsets: carets on the top edge, filled (hollow for pseudo-breakups), sized by strength
      for (const o of day.onsets) {
        const x = xOfUtc(G, o.utc), s = 3 + Math.min(3, o.strength * 1.5);
        c.beginPath(); c.moveTo(x, G.ky0 + s + 1); c.lineTo(x - s * 0.8, G.ky0 - 0.5); c.lineTo(x + s * 0.8, G.ky0 - 0.5); c.closePath();
        if (o.kind === "pseudo") { c.strokeStyle = "rgba(238,242,250,0.75)"; c.lineWidth = 1; c.stroke(); } else { c.fillStyle = C.inkHi; c.fill(); }
      }
      dirty = false;
    }

    /** The moment under x (CSS px in the canvas); null over the row words unless `clamped` (a drag
     *  that leaves the strip holds at its ends). */
    function utcAt(x, clamped) {
      if (!day) return null;
      const G = geom(canvas.clientWidth, canvas.clientHeight);
      if (!clamped && (x < G.x0 - 6 || x > G.x1 + 6)) return null;
      return day.t0 + clamp((x - G.x0) / (G.x1 - G.x0), 0, 1) * span();
    }
    const phaseAt = (utc) => (day ? PHASES[day.phase[clamp(Math.round((utc - day.t0) / day.step - 1), 0, day.phase.length - 1)]] || "" : "");
    /** `10 May 17:45 UT, onset, Kp 7.7, field −1063 nT.` */
    function moment(utc) {
      if (!day) return "";
      const f = (utc - day.t0) / day.step - 1, d = new Date(utc * 1000), dh = Math.round(sample(day.dh, f));
      const kp = day.kp ? `, Kp ${sample(day.kp, f).toFixed(1)}` : "";
      return `${d.toLocaleDateString("en-GB", { day: "numeric", month: "short", timeZone: "UTC" })} ${d.toISOString().slice(11, 16)} UT, `
        + `${phaseAt(utc)}${kp}, field ${dh < 0 ? "−" : ""}${Math.abs(dh)} nT.`;
    }
    function setHover(x) { hoverX = x; }

    function draw(nowUtc) {
      if (!day) return;
      const w = canvas.clientWidth, h = canvas.clientHeight, k = Math.min(3, UI.dpr * UI.scale);
      if (dirty || w !== lastW || h !== lastH || k !== lastK) { drawBase(w, h, k); lastW = w; lastH = h; lastK = k; lastSig = ""; }
      const G = geom(w, h), xn = xOfUtc(G, nowUtc), utcH = hoverX === null ? null : utcAt(hoverX);
      const xh = utcH === null ? null : Math.round(xOfUtc(G, utcH));
      const sig = (xn * 4 | 0) + "|" + xh;
      if (sig === lastSig) return;   // the playhead moves a quarter pixel at a time
      lastSig = sig;
      const { ctx } = UI.fit(canvas);
      ctx.clearRect(0, 0, w, h);
      ctx.drawImage(base, 0, 0, w, h);
      ctx.fillStyle = "rgba(5,7,13,0.5)"; ctx.fillRect(xn, G.ky0, G.x1 - xn, G.dy1 - G.ky0);   // still to come
      ctx.fillStyle = "#fff"; ctx.fillRect(Math.round(xn) - 0.5, G.ky0 - 3, 1.5, G.dy1 - G.ky0 + 5);
      ctx.beginPath(); ctx.moveTo(xn - 5, G.ky0 - 4); ctx.lineTo(xn + 5, G.ky0 - 4); ctx.lineTo(xn, G.ky0 + 3); ctx.closePath(); ctx.fill();
      if (xh !== null) { ctx.fillStyle = "rgba(255,255,255,0.55)"; ctx.fillRect(xh, G.ky0, 1, G.dy1 - G.ky0); }
    }

    return { setDay, draw, utcAt, phaseAt, moment, setHover, get keo() { return keo; }, get day() { return day; } };
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

      // UI.eqDb's parameters (T6.12's shape): as set, and as they are now
      const pOf = (v) => ({ bands: [{ hz: v("eq1_freq"), gain: v("eq1_gain") }, { hz: v("eq2_freq"), gain: v("eq2_gain"), q: v("eq2_q") },
        { hz: v("eq3_freq"), gain: v("eq3_gain"), q: v("eq3_q") }], cutHz: v("eq4_freq") });
      const setP = pOf(P), liveP = pOf((id) => (id in live ? live[id] : P(id)));

      // behind: the sound. T6.12's pre-EQ spectrum shaped by the curve now (the output's, which the EQ
      // has already shaped, where that tap is missing)
      const spec = tel.spectrumIn || tel.spectrum;
      if (spec && spec.length) {
        const n = spec.length, top = g.floor - (g.floor - 12) * 0.82;
        c.beginPath(); c.moveTo(0, g.floor);
        for (let i = 0; i < n; i++) {
          const x = (i + 0.5) / n * w, db = spec[i] + (tel.spectrumIn ? UI.eqDb(g.F(x), liveP, fs) : 0);
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
      path(liveP); c.lineTo(w, g.mid); c.lineTo(0, g.mid); c.closePath(); c.fillStyle = "rgba(201,211,230,0.13)"; c.fill();
      path(setP); c.strokeStyle = "rgba(201,211,230,0.32)"; c.lineWidth = 1; c.stroke();
      path(liveP); c.strokeStyle = C.inkHi; c.lineWidth = 1.8; c.stroke();

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

  Object.assign(UI, { DayStripView, KEO, LayersView, EqView, KeyboardView, PhaserView, ChorusView, DelayView, ReverbView, OutputView, HeaderMeter, dbOf, phaserResponse, phaserBreak });
})();
