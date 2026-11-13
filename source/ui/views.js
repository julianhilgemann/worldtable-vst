/* The instrument layer's live pictures (spec/06, T6.1). Each view redraws only when what it shows
 * has changed (or, for the moving ones, only while its tab is visible), and every change glides.
 * T6.7 (spec/09 §2 rule 2): red, green and violet only where a picture shows the bands (the resonator
 * display, resonators.js; the keyboard strip) or the sky's own light (the day strip's keogram); the
 * effect pictures are drawn in ink.
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

  Object.assign(UI, { DayStripView, KEO, DriveView, FilterView, KeyboardView, PhaserView, ChorusView, DelayView, ReverbView, OutputView, HeaderMeter, dbOf });
})();
