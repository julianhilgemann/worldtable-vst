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
    const k = UI.Knob(n, n.dataset.knob, n.dataset.label || "", { size: n.dataset.size, color: n.dataset.color });
    if (k) knobs[n.dataset.knob] = k;
  }
  for (const n of document.querySelectorAll("[data-seg]")) UI.Seg(n, n.dataset.seg, n.dataset.labels ? n.dataset.labels.split(",") : null);
  for (const n of document.querySelectorAll("[data-keys]")) UI.Keys(n, n.dataset.keys);
  for (const n of document.querySelectorAll("[data-toggle]")) UI.Toggle(n, n.dataset.toggle, n.dataset.label || "");
  for (const n of document.querySelectorAll("[data-play]")) UI.PlayButton(n, n.dataset.play);

  // ---------------------------------------------------------------- the hint line (spec/09 §2 rule 5)
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

  // ---------------------------------------------------------------- macro rings (T6.7)
  // The macros are hard-wired until T4.2: each scales what it moves around its neutral 0.5
  // (dsp/Instrument.cpp macroGain, stormGain; Breath in dsp/SkySound.cpp). The rings on the knobs
  // they move are drawn from the same formulas: the arc is the target with the macro at 0 and at 1
  // (Breath also at the top of its swell), the dot is the target now.
  const macroGain = (m) => 0.5 + m;
  const breathGain = (d, c) => 1 - 0.35 * d * c;
  const breathViolet = (d, c) => breathGain(d, c) * (1 - 0.15 * d * c);
  const MACROS = (UI.MACROS = {
    macro_storm: { moves: { progression: (m) => 0.4 + 1.2 * m }, say: "the strike, Progression" },
    macro_breath: { moves: { red_level: breathGain, green_level: breathGain, violet_level: breathViolet }, cycle: true, say: "red, green and violet levels" },
    macro_glow: { moves: { violet_level: macroGain, rev_shimmer: macroGain }, say: "Violet level, Shimmer" },
    macro_space: { moves: { dly_mix: macroGain, rev_mix: macroGain }, say: "Delay mix, Reverb mix" },
    macro_drift: { moves: { detune: macroGain, drift: macroGain }, say: "Detune, Drift" },
  });
  const TARGETS = {};   // target id → [[macro id, gain(m, cycle), uses the breath cycle]]
  for (const [mid, mac] of Object.entries(MACROS))
    for (const [tid, f] of Object.entries(mac.moves)) (TARGETS[tid] = TARGETS[tid] || []).push([mid, f, !!mac.cycle]);
  /** Puts each target's ring where the macros have it; `cycle` is Breath's swell now (0…1). */
  function updateRings(cycle) {
    for (const [tid, srcs] of Object.entries(TARGETS)) {
      const k = knobs[tid];
      if (!k) continue;
      let lo = 1, hi = 1, now = 1;
      for (const [mid, f, usesCycle] of srcs) {
        const ends = usesCycle ? [f(0, 0), f(0, 1), f(1, 0), f(1, 1)] : [f(0), f(1)];
        lo *= Math.min(...ends);
        hi *= Math.max(...ends);
        now *= f(WT.get(mid), usesCycle ? cycle : 0);
      }
      const base = WT.get(tid);
      k.setMod(base * lo, base * hi, base * now);
    }
  }
  UI.updateRings = () => updateRings(S.tel.breath || 0);
  // a macro says what it moves, and lights those knobs while it is pointed at or focused
  for (const [mid, mac] of Object.entries(MACROS)) {
    const k = knobs[mid];
    if (!k) continue;
    k.root.dataset.hint = `${UI.hintOf(mid)} Moves: ${mac.say}.`;
    const light = (on) => () => { for (const tid of Object.keys(mac.moves)) if (knobs[tid]) knobs[tid].setLit(on); };
    k.root.addEventListener("pointerenter", light(true));
    k.root.addEventListener("pointerleave", () => { if (document.activeElement !== k.root) light(false)(); });
    k.root.addEventListener("focus", light(true));
    k.root.addEventListener("blur", light(false));
  }
  UI.updateRings();

  // ---------------------------------------------------------------- details popover
  // One page (T6.5); observer, journey and data sources open over it from the header.
  const hash = new URLSearchParams(location.hash.slice(1));
  // #shot: no CSS transitions (headless review screenshots run on virtual time, which they ignore)
  if (hash.has("shot")) document.documentElement.classList.add("shot");
  /** A button that opens a panel; Escape or a click outside closes it. */
  function popover(btnId, panelId, onOpen) {
    const btn = $(btnId), panel = $(panelId);
    const p = { open: false, set(open) {
      p.open = open;
      panel.hidden = !open;
      btn.setAttribute("aria-expanded", String(open));
      if (open && onOpen) onOpen();
    } };
    btn.addEventListener("click", (e) => { e.stopPropagation(); p.set(!p.open); });
    panel.addEventListener("click", (e) => e.stopPropagation());
    document.addEventListener("click", () => { if (p.open) p.set(false); });
    document.addEventListener("keydown", (e) => { if (e.key === "Escape" && p.open) p.set(false); });
    return p;
  }
  const details = popover("details-btn", "details", () => { if (S.tel.sky) showJourney(S.tel.sky); });
  const viewPop = popover("view-btn", "view-panel");
  if (hash.has("details")) details.set(true);   // for review screenshots
  // mock only, for review screenshots: #set=play:1,flt_cutoff:800
  if (WT.isMock && hash.has("set"))
    for (const kv of hash.get("set").split(",")) {
      const [k, v] = kv.split(":");
      if (WT.META[k]) WT.set(k, Number(v));
    }
  if (hash.has("view")) viewPop.set(true);

  // ---------------------------------------------------------------- scaling
  function rescale() {
    UI.scale = Math.min(window.innerWidth / BASE_W, window.innerHeight / BASE_H);
    UI.dpr = window.devicePixelRatio || 1;
    const x = (window.innerWidth - BASE_W * UI.scale) / 2, y = (window.innerHeight - BASE_H * UI.scale) / 2;
    $("stage").style.transform = `translate(${x}px, ${y}px) scale(${UI.scale})`;
  }
  window.addEventListener("resize", rescale);
  rescale();

  // ---------------------------------------------------------------- views
  const voicing = UI.VoicingView($("cv-voicing"));
  const phaseTrack = UI.PhaseTrack($("phase-track"));
  const ticker = UI.Ticker($("ticker"));
  const mag = UI.MagnetogramView($("cv-mag"));
  const layers = UI.LayersView($("cv-layers"));
  const keys = UI.KeysView($("cv-keys"));
  const fx = {
    filter: UI.FilterView($("cv-filter")),
    drive: UI.DriveView($("cv-drive")), phaser: UI.PhaserView($("cv-phaser")), chorus: UI.ChorusView($("cv-chorus")),
    delay: UI.DelayView($("cv-delay")), reverb: UI.ReverbView($("cv-reverb")), output: UI.OutputView($("cv-output")),
  };
  const hmeter = UI.HeaderMeter($("cv-hmeter"));

  // T6.6: the magnetogram is the Journey's timeline: hover shows a moment, a click jumps there
  {
    const cv = $("cv-mag");
    const xOf = (e) => (e.clientX - cv.getBoundingClientRect().left) / UI.scale;
    cv.addEventListener("pointermove", (e) => {
      const x = xOf(e);
      mag.setHover(x);
      cv.style.cursor = mag.utcAt(x) === null ? "default" : "pointer";
    });
    cv.addEventListener("pointerleave", () => mag.setHover(null));
    cv.addEventListener("click", (e) => {
      const utc = mag.utcAt(xOf(e));
      if (utc === null) return;
      WT.native("seek", utc);
      ticker.push(`<span class="t">${new Date(utc * 1000).toISOString().slice(11, 16)} UT</span>jump to ${mag.phaseAt(utc)}`);
    });
  }

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

  // Emission per layer (red, green, violet) and the sky's colour, for the voicing view, the glow
  // behind the dome and the macro rings.
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
    $("stage").style.backgroundImage = `radial-gradient(circle at 664px 520px, rgba(${c}, ${glow.a.toFixed(3)}) 0, rgba(${c}, 0) 330px)`;
  }

  // ---------------------------------------------------------------- the day
  const DAY_LABELS = {
    "2024-05-10": "10–11 May 2024, Gannon storm (Kp 9)",
    "2025-01-19": "19–20 Jan 2025, moderate, substorms",
    "2025-01-25": "25–26 Jan 2025, quiet",
  };
  /** A live number in a fixed slot of w characters (mono), so nothing beside it moves when it changes. */
  const num = (text, w, tag = "b", cls = "") => `<${tag} class="num${cls ? " " + cls : ""}" style="--w:${w}">${text}</${tag}>`;
  const signed = (v, d) => (v >= 0 ? "+" : "−") + Math.abs(v).toFixed(d);
  const WINDS = ["N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"];
  const wind = (az) => WINDS[Math.round(((az % 360) + 360) % 360 / 22.5) % 16];
  const latLon = (lat, lon) => `${Math.abs(lat).toFixed(2)}${lat >= 0 ? "N" : "S"} ${Math.abs(lon).toFixed(2)}${lon >= 0 ? "E" : "W"}`;

  WT.on("day", (day) => {
    S.day = day;
    mag.setDay(day);
    const st = day.station;
    if (st) {
      dome.obs = new SKY.Observer({ code: st.code, name: st.name, lat: st.lat, lon: st.lon, elevKm: st.elevKm }, { lat: st.poleLat, lon: st.poleLon });
      $("ro-station").innerHTML = `${st.code} <b>${latLon(st.lat, st.lon)}</b>`;
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
  // Key, mood, scale and shape only steer the sky's chords; in Play mode the keyboard strip and the
  // Play controls take their place in the same card.
  const noteName = (n) => UI.KEY_NAMES[((n % 12) + 12) % 12] + (Math.floor(n / 12) - 1);
  let playShown = null;
  WT.onParam("play", (v) => {
    const on = !!v;
    $("harmony").classList.toggle("is-play", on);
    $("harmony-title").textContent = on ? "Play" : "Harmony";
    $("harmony-note").textContent = on ? "MIDI plays the resonators, the sky colours them" : "the sky writes the progression, MIDI holds a chord";
    if (playShown !== null && playShown !== on && S.tel.sky)
      ticker.push(`<span class="t">${new Date(S.tel.sky.utc * 1000).toISOString().slice(11, 16)} UT</span>${on ? "<b>Play</b>, your notes take over" : "Play off, the sky writes again"}`);
    playShown = on;
  });

  // ---------------------------------------------------------------- chord readout
  const FN = ["home", "tension", "awe", "borrowed"];
  const FN_WHY = { home: "the sky is quiet", tension: "the sky is in growth", awe: "a substorm has broken out", borrowed: "the sky is recovering" };
  const FN_TITLE = { home: "home", tension: "tension", awe: "the awe move", borrowed: "borrowed chord" };
  const FN_REASON = { home: "quiet → home", tension: "growth → tension", awe: "onset → chromatic mediant", borrowed: "recovery → borrowed" };
  let chordSig = "", chordChanges = -1, chordTimer = 0;
  const ut = (utc) => new Date(utc * 1000).toISOString().slice(11, 16);

  function showChord(c, sky) {
    if (!c) return;
    voicing.setChord(c.notes);
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
      if (!first && chordChanges >= 0 && sky)
        ticker.push(`<span class="t">${ut(sky.utc)} UT</span>${c.latched ? "MIDI" : FN_REASON[fn]} → <b>${c.label}</b> <span class="dim">${c.roman}</span>`);
    }
    chordChanges = c.changes;
    const pill = $("chord-state");
    const hold = !c.latched && WT.get("progression") < 0.005;
    pill.className = "pill" + (c.latched ? " midi" : hold ? " hold" : "");
    pill.innerHTML = `<i></i><span>${c.latched ? "MIDI hold" : hold ? "Hold" : "The sky writes"}</span>` + (c.pending ? `<span class="next">next chord waiting</span>` : "");
    $("chord-count").textContent = c.changes ? `${c.changes} change${c.changes === 1 ? "" : "s"} so far` : "";
  }

  /** Play mode: the held notes' name at once (no fade: you are playing), no ticker per chord. */
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
    const pill = $("chord-state");
    pill.className = "pill play";
    pill.innerHTML = `<i></i><span>Play</span>`;
    $("chord-count").textContent = "";
  }

  // ---------------------------------------------------------------- sky readouts and events
  let lastPhase = "", started = false, textShown = 0;
  const KIND = { loaded: "energy released", forced: "data-forced", pseudo: "pseudo-breakup" };

  function showSky(sky, d) {
    if (!sky) return;
    if (S.journeyFrom === undefined) S.journeyFrom = sky.utc;
    if (!started && S.day) {
      // open with what the day has already done: the last two onsets before now, then the start
      started = true;
      for (const e of S.day.onsets.filter((o) => o.utc < sky.utc - 30).slice(-2))
        ticker.push(`<span class="t">${ut(e.utc)} UT</span>substorm onset, ${KIND[e.kind] || e.kind}`);
      ticker.push(`<span class="t">${ut(sky.utc)} UT</span>journey ${DAY_LABELS[sky.day] || sky.day}, at ${Math.round(sky.speed)}×`);
    }
    for (const e of sky.newOnsets || [])
      ticker.push(`<span class="t">${ut(e.utc)} UT</span>${e.kind === "pseudo" ? "substorm onset" : "<b>substorm onset</b>"}, ${KIND[e.kind] || e.kind}, strength ${e.strength.toFixed(2)}`);
    if (lastPhase && sky.phase !== lastPhase && sky.phase !== "onset")
      ticker.push(`<span class="t">${ut(sky.utc)} UT</span>${sky.phase} begins`);
    lastPhase = sky.phase;

    phaseTrack.set(sky.phase, sky.progress);

    const t = performance.now();
    if (t - textShown < 250) return;   // text at 4 Hz is plenty
    textShown = t;
    const date = new Date(sky.utc * 1000);
    $("ro-date").innerHTML = `<b class="num l" style="--w:11">${date.toLocaleDateString("en-GB", { day: "numeric", month: "short", year: "numeric", timeZone: "UTC" })}</b>`;
    $("ro-ut").innerHTML = `<b>${ut(sky.utc)}</b> UT`;
    $("ro-kp").innerHTML = `Kp ${num(sky.kp.toFixed(1), 3, "b", "l")}${num(sky.kp >= 5 ? "storm" : "", 5, "b", "l state")}`;
    $("ro-bz").innerHTML = `Bz ${num(signed(sky.bz, 1), 5)} nT${num(sky.bz < 0 ? "south" : "", 5, "b", "l state")}`;
    $("ro-v").innerHTML = `v ${num(Math.round(sky.wind), 4)} km/s`;
    const frozen = (sky.speed || 0) < 0.01;
    $("mode").innerHTML = `<i></i>${frozen ? "Frozen" : "Journey"} <b id="mode-speed" class="num l" style="--w:4">${frozen ? "" : Math.round(sky.speed) + "×"}</b>`;

    const e = clamp(sky.energy / 1.5, 0, 1);
    $("sky-row").innerHTML =
      `<span>ΔH ${num(Math.round(sky.dh), 5)} nT <span class="dim">model ${num(Math.round(sky.dhModel), 5, "span", "l")}</span></span>` +
      `<span>E ${num((sky.coupling || 0).toFixed(1), 4)} mV/m</span>` +
      `<span>energy<span class="bar"><i style="width:${(e * 100).toFixed(1)}%"></i><u style="left:${(100 / 1.5).toFixed(1)}%"></u></span>${num(sky.energy.toFixed(2), 4, "b", "l")}</span>` +
      `<span>activity ${num(sky.activity.toFixed(2), 4, "b", "l")}</span>` +
      `<span>whistlers ${num(sky.whistlers, 2, "b", "l")}</span>`;
    $("sky-note").textContent = { quiet: "quiet: the arc rests low", growth: "growth: loading from the solar wind", onset: "onset: the reservoir lets go", recovery: "recovery: patchy, pulsating" }[sky.phase] || "";
    $("sky-onsets").textContent = `${sky.onsets} onset${sky.onsets === 1 ? "" : "s"}`;
    if (d) {
      $("dome-astro").innerHTML = `sun ${num(signed(d.sunEl, 1) + "°", 6)}${num(wind(d.sunAz), 3, "span", "gap")}<br>moon ${num(Math.round(d.moonIllum * 100) + " %", 5)}${num(signed(d.moonEl, 0) + "°", 4, "span", "gap")}`;
      if ($("info-astro")) $("info-astro").innerHTML = `sun ${d.sunEl.toFixed(1)}° el, moon ${Math.round(d.moonIllum * 100)} % lit, ${d.moonEl.toFixed(0)}° el`;
    }
    $("dome-oval").innerHTML = `arc ${num(sky.arcLat.toFixed(1) + "°", 5)} mlat<br>MLT ${num(sky.mlt.toFixed(1) + " h", 6)}`;
    if (details.open) showJourney(sky);
  }

  function showJourney(sky) {
    const day = S.day;
    $("info-journey").innerHTML =
      `<span class="k">day</span><span><b>${DAY_LABELS[sky.day] || sky.day}</b></span>` +
      `<span class="k">speed</span><span>${num(Math.round(sky.speed) + "×", 4, "b", "l")}<span class="gap">${sky.speed > 0 ? (24 / sky.speed).toFixed(sky.speed >= 24 ? 1 : 0) + " h of listening per day" : "frozen"}</span></span>` +
      `<span class="k">now</span><span><b>${new Date(sky.utc * 1000).toLocaleDateString("en-GB", { day: "numeric", month: "short", timeZone: "UTC" })} ${ut(sky.utc)} UT</b><span class="gap">MLT ${num(sky.mlt.toFixed(1), 4, "span")} h</span></span>`;
    if (!day) return;
    const span = day.phase.length * day.step;
    const f = clamp((sky.utc - day.t0) / span, 0, 1);
    $("journey-bar").querySelector("i").style.width = (f * 100).toFixed(2) + "%";
    if (S.journeyFrom === undefined) S.journeyFrom = sky.utc;   // where this session joined the day
    $("journey-bar").querySelector("u").style.left = (clamp((S.journeyFrom - day.t0) / span, 0, 1) * 100).toFixed(2) + "%";
    const leftS = (day.t0 + span - sky.utc) / Math.max(1e-6, sky.speed);
    const past = day.onsets.filter((o) => o.utc <= sky.utc).length;
    $("info-journey-2").innerHTML =
      `<span class="k">left</span><span>${num(sky.speed > 0 ? Math.round(leftS / 60) + " min" : "—", 7, "b", "l")} of listening to the end of the recording</span>` +
      `<span class="k">onsets</span><span>${num(past, 2, "b", "l")} of ${day.onsets.length} in this day</span>` +
      `<span class="k">choice</span><span>day and speed arrive with the transport (T6.9)</span>`;
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
    showChord(tel.chord, tel.sky);
    env += (Math.max(tel.peakL || 0, tel.peakR || 0) - env) * 0.05;
    if (tel.autoGain) {
      const db = 20 * Math.log10(tel.autoGain);
      setHtml("out-note", `auto ${num(signed(db, 1), 4, "span")} dB`);
    }
  });

  // ---------------------------------------------------------------- render loop
  const P = (id) => WT.get(id);
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

    voicing.setEmission(emission, [P("red_level"), P("green_level"), P("violet_level")]);
    voicing.draw();
    if (tel.sky) mag.draw(tel.sky.utc);
    hmeter.draw(tel.peakL || 0, tel.peakR || 0, P("lim_ceiling"));

    // the macros' rings on the knobs they move: Breath's dot swells with the sound
    updateRings(tel.breath || 0);

    layers.draw([P("low_level"), P("red_level"), P("green_level"), P("violet_level")], [P("low_decay"), P("red_decay"), P("green_decay"), P("violet_decay")]);
    const low = tel.low && tel.low.note >= 0 ? tel.low.note : -1;
    if (low !== S.lowShown) {
      S.lowShown = low;
      $("low-note").innerHTML = low >= 0 ? `<b class="num" style="--w:3">${noteName(low)}</b>` : "";
    }
    if (P("play") || (tel.play && tel.play.voices && tel.play.voices.length)) keys.draw(tel.play ? tel.play.voices : [], low);
    {
      const peak = Math.max(tel.peakL || 0, tel.peakR || 0);
      fx.filter.draw({ type: Math.round(P("flt_type")), hz: tel.filterHz || P("flt_cutoff"), base: P("flt_cutoff"), reso: P("flt_reso"), sky: P("flt_sky") }, tel.spectrum);
      fx.drive.draw(P("drive"), peak);
      fx.phaser.draw(t, { rate: P("ph_rate"), depth: P("ph_depth"), fb: P("ph_feedback"), stages: P("ph_stages"), mix: P("ph_mix") }, tel.spectrum);
      fx.chorus.draw(t, { rate: P("ch_rate"), depth: P("ch_depth"), mix: P("ch_mix") });
      fx.delay.draw({ time: P("dly_time"), fb: P("dly_fb"), tone: P("dly_tone"), mix: P("dly_mix"), ping: P("dly_pingpong") });
      fx.reverb.draw({ size: P("rev_size"), decay: P("rev_decay"), damp: P("rev_damp"), pre: P("rev_predelay"), shimmer: P("rev_shimmer"), mix: P("rev_mix") }, tel.verb || 0);
      fx.output.draw(tel.peakL || 0, tel.peakR || 0, tel.gr || 1, P("lim_ceiling"));
      setHtml("verb-note", `${num(P("rev_decay").toFixed(1), 4, "span")} s<span class="gap">size ${num(Math.round(P("rev_size") * 100), 3, "span")} %</span>`);
    }
  }
  requestAnimationFrame(frame);

  if (!WT.isMock) WT.native("uiReady");
})();
