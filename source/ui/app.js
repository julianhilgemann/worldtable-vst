/* Wires the page (spec/06, T6.1): mounts the controls, scales the stage, feeds the dome and the
 * instrument layer from the editor's "telemetry" (30 Hz) and "day" events, runs one render loop
 * capped at 30 fps (15 with reduced motion) and paused while hidden. */
(function () {
  "use strict";
  const WT = window.WT, UI = window.UI, SKY = window.SKY;
  const BASE_W = 1350, BASE_H = 900;
  const $ = (id) => document.getElementById(id);
  const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
  const reduced = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  const S = { tel: {}, day: null, frames: 0, domeMode: "" };
  window.__wtState = S;   // for debugging and the UI integration check

  /** Rewrites an element's markup only when it changed (a rewrite relayouts and repaints; T6.7). */
  const htmlShown = {};
  const setHtml = (id, html) => { if (htmlShown[id] !== html) { htmlShown[id] = html; $(id).innerHTML = html; } };

  const store = {
    get(k, d) { try { const v = window.localStorage.getItem("wt." + k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } },
    set(k, v) { try { window.localStorage.setItem("wt." + k, JSON.stringify(v)); } catch (e) { /* private window: fine */ } },
  };

  // ---------------------------------------------------------------- controls
  const knobs = (UI.knobs = {});
  for (const n of document.querySelectorAll("[data-knob]")) {
    // data-view: a second view of a parameter bound elsewhere (T6.11's band row Level)
    const k = UI.Knob(n, n.dataset.knob, n.dataset.label || "", { size: n.dataset.size, color: n.dataset.color, view: n.hasAttribute("data-view"), hint: n.dataset.hint });
    if (k) knobs[n.dataset.knob] = k;
  }
  for (const n of document.querySelectorAll("[data-seg]")) UI.Seg(n, n.dataset.seg, n.dataset.labels ? n.dataset.labels.split(",") : null);
  for (const n of document.querySelectorAll("[data-keys]")) UI.Keys(n, n.dataset.keys);
  for (const n of document.querySelectorAll("[data-toggle]")) UI.Toggle(n, n.dataset.toggle, n.dataset.label || "");
  // T6.12: values set inside a picture have a focusable proxy on their handle; the phaser's Stages chip
  const handles = (UI.handles = {});
  for (const n of document.querySelectorAll("[data-handle]")) {
    const h = UI.Handle(n, n.dataset.handle, n.dataset.label || "");
    if (h) handles[n.dataset.handle] = h;
  }
  for (const n of document.querySelectorAll("[data-cycle]"))
    UI.Cycle(n, n.dataset.cycle, n.dataset.values.split(",").map(Number), (v) => `<b>${v}</b> stages`);

  // ---------------------------------------------------------------- the hint line (spec/06 §2 rule 5)
  // One sentence about what the thing under the pointer (or, from the keyboard, in focus) does.
  const HINT0 = "Point at anything to read what it does.";
  const showHint = (node) => {
    const n = node && node.closest ? node.closest("[data-hint]") : null;
    const text = n ? n.dataset.hint : HINT0;
    if ($("hint").textContent !== text) $("hint").textContent = text;
  };
  $("stage").addEventListener("pointerover", (e) => showHint(e.target));
  $("stage").addEventListener("pointerleave", () => showHint(document.activeElement));
  document.addEventListener("focusin", (e) => showHint(e.target));
  showHint(null);
  UI.showHint = showHint;   // for pictures whose hint follows the pointer (T6.11)

  // ---------------------------------------------------------------- modulation: rings and sources (T4.2)
  // Every routing is the grid's (UI.mod, mod.js). A knob with at least one route carries the ring
  // (spec/06 §2 rule 3): the arc is the range its sources can push it, the dot the value the engine
  // uses now (telemetry mod.val, lo, hi); the number under it stays the value set. Pointing at a
  // source (a macro, the LFO panel, the envelope panel) lights the knobs it moves, and its hint says
  // what it moves.
  const MOD = UI.mod;
  let routed = new Set(), targetAt = {};
  /** What a source moves, in words ("nothing yet"), at most `max` names and "N more". */
  function movesText(key, max = 4) {
    const names = MOD.movedBy(key).map((r) => MOD.destName(r.destination.key));
    if (!names.length) return "nothing yet";
    return names.length <= max ? names.join(", ") : names.slice(0, max - 1).join(", ") + " and " + (names.length - max + 1) + " more";
  }
  /** The value a parameter has now with what moves it, { lo, hi, now } in its own units, or null. */
  UI.modLive = (id) => {
    const t = MOD.tel, i = targetAt[id];
    if (!routed.has(id) || !t || !t.val || i === undefined || !isFinite(t.val[i])) return null;
    return { lo: t.lo[i], hi: t.hi[i], now: t.val[i] };
  };
  function updateRings() {
    for (const id of Object.keys(knobs)) {
      const m = UI.modLive(id), k = knobs[id];
      if (m) k.setMod(m.lo, m.hi, m.now); else if (k.mod) k.clearMod();
    }
  }
  UI.updateRings = updateRings;
  /** A source (`key()`: the source shown now) lights the knobs it moves while it is pointed at or
   *  focused; its hint is `base` and what it moves (≤ 90 characters with `max` names). */
  const hovered = [];
  function sourceHover(node, key, base, max) {
    const say = () => { node.dataset.hint = `${base} Moves: ${movesText(key(), max)}.`; };
    const light = (on) => () => {
      for (const r of MOD.movedBy(key())) for (const p of r.destination.params || []) if (knobs[p]) knobs[p].setLit(on);
    };
    node.addEventListener("pointerenter", light(true));
    node.addEventListener("pointerleave", () => { if (!node.contains(document.activeElement)) light(false)(); });
    node.addEventListener("focusin", light(true));
    node.addEventListener("focusout", (e) => { if (!node.contains(e.relatedTarget)) light(false)(); });
    hovered.push(say);
    say();
    return { say, light };
  }
  UI.sourceHover = sourceHover;
  UI.movesText = movesText;
  for (const id of ["macro_storm", "macro_breath", "macro_glow", "macro_space", "macro_drift"])
    if (knobs[id]) sourceHover(knobs[id].root, () => id, UI.hintOf(id), 3);
  let routesSig = null;
  MOD.on(() => {
    // called on every telemetry frame too: only a change of the grid recomputes what is routed
    const info = MOD.info, sig = info.depths.map((r) => r.join()).join("|") + "|" + (info.targets || []).join();
    if (sig === routesSig) return;
    routesSig = sig;
    routed = new Set();
    targetAt = {};
    (info.targets || []).forEach((p, i) => { if (p) targetAt[p] = i; });
    info.destinations.forEach((d, j) => { if (info.depths.some((row) => row[j])) for (const p of d.params || []) routed.add(p); });
    for (const say of hovered) say();
    updateRings();
  });

  // ---------------------------------------------------------------- popovers: More, Details, View
  // One page (T6.5). Parameters without a visible knob live in their module's More popover (spec/06
  // §5, T6.8): every `button.more-btn` opens the panel its aria-controls names. Details (observer,
  // journey, the sky's numbers, data) opens from the bottom bar, View over the dome.
  const hash = new URLSearchParams(location.hash.slice(1));
  // #shot: no CSS transitions (headless review screenshots run on virtual time, which they ignore)
  if (hash.has("shot")) document.documentElement.classList.add("shot");
  const pops = [];
  /** A button that opens a panel; one is open at a time; Escape or a click outside closes it, and
   *  focus that was inside the panel goes back to its button. */
  function popover(btn, panel, onOpen) {
    const p = { open: false, btn, panel, set(open) {
      if (open) for (const q of pops) if (q !== p && q.open) q.set(false);
      if (!open && panel.contains(document.activeElement)) btn.focus({ preventScroll: true });
      p.open = open;
      panel.hidden = !open;
      btn.setAttribute("aria-expanded", String(open));
      if (open && onOpen) onOpen();
    } };
    btn.addEventListener("click", (e) => { e.stopPropagation(); p.set(!p.open); });
    panel.addEventListener("click", (e) => e.stopPropagation());
    pops.push(p);
    return p;
  }
  document.addEventListener("click", () => { for (const p of pops) if (p.open) p.set(false); });
  document.addEventListener("keydown", (e) => { if (e.key === "Escape") for (const p of pops) if (p.open) p.set(false); });
  const CHEVRON = '<svg class="chev" width="8" height="5" viewBox="0 0 8 5" fill="none" aria-hidden="true"><path d="M1 1l3 3 3-3" stroke="currentColor" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round"/></svg>';
  const more = {};
  for (const btn of document.querySelectorAll(".more-btn[aria-controls]")) {
    btn.insertAdjacentHTML("beforeend", CHEVRON);
    more[btn.closest(".module, #macros").id] = popover(btn, $(btn.getAttribute("aria-controls")));
  }
  UI.more = more;   // module id → its More popover ({ open, set(open) })
  const details = popover($("details-btn"), $("details"), () => { if (S.tel.sky) showJourney(S.tel.sky); });
  const viewPop = popover($("view-btn"), $("view-panel"));
  // for review screenshots: #more=resonators opens that module's popover, #details the details
  if (hash.has("more") && more[hash.get("more")]) more[hash.get("more")].set(true);
  if (hash.has("bands")) more.resonators.set(true);
  if (hash.has("details")) details.set(true);
  // mock only, for review screenshots: #set=play:1,eq4_freq:800
  if (WT.isMock && hash.has("set"))
    for (const kv of hash.get("set").split(",")) {
      const [k, v] = kv.split(":");
      if (WT.META[k]) WT.set(k, Number(v));
    }
  if (hash.has("view")) viewPop.set(true);

  // ---------------------------------------------------------------- scaling
  // The page fills the window (T6.13: the editor's window is Small, Medium or Large). In the browser
  // mock the chosen size caps the zoom instead (`UI.sizeFactor`, set by menu.js).
  function rescale() {
    UI.scale = Math.min(window.innerWidth / BASE_W, window.innerHeight / BASE_H, UI.sizeFactor || Infinity);
    UI.dpr = window.devicePixelRatio || 1;
    const x = (window.innerWidth - BASE_W * UI.scale) / 2, y = (window.innerHeight - BASE_H * UI.scale) / 2;
    $("stage").style.transform = `translate(${x}px, ${y}px) scale(${UI.scale})`;
  }
  window.addEventListener("resize", rescale);
  UI.rescale = rescale;
  rescale();

  // ---------------------------------------------------------------- views
  const strip = UI.DayStripView($("cv-strip"));
  const res = (UI.res = UI.ResonatorDisplay($("resonators")));   // T6.11
  const keys = UI.KeyboardView($("cv-keys"));
  const eq = (UI.eq = UI.EqView($("cv-eq"), $("eq-well"), $("eq-say")));   // T6.14; UI.eq for the UI check
  const fx = {
    // T6.12: the pictures are the effects' controls; the phaser's outline is shaped by the tone stage
    phaser: UI.PhaserView($("cv-phaser"), { handles, tone: (f, tel) => UI.eqDb(f, UI.eqParams(tel.eqCutHz), tel.sampleRate || 48000) }),
    chorus: UI.ChorusView($("cv-chorus")),
    delay: UI.DelayView($("cv-delay"), { handles }), reverb: UI.ReverbView($("cv-reverb"), { handles }), output: UI.OutputView($("cv-output"), { handles }),
  };
  const hmeter = UI.HeaderMeter($("cv-hmeter"));

  // ---------------------------------------------------------------- the day strip (T6.10)
  // The Journey's timeline (T6.6): hover reads the moment into the hint line, a click jumps there, a
  // drag scrubs (at most 5 seeks a second, and one on release), and from the keyboard it is a slider
  // (arrows ±10 min, Shift ±1 h, PageUp/PageDown ±6 h, Home/End). Every seek takes T6.6's path: the
  // sound follows without a jolt.
  const stripBox = $("strip"), stripCv = $("cv-strip");
  const scrub = { utc: null, holdUntil: 0, dragging: false, lastSeekAt: -1e9, lastSeekUtc: null, timer: 0 };
  const playUtc = () => (S.tel.sky ? S.tel.sky.utc : S.day ? S.day.t0 : 0);
  /** Where the playhead is drawn: the scrub's target until the sky has caught up with it. */
  const shownUtc = () => (scrub.utc !== null && (scrub.dragging || performance.now() < scrub.holdUntil) ? scrub.utc : playUtc());
  const SEEK_MS = 200;
  function seekNow(utc) {
    clearTimeout(scrub.timer);
    scrub.timer = 0;
    scrub.lastSeekAt = performance.now();
    scrub.lastSeekUtc = utc;
    scrub.holdUntil = scrub.lastSeekAt + 500;
    WT.native("seek", utc);
  }
  /** Moves the playhead to `utc` at once; seeks there now, or as soon as 200 ms have passed since the
   *  last seek (the latest target wins). `final` seeks at once unless that moment was the last seek. */
  function scrubTo(utc, final) {
    scrub.utc = utc;
    scrub.holdUntil = Math.max(scrub.holdUntil, performance.now() + 500);
    if (final) { if (utc !== scrub.lastSeekUtc || scrub.timer) seekNow(utc); return; }
    const wait = SEEK_MS - (performance.now() - scrub.lastSeekAt);
    if (wait <= 0) seekNow(utc);
    else if (!scrub.timer) scrub.timer = setTimeout(() => seekNow(scrub.utc), wait);
  }
  let ariaShown = 0, ariaMinute = null;
  /** The slider's value for assistive tech: the moment's sentence (at most 4× a second unless forced). */
  function stripAria(utc, force) {
    const t = performance.now(), m = Math.floor(utc / 60);
    if ((!force && t - ariaShown < 250) || m === ariaMinute) return;
    ariaShown = t;
    ariaMinute = m;
    stripBox.setAttribute("aria-valuenow", String(Math.round(utc)));
    stripBox.setAttribute("aria-valuetext", strip.moment(utc));
  }
  {
    const xOf = (e) => (e.clientX - stripCv.getBoundingClientRect().left) / UI.scale;
    let drag = false, hoverMinute;
    const hover = (e) => {
      const x = xOf(e), utc = strip.utcAt(x);
      strip.setHover(utc === null ? null : x);
      stripCv.style.cursor = utc === null && !drag ? "default" : "pointer";
      const m = utc === null ? null : Math.floor(utc / 60);
      if (m !== hoverMinute) {
        hoverMinute = m;
        $("hint").textContent = utc === null ? stripBox.dataset.hint : `${strip.moment(utc)} Click to jump there.`;
      }
      return x;
    };
    stripCv.addEventListener("pointermove", (e) => {
      const x = hover(e);
      if (drag) scrubTo(strip.utcAt(x, true), false);
    });
    stripCv.addEventListener("pointerleave", () => { strip.setHover(null); hoverMinute = undefined; });
    stripCv.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      const utc = strip.utcAt(xOf(e));
      if (utc === null) return;
      e.preventDefault();
      stripBox.focus({ preventScroll: true });
      try { stripCv.setPointerCapture(e.pointerId); } catch (err) { /* a synthetic pointer (the UI check) */ }
      drag = scrub.dragging = true;
      scrubTo(utc, false);
    });
    const release = (e) => {
      if (!drag) return;
      drag = scrub.dragging = false;
      const utc = strip.utcAt(xOf(e), true);
      scrubTo(utc, true);
      stripAria(utc, true);
    };
    stripCv.addEventListener("pointerup", release);
    stripCv.addEventListener("pointercancel", release);
    stripBox.addEventListener("keydown", (e) => {
      const day = S.day;
      if (!day || e.target !== stripBox) return;
      const k = e.key, t0 = day.t0, last = t0 + day.phase.length * day.step - day.step;
      const from = shownUtc(), step = e.shiftKey ? 3600 : 600;
      const to = k === "ArrowRight" || k === "ArrowUp" ? from + step : k === "ArrowLeft" || k === "ArrowDown" ? from - step
        : k === "PageUp" ? from + 6 * 3600 : k === "PageDown" ? from - 6 * 3600 : k === "Home" ? t0 : k === "End" ? last : null;
      if (to === null) return;
      e.preventDefault();
      const utc = clamp(to, t0, last);
      scrubTo(utc, false);
      stripAria(utc, true);
      $("hint").textContent = strip.moment(utc);
    });
  }
  UI.dayStrip = { view: strip, shownUtc, scrubTo, scrub };   // for the UI check

  // ---------------------------------------------------------------- the dome
  const dome = new SKY.DomeRenderer($("dome"), $("dome-ov"), new SKY.Observer(), 1);
  S.domeMode = dome.mode;
  const VIEW = { stars: store.get("view.stars", true), dayNight: store.get("view.dayNight", true), gradientsOnly: store.get("view.gradientsOnly", false) };
  function applyView() {
    dome.options.stars = dome.options.moon = VIEW.stars;
    dome.options.dayNight = VIEW.dayNight;
    dome.options.gradientsOnly = VIEW.gradientsOnly;
    document.querySelectorAll("[data-view]").forEach((n) => {
      n.classList.toggle("on", !!VIEW[n.dataset.view]);
      n.setAttribute("aria-checked", String(!!VIEW[n.dataset.view]));
    });
  }
  document.querySelectorAll("[data-view]").forEach((n) => n.addEventListener("click", () => {
    const k = n.dataset.view;
    VIEW[k] = !VIEW[k];
    store.set("view." + k, VIEW[k]);
    applyView();
  }));
  applyView();

  // The snapshot the renderer reads (the sketch's format), eased toward each telemetry frame so the
  // 30 Hz updates never step.
  const N = 128;
  const snap = {
    nodeCount: 0,
    nodes: { mlat: new Float32Array(N), mlon: new Float32Array(N), green: new Float32Array(N), red: new Float32Array(N), violet: new Float32Array(N) },
    activity: 0, unloading: 0, gain: 1, eqLat: 66, poleLat: 72, dayShiftDeg: 4, mlt: 0, stormRed: 0, modelTimeUtc: 0,
    sky: { sunAz: 0, sunEl: -30, moonAz: 0, moonEl: -30, moonIllum: 0 },
  };
  let target = null, primed = false;
  const SCALARS = [["activity", "sky", "activity"], ["unloading", "sky", "unloading"], ["gain", "sky", "gain"], ["mlt", "sky", "mlt"],
    ["eqLat", "dome", "eqLat"], ["poleLat", "dome", "poleLat"], ["dayShiftDeg", "dome", "dayShift"], ["stormRed", "dome", "stormRed"]];

  // T6.6: a critically damped follower, two one-poles in series (τ 85 ms each), so the curtain's
  // velocity doesn't kink at every 30 Hz frame. Angles ease the short way round.
  const mid = { mlat: new Float32Array(N), mlon: new Float32Array(N), green: new Float32Array(N), red: new Float32Array(N), violet: new Float32Array(N) };
  const midScalar = {};
  const TAU = 0.085;
  const wrapTo = (x, period) => ((((x + period / 2) % period) + period) % period) - period / 2;
  const PERIOD = { mlt: 24 };

  function easeSnap(dt) {
    const { sky, dome: d } = target;
    const k = primed ? 1 - Math.exp(-dt / TAU) : 1;
    const n = Math.min(N, d.n | 0);
    snap.nodeCount = n;
    const nd = snap.nodes;
    for (let i = 0; i < n; i++) {
      for (const f of ["mlat", "green", "red", "violet"]) {
        mid[f][i] += (d[f][i] - mid[f][i]) * k;
        nd[f][i] += (mid[f][i] - nd[f][i]) * k;
      }
      mid.mlon[i] += wrapTo(d.mlon[i] - mid.mlon[i], 360) * k;
      nd.mlon[i] += wrapTo(mid.mlon[i] - nd.mlon[i], 360) * k;
      if (Math.abs(nd.mlon[i]) > 540) { nd.mlon[i] = wrapTo(nd.mlon[i], 360); mid.mlon[i] = wrapTo(mid.mlon[i], 360); }
    }
    for (const [key, src, field] of SCALARS) {
      const v = (src === "sky" ? sky : d)[field];
      if (typeof v !== "number") continue;
      const p = PERIOD[key];
      if (!(key in midScalar) || !primed) midScalar[key] = v;
      midScalar[key] += (p ? wrapTo(v - midScalar[key], p) : v - midScalar[key]) * k;
      snap[key] += (p ? wrapTo(midScalar[key] - snap[key], p) : midScalar[key] - snap[key]) * k;
      if (p) snap[key] = ((snap[key] % p) + p) % p;
    }
    // model time runs on between frames at the replay speed, so the stars turn smoothly
    snap.modelTimeUtc = (sky.utc + (performance.now() / 1000 - target.at) * (sky.speed || 0)) * 1000;
    const a = snap.sky;
    a.sunAz = d.sunAz; a.sunEl = d.sunEl; a.moonAz = d.moonAz; a.moonEl = d.moonEl; a.moonIllum = d.moonIllum;
    primed = true;
  }

  // Emission per layer (red, green, violet) and the sky's colour, for the keyboard strip and the
  // glow behind the dome.
  const emission = [0, 0, 0];
  const glow = { r: 92, g: 242, b: 160, a: 0.06 };
  function measureEmission(d) {
    let r = 0, g = 0, v = 0;
    const n = d.n | 0;
    for (let i = 0; i < n; i++) { r += d.red[i]; g += d.green[i]; v += d.violet[i]; }
    if (!n) return;
    r /= n; g /= n; v /= n;
    emission[0] = clamp(r / 0.8, 0, 1); emission[1] = clamp(g / 0.9, 0, 1); emission[2] = clamp(v / 0.5, 0, 1);
    const tot = r + g + v + 1e-6;
    const mix = [(r * 255 + g * 92 + v * 143) / tot, (r * 107 + g * 242 + v * 123) / tot, (r * 107 + g * 160 + v * 255) / tot];
    const kk = 0.08;   // ~3 s at 30 Hz: the glow drifts, never flicks
    glow.r += (mix[0] - glow.r) * kk; glow.g += (mix[1] - glow.g) * kk; glow.b += (mix[2] - glow.b) * kk;
    glow.a += (clamp(0.04 + 0.12 * tot, 0.04, 0.2) - glow.a) * kk;
  }
  let glowShown = 0;
  function showGlow() {
    const t = performance.now();
    if (t - glowShown < 250) return;
    glowShown = t;
    // written as the stage's own background, not as an inherited custom property: a custom property
    // on #stage restyled every element under it four times a second (T6.7: 63 → 42 % of a core)
    const c = `${glow.r | 0}, ${glow.g | 0}, ${glow.b | 0}`;
    $("stage").style.backgroundImage = `radial-gradient(circle at 676px 374px, rgba(${c}, ${glow.a.toFixed(3)}) 0, rgba(${c}, 0) 330px)`;
  }

  // ---------------------------------------------------------------- the day
  /** The day's name from the transport's list (T6.9: the plug-in sends it with the day). */
  const dayLabel = (id) => (UI.dayLabel ? UI.dayLabel(id) : id);
  /** A live number in a fixed slot of w characters (mono), so nothing beside it moves when it changes. */
  const num = (text, w, tag = "b", cls = "") => `<${tag} class="num${cls ? " " + cls : ""}" style="--w:${w}">${text}</${tag}>`;
  const signed = (v, d) => (v >= 0 ? "+" : "−") + Math.abs(v).toFixed(d);
  const WINDS = ["N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"];
  const wind = (az) => WINDS[Math.round(((az % 360) + 360) % 360 / 22.5) % 16];
  const PHASES = ["quiet", "growth", "onset", "recovery"];
  const KIND = { loaded: "energy released", forced: "seen in the magnetometer", pseudo: "a small one" };
  /** What happened in the day, in time order: onsets and the phases' beginnings (the Sky block's last event). */
  let dayEvents = [];

  WT.on("day", (day) => {
    if (S.day && S.day.day !== day.day) S.journeyFrom = undefined;   // T6.9: a new day, joined where it lands
    S.day = day;
    strip.setDay(day);
    stripBox.setAttribute("aria-valuemin", String(day.t0));
    stripBox.setAttribute("aria-valuemax", String(day.t0 + day.phase.length * day.step));
    ariaMinute = null;
    dayEvents = day.onsets.map((o) => ({ utc: o.utc, say: "substorm onset, " + (KIND[o.kind] || o.kind) }));
    for (let i = 1; i < day.phase.length; i++)
      if (day.phase[i] !== day.phase[i - 1] && PHASES[day.phase[i]] !== "onset")
        dayEvents.push({ utc: day.t0 + i * day.step, say: PHASES[day.phase[i]] + " begins" });
    dayEvents.sort((x, y) => x.utc - y.utc);
    const st = day.station;
    if (st) {
      dome.obs = new SKY.Observer({ code: st.code, name: st.name, lat: st.lat, lon: st.lon, elevKm: st.elevKm }, { lat: st.poleLat, lon: st.poleLon });
      $("dome-station").textContent = st.code;
      $("dome-place").textContent = st.name;
      $("info-station").innerHTML =
        `<span class="k">station</span><span><b>${st.code}</b><span class="gap">${st.name}</span></span>` +
        `<span class="k">position</span><span>${Math.abs(st.lat).toFixed(2)}° ${st.lat >= 0 ? "N" : "S"}<span class="gap">${Math.abs(st.lon).toFixed(2)}° ${st.lon >= 0 ? "E" : "W"}</span><span class="gap">${Math.round(st.elevKm * 1000)} m</span></span>` +
        `<span class="k">geomag</span><span>${st.mlat.toFixed(1)}° lat<span class="gap">${st.mlon.toFixed(1)}° lon (dipole)</span></span>` +
        `<span class="k">zenith</span><span>rays converge at <b>${st.mzEl.toFixed(0)}°</b><span class="gap">${wind(st.mzAz)} (MZ)</span></span>` +
        `<span class="k">sky</span><span id="info-astro">—</span>`;
    }
  });

  // ---------------------------------------------------------------- Play mode (T3.7)
  // Mood and scale only steer the sky's chords; in Play mode the Play controls take their place in
  // the Harmony card, and the keyboard strip shows the keys you hold.
  const noteName = (n) => UI.KEY_NAMES[((n % 12) + 12) % 12] + (Math.floor(n / 12) - 1);
  WT.onParam("play", (v) => $("harmony").classList.toggle("is-play", !!v));

  // ---------------------------------------------------------------- the chord, why, the rule
  const FN = ["home", "tension", "awe", "borrowed"];
  const FN_WHY = { home: "the sky is quiet", tension: "the sky is in growth", awe: "a substorm has broken out", borrowed: "the sky is recovering" };
  const FN_TITLE = { home: "home", tension: "tension", awe: "the awe move", borrowed: "borrowed chord" };
  let chordSig = "", chordTimer = 0, ruleShown = -2;
  const ut = (utc) => new Date(utc * 1000).toISOString().slice(11, 16);
  /** Who writes the chords, in words (and weight when it is you): the head of the Harmony card. */
  function showState(text, cls) {
    const n = $("chord-state");
    if (n.textContent !== text) n.textContent = text;
    if (n.className !== cls) n.className = "note" + (cls ? " " + cls : "");
  }
  /** The rule row: the phase → chord rule that applies now is filled (none for a held or played chord). */
  function showRule(i) {
    if (i === ruleShown) return;
    ruleShown = i;
    [...$("rule").children].forEach((n, k) => n.classList.toggle("on", k === i));
  }

  function showChord(c) {
    if (!c) return;
    if (c.play) { showPlayed(c); return; }
    const fn = FN[c.function] || "home";
    const sig = `${c.label}|${c.roman}|${c.latched}`;
    if (sig !== chordSig) {
      const first = chordSig === "";
      chordSig = sig;
      const text = $("chord-text");
      const apply = () => {
        text.textContent = c.label || "—";
        $("chord-roman").textContent = c.roman || "";
        text.classList.remove("out");
      };
      clearTimeout(chordTimer);
      if (first) apply(); else { text.classList.add("out"); chordTimer = setTimeout(apply, 900); }
      $("chord-why").innerHTML = c.latched ? `<b>held on MIDI</b>, the sky keeps listening` : `<b>${FN_TITLE[fn]}</b>, because ${FN_WHY[fn]}`;
    }
    const hold = !c.latched && WT.get("progression") < 0.005;
    if (c.latched) showState("a chord held on MIDI", "held");
    else if (hold) showState("Progression holds the chord", "hold");
    else showState(c.pending ? "the sky writes, a chord waits" : "the sky writes the chords", "");
    showRule(c.latched ? -1 : FN.indexOf(fn));
  }

  /** Play mode: the held notes' name at once (no fade: you are playing). */
  function showPlayed(c) {
    const sig = `play|${c.label}|${c.held}`;
    if (sig !== chordSig) {
      chordSig = sig;
      clearTimeout(chordTimer);
      const text = $("chord-text");
      text.classList.remove("out");
      text.textContent = c.held ? c.label || "—" : "—";
      $("chord-roman").textContent = "";
      const drone = WT.get("play_drone");
      $("chord-why").innerHTML = (c.held ? `<b>played</b>, ${c.held} note${c.held === 1 ? "" : "s"} held` : `<b>played</b>, waiting for notes`)
        + `, ${drone < 0.005 ? "the drone rests" : "the drone under at " + Math.round(drone * 100) + " %"}`;
    }
    showState("your keyboard plays the resonators", "held");
    showRule(-1);
  }

  /** The keyboard strip's keys: the chord's voices (or the keys you hold), each in its band's colour. */
  const BAND_OF = ["ink", "red", "green", "violet"];
  const lit = new Map();
  function litKeys(tel) {
    lit.clear();
    const low = tel.low && tel.low.note >= 0 ? tel.low.note : -1;
    const c = tel.chord;
    if (WT.get("play") || (c && c.play)) {
      // a played note sounds in the band whose range holds it (T3.8's edges, as the engine voiced them)
      const e = c && c.edges && c.edges.length === 5 ? c.edges : ["band_floor", "band_x1", "band_x2", "band_x3", "band_ceiling"].map((id) => WT.get(id));
      const band = (n) => BAND_OF[n < e[1] ? 0 : n < e[2] ? 1 : n < e[3] ? 2 : 3];
      for (const [n, level, held] of (tel.play && tel.play.voices) || [])
        lit.set(n, [band(n), held ? 0.45 + 0.55 * clamp(level, 0, 1) : 0.6 * clamp(level, 0, 1)]);
    } else if (c && c.notes) {
      const lv = [P("red_level"), P("green_level"), P("violet_level")];
      c.notes.forEach((layer, l) => {
        const a = (0.3 + 0.7 * emission[l]) * (0.35 + 0.65 * lv[l]);
        for (const n of layer) lit.set(n, [BAND_OF[l + 1], a]);
      });
    }
    if (low >= 0 && !lit.has(low)) lit.set(low, ["ink", 0.35 + 0.5 * clamp(P("low_level"), 0, 1)]);
    return lit;
  }

  // ---------------------------------------------------------------- the Sky block, the clock, Details
  let textShown = 0;
  const PHASE_TITLE = { quiet: "Quiet", growth: "Growth", onset: "Onset", recovery: "Recovery" };

  function showSky(sky, d) {
    if (!sky) return;
    if (S.journeyFrom === undefined) S.journeyFrom = sky.utc;
    const t = performance.now();
    if (t - textShown < 250) return;   // text at 4 Hz is plenty
    textShown = t;
    const date = new Date(sky.utc * 1000);
    setHtml("ro-date", date.toLocaleDateString("en-GB", { day: "numeric", month: "short", year: "numeric", timeZone: "UTC" }));
    setHtml("ro-ut", `<b>${ut(sky.utc)}</b> UT`);

    // the Sky block: phase and how far through it; wind, Bz, Kp; energy against the release mark,
    // activity; the last thing that happened
    setHtml("sky-ph", PHASE_TITLE[sky.phase] || "—");
    $("sky-prog").firstChild.style.width = (clamp(sky.progress, 0, 1) * 100).toFixed(1) + "%";
    const bzSay = sky.bz < -0.5 ? "S" : sky.bz > 0.5 ? "N" : "";
    setHtml("sky-nums", `${num(Math.round(sky.wind), 4)} <span class="u">km/s</span> Bz ${num(signed(sky.bz, 1), 5)} <span class="u num l" style="--w:1">${bzSay}</span> Kp ${num(sky.kp.toFixed(1), 3, "b", "l")}`);
    const day = S.day;
    let thr = 1;
    if (day && day.threshold && day.threshold.length) thr = day.threshold[clamp(Math.round((sky.utc - day.t0) / day.step), 0, day.threshold.length - 1)] || 1;
    const full = Math.max(1.5, thr * 1.25, sky.energy);
    const bar = $("sky-energy");
    bar.firstChild.style.width = (clamp(sky.energy / full, 0, 1) * 100).toFixed(1) + "%";
    bar.lastChild.style.left = (clamp(thr / full, 0, 1) * 100).toFixed(1) + "%";
    $("sky-act").firstChild.style.width = (clamp(sky.activity, 0, 1) * 100).toFixed(1) + "%";
    let ev = null;
    for (const e of dayEvents) { if (e.utc > sky.utc) break; ev = e; }
    setHtml("sky-ev", ev ? `<b>${ut(ev.utc)}</b>${ev.say}` : "the recording begins");

    if (d) {
      setHtml("dome-astro", `sun ${num(signed(d.sunEl, 1) + "°", 6)}${num(wind(d.sunAz), 3, "span", "gap")}<br>moon ${num(Math.round(d.moonIllum * 100) + " %", 5)}${num(signed(d.moonEl, 0) + "°", 4, "span", "gap")}`);
      if (details.open && $("info-astro")) $("info-astro").innerHTML = `sun ${d.sunEl.toFixed(1)}° el, moon ${Math.round(d.moonIllum * 100)} % lit, ${d.moonEl.toFixed(0)}° el`;
    }
    setHtml("dome-oval", `arc ${num(sky.arcLat.toFixed(1) + "°", 5)} mlat<br>MLT ${num(sky.mlt.toFixed(1) + " h", 6)}`);
    if (details.open) showJourney(sky);
  }

  /** How long a whole day lasts at `speed`: 1 h at 24×, 6 min at 240×. */
  const perDay = (speed) => { const m = 1440 / speed; return m >= 60 ? +(m / 60).toFixed(1) + " h" : Math.round(m) + " min"; };
  function showJourney(sky) {
    const day = S.day;
    // T6.9: the transport's state, read-only here
    const j = S.tel.journey || { speed: sky.speed, frozen: !(sky.speed > 0), loop: false, ended: false };
    const run = j.speed || sky.speed || 0;
    const state = (j.ended ? "the recording has ended" : j.frozen ? "frozen" : run > 1 ? perDay(run) + " of listening per day" : "real time")
      + (j.loop ? ", looping" : "");
    $("info-journey").innerHTML =
      `<span class="k">day</span><span><b>${dayLabel(sky.day)}</b></span>` +
      `<span class="k">speed</span><span>${num(Math.round(run) + "×", 5, "b", "l")}<span class="gap">${state}</span></span>` +
      `<span class="k">now</span><span><b>${new Date(sky.utc * 1000).toLocaleDateString("en-GB", { day: "numeric", month: "short", timeZone: "UTC" })} ${ut(sky.utc)} UT</b><span class="gap">MLT ${num(sky.mlt.toFixed(1), 4, "span")} h</span></span>`;
    // the sky's full numbers (the Sky block keeps only a few)
    const c = S.tel.chord;
    $("info-sky").innerHTML =
      `<span class="k">phase</span><span><b>${sky.phase}</b><span class="gap">${Math.round(clamp(sky.progress, 0, 1) * 100)} % through</span></span>` +
      `<span class="k">Kp</span><span>${num(sky.kp.toFixed(1), 3, "b", "l")}${num(sky.kp >= 5 ? "storm" : "", 5, "b", "l state")}</span>` +
      `<span class="k">wind</span><span>${num(Math.round(sky.wind), 4)} km/s<span class="gap">Bz ${num(signed(sky.bz, 1), 5)} nT</span>${num(sky.bz < 0 ? "south" : "", 5, "b", "l state")}</span>` +
      `<span class="k">field</span><span>ΔH ${num(Math.round(sky.dh), 5)} nT<span class="gap">model ${num(Math.round(sky.dhModel), 5, "b", "l")}</span></span>` +
      `<span class="k">coupling</span><span>E ${num((sky.coupling || 0).toFixed(1), 4)} mV/m</span>` +
      `<span class="k">stored</span><span>energy ${num(sky.energy.toFixed(2), 4, "b", "l")}<span class="gap">activity ${num(sky.activity.toFixed(2), 4, "b", "l")}</span></span>` +
      `<span class="k">counts</span><span>${num(sky.onsets, 2, "b")} onsets<span class="gap">${num(sky.whistlers, 2, "b")} whistlers</span><span class="gap">${num(c && c.changes ? c.changes : 0, 3, "b")} chords</span></span>`;
    if (!day) return;
    const span = day.phase.length * day.step;
    const f = clamp((sky.utc - day.t0) / span, 0, 1);
    $("journey-bar").querySelector("i").style.width = (f * 100).toFixed(2) + "%";
    if (S.journeyFrom === undefined) S.journeyFrom = sky.utc;   // where this session joined the day
    $("journey-bar").querySelector("u").style.left = (clamp((S.journeyFrom - day.t0) / span, 0, 1) * 100).toFixed(2) + "%";
    const leftS = (day.t0 + span - sky.utc) / Math.max(1e-6, run);
    const past = day.onsets.filter((o) => o.utc <= sky.utc).length;
    $("info-journey-2").innerHTML =
      `<span class="k">left</span><span>${num(j.ended ? "0 min" : Math.round(leftS / 60) + " min", 7, "b", "l")} of listening to the end of the recording${j.frozen && !j.ended ? ", once it runs" : ""}</span>` +
      `<span class="k">onsets</span><span>${num(past, 2, "b", "l")} of ${day.onsets.length} in this day</span>` +
      `<span class="k">at the end</span><span>${j.loop ? "it starts again from the beginning" : "it holds there; Run plays it again"}</span>`;
  }

  // ---------------------------------------------------------------- telemetry
  let env = 0;
  WT.on("telemetry", (tel) => {
    S.tel = tel;
    if (tel.sky && tel.dome && tel.dome.n) {
      target = { sky: tel.sky, dome: tel.dome, at: performance.now() / 1000 };
      measureEmission(tel.dome);
    }
    showSky(tel.sky, tel.dome);
    showChord(tel.chord);
    env += (Math.max(tel.peakL || 0, tel.peakR || 0) - env) * 0.05;
    if (tel.autoGain) {
      const db = 20 * Math.log10(tel.autoGain);
      setHtml("out-note", `auto ${num(signed(db, 1), 4, "span")} dB`);
    }
  });

  // ---------------------------------------------------------------- render loop
  const P = (id) => WT.get(id);
  const EQ_MOD = ["eq1_gain", "eq2_gain", "eq3_gain", "eq4_freq", "eq1_drive", "eq2_drive", "eq3_drive", "eq4_drive"];
  let last = 0;
  // T6.6: the dome draws at the display's rate (up to 60 fps; 30 with reduced motion), the
  // instrument pictures at 30.
  let lastDome = 0;
  function frame(tms) {
    requestAnimationFrame(frame);
    if (document.hidden) return;
    const t = tms / 1000;
    const tel = S.tel;

    if (target && t - lastDome >= (reduced ? 1 / 30 : 1 / (S.domeHz || 61))) {
      const dtDome = Math.min(0.2, t - lastDome);
      lastDome = t;
      easeSnap(dtDome);
      // Breath swells the whole sky a little; the sound's own level adds a faint glow (spec/06).
      const breath = (tel.breath || 0) * P("macro_breath");
      dome.options.exposure = 1.6 * (1 + 0.16 * breath) * (1 + 0.1 * clamp(env * 3, 0, 1));
      const t0 = performance.now();
      dome.render(snap, reduced ? t * 0.35 : t);
      // the dome's cost per frame, eased (T6.6; the UI check and the T6.4 cost pass read it)
      S.domeMs = (S.domeMs || 0) + (performance.now() - t0 - (S.domeMs || 0)) * 0.05;
      S.meshMs = dome.meshMs;
      S.frames++;
      showGlow();
    }

    if (t - last < (reduced ? 1 / 15 : 1 / 30) - 0.002) return;
    last = t;

    if (tel.sky) {
      const u = shownUtc();
      strip.draw(u);
      stripAria(u, false);
    }
    hmeter.draw(tel.peakL || 0, tel.peakR || 0, P("lim_ceiling"));

    // the rings on the knobs the grid moves: the dot is the value the engine uses now
    updateRings();

    res.draw(tel);
    const low = tel.low && tel.low.note >= 0 ? tel.low.note : -1;
    if (low !== S.lowShown) {
      S.lowShown = low;
      $("low-note").innerHTML = low >= 0 ? `<b class="num" style="--w:3">${noteName(low)}</b>` : "";
    }
    keys.draw(litKeys(tel));
    // the EQ's values now, where the grid moves them (T4.2); the cut also arrives as tel.eqCutHz
    const eqLive = {};
    for (const id of EQ_MOD) { const m = UI.modLive(id); if (m) eqLive[id] = m.now; }
    eq.draw(tel, eqLive);
    if (UI.motion) UI.motion.draw(tel);
    {
      fx.phaser.draw(tel);
      fx.chorus.draw(tel);
      fx.delay.draw(tel);
      fx.reverb.draw(tel);
      fx.output.draw(tel);
      setHtml("verb-note", `${num(P("rev_decay").toFixed(1), 4, "span")} s`);
    }
  }
  requestAnimationFrame(frame);

  if (!WT.isMock) WT.native("uiReady");
})();
