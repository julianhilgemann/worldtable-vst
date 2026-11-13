/* Controls bound to parameters (spec/09 §4–5, T6.7): solid knobs, segmented rows, a 12-key row,
 * switches. Every control root carries data-param="<id>" (the UI check counts them), a role
 * (slider, radiogroup, switch), aria-valuetext and a data-hint sentence for the hint line, and
 * works from the keyboard alone. Pointer interaction is the POC's: vertical drag, shift = fine,
 * wheel, double-click = default. */
(function () {
  "use strict";
  const WT = window.WT;
  const SVGNS = "http://www.w3.org/2000/svg";

  const UI = (window.UI = { scale: 1, dpr: window.devicePixelRatio || 1 });

  // ---------- helpers ----------
  function el(tag, cls, parent) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (parent) parent.appendChild(e);
    return e;
  }
  function svg(tag, attrs, parent) {
    const e = document.createElementNS(SVGNS, tag);
    for (const k in attrs) e.setAttribute(k, attrs[k]);
    if (parent) parent.appendChild(e);
    return e;
  }
  const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
  function polar(cx, cy, r, deg) {
    const a = (deg - 90) * Math.PI / 180;
    return [cx + r * Math.cos(a), cy + r * Math.sin(a)];
  }
  function arcPath(cx, cy, r, a0, a1) {
    if (a1 < a0) [a0, a1] = [a1, a0];
    if (a1 - a0 < 0.01) a1 = a0 + 0.01;
    const [x0, y0] = polar(cx, cy, r, a0);
    const [x1, y1] = polar(cx, cy, r, a1);
    return `M${x0.toFixed(2)},${y0.toFixed(2)} A${r},${r} 0 ${a1 - a0 > 180 ? 1 : 0} 1 ${x1.toFixed(2)},${y1.toFixed(2)}`;
  }
  function defaultValue(id) {
    const m = WT.META[id];
    return m.type === "bool" ? (m.default ? 1 : 0) : m.default;
  }
  const rangeOf = (id) => {
    const m = WT.META[id];
    if (m.type === "choice") return [0, m.choices.length - 1];
    if (m.type === "bool") return [0, 1];
    return [m.min, m.max];
  };

  // ---------- value text ----------
  const signedPct = (v) => { const p = Math.round((v - 0.5) * 200); return p === 0 ? "neutral" : (p > 0 ? "+" : "−") + Math.abs(p) + " %"; };
  const FORMAT = {
    macro_storm: signedPct, macro_glow: signedPct, macro_space: signedPct, macro_drift: signedPct,
    macro_breath: (v) => (v < 0.005 ? "off" : Math.round(v * 100) + " %"),
    progression: (v) => (v < 0.005 ? "hold" : v > 0.995 ? "follow sky" : Math.round(v * 100) + " %"),
    ph_stages: (v) => v + " st.",
    lim_ceiling: (v) => v.toFixed(1) + " dB",
    master: (v) => (v <= -59.9 ? "−∞" : v.toFixed(1) + " dB"),
    eq4_freq: (v) => (v >= 19980 ? "open" : WT.fmt("eq4_freq", v)),   // the engine opens the cut from 19.98 kHz (Eq::isOpen)
    eq_sky: (v) => (v < 0.005 ? "still" : "+" + (3 * v).toFixed(1) + " oct"),
    play_velocity: (v) => (v < 0.005 ? "fixed" : Math.round(v * 30) + " dB"),
    play_drone: (v) => (v < 0.005 ? "off" : Math.round(v * 100) + " %"),
  };
  for (const id of ["eq1_drive", "eq2_drive", "eq3_drive", "eq4_drive"]) FORMAT[id] = (v) => (v < 0.005 ? "off" : Math.round(v * 100) + " %");
  const NOTE_NAMES = ["C", "D♭", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"];
  const noteName = (n) => NOTE_NAMES[((n % 12) + 12) % 12] + (Math.floor(n / 12) - 1);
  for (const id of ["band_floor", "band_x1", "band_x2", "band_x3", "band_ceiling"]) FORMAT[id] = (v) => noteName(Math.round(v));
  FORMAT.band_blend = (v) => v.toFixed(1) + " st";
  UI.format = (id, v) => (FORMAT[id] ? FORMAT[id](v) : WT.fmt(id, v).replace("-", "−").replace(/(\d)%$/, "$1 %"));

  /** A typed value (Enter on a knob) → the parameter's value, or null. Reads what the knob shows:
   *  percent for 0…1 values, kHz and ms where the readout uses them, and the words the readouts use. */
  const NEUTRAL = { neutral: 0.5 };
  const WORDS = {
    macro_storm: NEUTRAL, macro_glow: NEUTRAL, macro_space: NEUTRAL, macro_drift: NEUTRAL, macro_breath: { off: 0 },
    progression: { hold: 0, "follow sky": 1, follow: 1 }, play_velocity: { fixed: 0 }, play_drone: { off: 0 },
    eq4_freq: { open: 20000 }, eq_sky: { still: 0 }, master: { "-∞": -60, "∞": -60, "-inf": -60, inf: -60 },
  };
  for (const id of ["eq1_drive", "eq2_drive", "eq3_drive", "eq4_drive"]) WORDS[id] = { off: 0 };
  const signedFrom = (n) => 0.5 + n / 200;
  const PARSE = {
    macro_storm: signedFrom, macro_glow: signedFrom, macro_space: signedFrom, macro_drift: signedFrom,
    play_velocity: (n) => n / 30, eq_sky: (n) => n / 3,
  };
  UI.parse = function (id, text) {
    const m = WT.META[id];
    const t = String(text).trim().toLowerCase().replace(/−/g, "-").replace(",", ".");
    if (WORDS[id] && t in WORDS[id]) return WORDS[id][t];
    const note = /^([a-g])\s*([#♯b♭]?)\s*(-?\d)$/.exec(t);   // a note name: C2, F#3, Bb1
    if (note && id.startsWith("band_") && id !== "band_blend")
      return 12 * (+note[3] + 1) + [0, 2, 4, 5, 7, 9, 11]["cdefgab".indexOf(note[1])] + (/[#♯]/.test(note[2]) ? 1 : note[2] ? -1 : 0);
    let n = parseFloat(t.replace(/^\+/, ""));
    if (!isFinite(n)) return null;
    if (PARSE[id]) return PARSE[id](n);
    const u = m.unit || "";
    if (u === "Hz" && /\dk|khz/.test(t.replace(/\s/g, ""))) n *= 1000;
    else if (u === "s" && /ms$/.test(t)) n /= 1000;
    else if (u === "ms" && /\ds$|\ss$/.test(t) && !/ms$/.test(t)) n *= 1000;
    else if (!u && m.type === "float" && ((m.min === 0 && m.max === 1) || m.min < 0)) n /= 100;   // shown as percent
    return n;
  };

  // ---------- hints (spec/09 §2 rule 5, §5): what each control does to the sound, ≤ 90 characters
  UI.HINT = {
    key: "The key the sky's chords are in.",
    mood: "Which chords the sky draws from: bright, bittersweet or nocturnal.",
    scale: "The scale the sparkles will use. It does not change the chords.",
    chord_shape: "Which of the chord's shapes holds while Progression is at 0.",
    progression: "From holding one chord to following every move of the sky.",
    transpose: "Shifts every chord and played note by semitones.",
    detune: "A few cents between neighbouring resonators, for thickness.",
    drift: "Slow wander of each resonator's pitch, like worn tape.",
    play: "Drone: the sky plays, MIDI holds a chord. Play: your keyboard plays the resonators.",
    play_attack: "How fast a played note swells in.",
    play_release: "How long a played note rings on after you let go.",
    play_velocity: "How much louder a harder note plays. At 0 every note is the same.",
    play_drone: "How much of the sky's drone plays on under your notes.",
    low_level: "Level of the low resonator, on the chord's root under the bands.",
    red_level: "Level of the red band, the low notes of the chord.",
    green_level: "Level of the green band, the body of the chord.",
    violet_level: "Level of the violet band, the high notes.",
    low_decay: "How long the low resonator rings on.",
    red_decay: "How long the red band rings on after the sky strikes it.",
    green_decay: "How long the green band rings on after the sky strikes it.",
    violet_decay: "How long the violet band rings on after the sky strikes it.",
    width: "How wide the curtain is spread between the speakers.",
    band_floor: "The low band's lowest note. The low resonator sits within an octave above it.",
    band_x1: "Where the low band ends and the red band begins.",
    band_x2: "Where the red band ends and the green band begins.",
    band_x3: "Where the green band ends and the violet band begins.",
    band_ceiling: "The first note above the violet band.",
    band_blend: "How far neighbouring bands reach into each other, in semitones.",
    red_tones: "Which chord tones the red band rings: root, root and fifth, chord, colours, all.",
    green_tones: "Which chord tones the green band rings: root, root and fifth, chord, colours, all.",
    violet_tones: "Which chord tones the violet band rings: root, root and fifth, chord, colours, all.",
    red_shift: "Transposes the red band against the chord. +7 rings a fifth above.",
    green_shift: "Transposes the green band against the chord. +7 rings a fifth above.",
    violet_shift: "Transposes the violet band against the chord. +7 rings a fifth above.",
    macro_storm: "Storm: how hard the sky plays every band.",
    macro_breath: "Breath: a slow swell, five times a minute.",
    macro_glow: "Glow: more violet and more shimmer.",
    macro_space: "Space: more or less room around the sound.",
    macro_drift: "Drift: towards worn tape, the pitch wanders further.",
    eq1_freq: "Where the low shelf turns: everything below it is lifted or lowered.",
    eq1_gain: "Lifts or lowers everything below the low shelf's corner.",
    eq1_drive: "Saturates only the lows: warmer, then thicker. Quiet parts stay clean.",
    eq2_freq: "The centre of the lower bell.",
    eq2_gain: "Lifts or lowers the sound around the lower bell's centre.",
    eq2_q: "The lower bell's width: low is broad, high is narrow.",
    eq2_drive: "Saturates only what the lower bell holds.",
    eq3_freq: "The centre of the upper bell.",
    eq3_gain: "Lifts or lowers the sound around the upper bell's centre.",
    eq3_q: "The upper bell's width: low is broad, high is narrow.",
    eq3_drive: "Saturates only what the upper bell holds.",
    eq4_freq: "Cuts the highs above this, 12 dB per octave. At the top it is open.",
    eq4_drive: "Saturates only the octave just under the cut.",
    eq_sky: "How far the sky's activity opens the cut, up to three octaves.",
    ph_rate: "How fast the phaser's notches travel.",
    ph_depth: "How far the phaser's notches sweep.",
    ph_feedback: "Feedback: sharper, more resonant notches.",
    ph_stages: "Stages: two per notch. More stages, more notches.",
    ph_mix: "How much of the phased sound you hear.",
    ch_rate: "How fast the three chorus voices swing.",
    ch_depth: "How far the three chorus voices swing.",
    ch_mix: "How much of the chorus you hear against the dry sound.",
    dly_time: "Time between the echoes.",
    dly_fb: "Feedback: from a single echo to a long trail.",
    dly_tone: "Darker or brighter echoes.",
    dly_pingpong: "Echoes alternate left and right.",
    dly_mix: "How loud the echoes are against the dry sound.",
    rev_size: "Size of the room.",
    rev_decay: "How long the reverb's tail rings on.",
    rev_damp: "Damping: the tail darkens as it fades.",
    rev_predelay: "A short gap before the tail starts, to keep the attack clear.",
    rev_shimmer: "Octave and fifth above, fed back into the tail.",
    rev_mod: "Slow movement inside the tail, against metallic ringing.",
    rev_mix: "How much reverb you hear against the dry sound.",
    lim_ceiling: "The limiter's ceiling: the output never goes above it.",
    lim_release: "How fast the limiter lets go after a peak.",
    master: "Output level, after the limiter.",
  };
  UI.hintOf = (id) => UI.HINT[id] || WT.META[id].name;

  /** One host gesture at a time per parameter (JUCE asserts on a nested begin): a run of discrete
   *  changes (keys, wheel) is one gesture, ended 400 ms after the last; anything else that starts a
   *  gesture (a drag, a click, a typed value) ends a pending run first. */
  const gestures = {};
  function gesture(id) {
    if (gestures[id]) return gestures[id];
    let timer = null;
    const flush = () => { if (timer) { clearTimeout(timer); timer = null; WT.end(id); } };
    return (gestures[id] = {
      step(fn) {
        if (timer) clearTimeout(timer); else WT.begin(id);
        fn();
        timer = setTimeout(() => { timer = null; WT.end(id); }, 400);
      },
      flush,
      commit(v) { flush(); WT.commit(id, v); },
    });
  }
  UI.commit = (id, v) => gesture(id).commit(v);

  // ---------- drag (vertical, shift = fine) and wheel ----------
  function verticalDrag(target, id, opts = {}) {
    const sens = opts.sensitivity || 220;
    target.addEventListener("pointerdown", (e) => {
      if (e.button !== 0 || e.target.closest("input")) return;
      e.preventDefault();
      target.focus({ preventScroll: true });
      target.setPointerCapture(e.pointerId);
      const startY = e.clientY;
      const startN = WT.toNorm(id, WT.get(id));
      gesture(id).flush();
      WT.begin(id);
      target.classList.add("active");
      const move = (ev) => {
        const fine = ev.shiftKey ? 0.15 : 1;
        WT.set(id, WT.fromNorm(id, startN + (startY - ev.clientY) / (sens * UI.scale) * fine));
      };
      const up = () => {
        target.releasePointerCapture(e.pointerId);
        target.removeEventListener("pointermove", move);
        target.removeEventListener("pointerup", up);
        target.removeEventListener("pointercancel", up);
        target.classList.remove("active");
        WT.end(id);
      };
      target.addEventListener("pointermove", move);
      target.addEventListener("pointerup", up);
      target.addEventListener("pointercancel", up);
    });
    target.addEventListener("dblclick", (e) => { if (!e.target.closest("input")) gesture(id).commit(defaultValue(id)); });

    target.addEventListener("wheel", (e) => {
      e.preventDefault();
      gesture(id).step(() => {
        if (WT.META[id].type !== "float") WT.set(id, WT.get(id) + (e.deltaY < 0 ? 1 : -1));
        else WT.set(id, WT.fromNorm(id, WT.toNorm(id, WT.get(id)) - e.deltaY * (e.shiftKey ? 0.0002 : 0.0015)));
      });
    }, { passive: false });
  }

  /** Arrow keys on a continuous parameter: 2 % of the range (Shift 0.4 %), at least one step for
   *  ints; PageUp/PageDown 10 %; Home/End; Backspace or Delete = default. Returns true if handled. */
  function sliderKey(id, e) {
    const g = gesture(id);
    const m = WT.META[id];
    const k = e.key;
    let dir = 0, big = false;
    if (k === "ArrowUp" || k === "ArrowRight") dir = 1;
    else if (k === "ArrowDown" || k === "ArrowLeft") dir = -1;
    else if (k === "PageUp") { dir = 1; big = true; } else if (k === "PageDown") { dir = -1; big = true; }
    const [lo, hi] = rangeOf(id);
    if (dir) {
      g.step(() => {
        if (m.type !== "float") WT.set(id, WT.get(id) + dir * (big ? Math.max(1, Math.round((hi - lo) / 10)) : 1));
        else WT.set(id, WT.fromNorm(id, WT.toNorm(id, WT.get(id)) + dir * (big ? 0.1 : e.shiftKey ? 0.004 : 0.02)));
      });
      return true;
    }
    if (k === "Home") { g.commit(lo); return true; }
    if (k === "End") { g.commit(hi); return true; }
    if (k === "Backspace" || k === "Delete") { g.commit(defaultValue(id)); return true; }
    return false;
  }

  /** Arrow keys in a radiogroup (the ARIA pattern): Right/Down next, Left/Up previous, wrapping;
   *  Home/End; Backspace or Delete = default. Returns true if handled. */
  function choiceKey(id, e, count) {
    const k = e.key, v = Math.round(WT.get(id));
    const to = k === "ArrowRight" || k === "ArrowDown" ? (v + 1) % count
      : k === "ArrowLeft" || k === "ArrowUp" ? (v + count - 1) % count
      : k === "Home" ? 0 : k === "End" ? count - 1
      : k === "Backspace" || k === "Delete" ? defaultValue(id) : null;
    if (to === null) return false;
    if (to !== v) gesture(id).commit(to);
    return true;
  }

  // ---------- Knob v2: a solid disc, a value arc outside it, a modulation ring outside that ----------
  const SIZES = { sm: 28, macro: 50 };   // macros at 50 px while they fill the centre block (T6.8; T4.2: 40)
  /** The disc's fill, shared by every knob. */
  function ensureDefs() {
    if (document.getElementById("kg")) return;
    const s = svg("svg", { width: 0, height: 0, style: "position:absolute", "aria-hidden": "true" }, document.body);
    const g = svg("radialGradient", { id: "kg", cx: "50%", cy: "32%", r: "75%" }, svg("defs", {}, s));
    svg("stop", { offset: "0", "stop-color": "#232c40" }, g);
    svg("stop", { offset: "1", "stop-color": "#111724" }, g);
  }

  function Knob(host, id, label, opts = {}) {
    const m = WT.META[id];
    if (!m) { console.warn("unknown param", id); return null; }
    ensureDefs();
    const size = opts.size === "macro" ? "macro" : "sm";
    const d = SIZES[size], R = d / 2, W = d + 20, c = W / 2;
    const BAND = ["red", "green", "violet"].includes(opts.color) ? opts.color : "ink";
    const root = el("div", `knob ${size} k-${BAND}`, host);
    root.dataset.param = id;
    root.dataset.hint = opts.hint || UI.hintOf(id);
    root.tabIndex = 0;
    root.setAttribute("role", "slider");
    root.setAttribute("aria-label", m.name);
    const [lo, hi] = rangeOf(id);
    root.setAttribute("aria-valuemin", String(lo));
    root.setAttribute("aria-valuemax", String(hi));

    const s = svg("svg", { width: W, height: W, viewBox: `0 0 ${W} ${W}`, "aria-hidden": "true" }, root);
    const A = (n) => -135 + 270 * clamp(n, 0, 1);
    const pol = (r, deg) => polar(c, c, r, deg);
    const arc = (r, a0, a1) => arcPath(c, c, r, a0, a1);
    // bipolar values draw the arc from the centre (spec/09 §4): the signed macros and ± ranges
    const bipolar = id.startsWith("macro_") ? id !== "macro_breath" : (m.type === "float" || m.type === "int") && m.min < 0 && m.max > 0;
    const from = bipolar ? A(id.startsWith("macro_") ? 0.5 : WT.toNorm(id, 0)) : -135;

    svg("path", { d: arc(R + 3.5, -135, 135), class: "track", "stroke-width": 2.5, fill: "none", "stroke-linecap": "round" }, s);
    const valArc = svg("path", { class: "arc", "stroke-width": 2.5, fill: "none", "stroke-linecap": "round" }, s);
    const ring = svg("g", { class: "ring", display: "none" }, s);
    const ringArc = svg("path", { class: "ring-arc", "stroke-width": 1.5, fill: "none", "stroke-linecap": "round" }, ring);
    const ringDot = svg("circle", { class: "ring-dot", r: size === "macro" ? 3 : 2.7 }, ring);
    svg("circle", { cx: c, cy: c, r: R - 1.5, class: "disc" }, s);
    const ptr = svg("line", { class: "ptr", "stroke-width": 2, "stroke-linecap": "round" }, s);

    const lab = el("div", "label", root);
    lab.textContent = label;
    const val = el("div", "value", root);
    const valText = el("span", "", val);

    WT.onParam(id, (v) => {
      const a = A(WT.toNorm(id, v));
      valArc.setAttribute("d", arc(R + 3.5, from, a));
      const [x0, y0] = pol(R * 0.32, a), [x1, y1] = pol(R * 0.8, a);
      ptr.setAttribute("x1", x0.toFixed(2)); ptr.setAttribute("y1", y0.toFixed(2));
      ptr.setAttribute("x2", x1.toFixed(2)); ptr.setAttribute("y2", y1.toFixed(2));
      const text = UI.format(id, v);   // the value you set; the ring's dot is the value now
      valText.textContent = text;
      root.setAttribute("aria-valuenow", String(v));
      root.setAttribute("aria-valuetext", text);
    });

    verticalDrag(root, id);

    // ---- typing a value: Enter opens a small field over the readout; Enter sets, Escape cancels
    let field = null;
    function closeField(commit) {
      if (!field) return;
      const f = field;
      field = null;
      if (commit) {
        const v = UI.parse(id, f.value);
        if (v !== null && isFinite(v)) gesture(id).commit(v);
      }
      f.remove();
      root.classList.remove("editing");
      root.focus({ preventScroll: true });
    }
    function openField() {
      if (field) return;
      field = el("input", "knob-field", val);
      field.type = "text";
      field.value = valText.textContent;
      field.setAttribute("aria-label", m.name + ", type a value");
      field.spellcheck = false;
      root.classList.add("editing");
      field.addEventListener("keydown", (e) => {
        e.stopPropagation();
        if (e.key === "Enter") { e.preventDefault(); closeField(true); }
        else if (e.key === "Escape") { e.preventDefault(); closeField(false); }
      });
      field.addEventListener("blur", () => closeField(false));
      field.addEventListener("pointerdown", (e) => e.stopPropagation());
      field.focus({ preventScroll: true });
      field.select();
    }

    root.addEventListener("keydown", (e) => {
      if (e.target !== root) return;
      if (e.key === "Enter") { e.preventDefault(); openField(); return; }
      if (sliderKey(id, e)) e.preventDefault();
    });

    // ---- the modulation ring (spec/09 §2 rule 3): fed by the macros now, by T4.2's grid later
    let mod = null, modSig = "";
    const knob = {
      root,
      /** The ring: lo and hi are the range the sources can push this parameter, now is where they
       *  have it; all three in the parameter's own units (as WT.get returns). */
      setMod(loV, hiV, nowV) {
        const n = [loV, hiV, nowV].map((x) => WT.toNorm(id, clamp(x, lo, hi)));
        const sig = n.map((x) => x.toFixed(4)).join();
        mod = { lo: loV, hi: hiV, now: nowV };
        if (sig === modSig) return;
        modSig = sig;
        ring.removeAttribute("display");
        ringArc.setAttribute("d", arc(R + 8, A(n[0]), A(n[1])));
        const [x, y] = pol(R + 8, A(n[2]));
        ringDot.setAttribute("cx", x.toFixed(2));
        ringDot.setAttribute("cy", y.toFixed(2));
      },
      clearMod() { mod = null; modSig = ""; ring.setAttribute("display", "none"); },
      get mod() { return mod; },
      /** Lit while a source that moves this knob is pointed at or focused. */
      setLit(on) { root.classList.toggle("lit", !!on); },
    };
    return knob;
  }

  // ---------- Seg: a row of choices, a radiogroup ----------
  function Seg(host, id, labels, opts = {}) {
    const m = WT.META[id];
    const root = el("div", "seg" + (opts.cls ? " " + opts.cls : ""), host);
    root.dataset.param = id;
    root.dataset.hint = UI.hintOf(id);
    root.setAttribute("role", "radiogroup");
    root.setAttribute("aria-label", m.name);
    const names = labels || m.choices;
    const said = m.choices || names;   // a bool as a pair of choices (Drone | Play) says its labels
    const buttons = names.map((name, i) => {
      const b = el("button", opts.classOf ? opts.classOf(i) : "", root);
      b.type = "button";
      b.setAttribute("role", "radio");
      b.textContent = name;
      b.setAttribute("aria-label", said[i]);
      b.addEventListener("click", () => gesture(id).commit(i));
      return b;
    });
    root.addEventListener("keydown", (e) => {
      if (!choiceKey(id, e, buttons.length)) return;
      e.preventDefault();
      buttons[Math.round(WT.get(id))].focus({ preventScroll: true });
    });
    WT.onParam(id, (v) => {
      const k = Math.round(v);
      buttons.forEach((b, i) => {
        b.classList.toggle("on", i === k);
        b.setAttribute("aria-checked", String(i === k));
        b.tabIndex = i === k ? 0 : -1;
      });
      root.setAttribute("aria-valuetext", said[k] || "");
    });
    return root;
  }

  /** The key as twelve pitch classes, spelt like the chord names (flats, F♯). */
  const KEY_NAMES = ["C", "D♭", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B"];
  const ACCIDENTAL = [1, 3, 6, 8, 10];
  function Keys(host, id) {
    return Seg(host, id, KEY_NAMES, { cls: "keys", classOf: (i) => (ACCIDENTAL.includes(i) ? "acc" : "") });
  }

  // ---------- switches: a chip (Toggle) ----------
  function bindSwitch(btn, id, label) {
    btn.dataset.param = id;
    btn.dataset.hint = UI.hintOf(id);
    btn.setAttribute("role", "switch");
    btn.setAttribute("aria-label", label || WT.META[id].name);
    btn.removeAttribute("aria-pressed");
    WT.onParam(id, (v) => {
      btn.classList.toggle("on", !!v);
      btn.setAttribute("aria-checked", String(!!v));
      btn.setAttribute("aria-valuetext", v ? "on" : "off");
    });
    btn.addEventListener("click", (e) => { e.stopPropagation(); gesture(id).commit(WT.get(id) ? 0 : 1); });
    btn.addEventListener("keydown", (e) => {
      // Space and Enter click the button natively; arrows set it, Home/End too
      const k = e.key;
      const to = k === "ArrowRight" || k === "ArrowUp" || k === "End" ? 1 : k === "ArrowLeft" || k === "ArrowDown" || k === "Home" ? 0
        : k === "Backspace" || k === "Delete" ? defaultValue(id) : null;
      if (to === null) return;
      e.preventDefault();
      if (to !== WT.get(id)) gesture(id).commit(to);
    });
    return btn;
  }
  function Toggle(host, id, label) {
    const b = el("button", "chip", host);
    b.type = "button";
    b.textContent = label;
    return bindSwitch(b, id, label);
  }

  // ---------- canvas ----------
  /** Sizes a canvas backing store for the stage zoom and the display; returns {ctx, w, h} in CSS px.
   *  Capped (T6.6): a canvas whose CSS size follows its own backing store would otherwise grow by
   *  the device-pixel ratio on every redraw (the T3.7 filter picture did, on Retina). */
  function fit(canvas) {
    const w = Math.min(canvas.clientWidth, 2048), h = Math.min(canvas.clientHeight, 2048);
    const k = Math.min(3, UI.dpr * UI.scale);
    const bw = Math.max(1, Math.round(w * k)), bh = Math.max(1, Math.round(h * k));
    if (canvas.width !== bw || canvas.height !== bh) { canvas.width = bw; canvas.height = bh; }
    const ctx = canvas.getContext("2d");
    ctx.setTransform(k, 0, 0, k, 0, 0);
    return { ctx, w, h, k };
  }

  /** The palette (spec/09 §4): ink for everything a person sets, the three lines only for the bands. */
  const C = {
    night: "#05070d", well: "#070a12", ink: "#c9d3e6", inkHi: "#eef2fa", arc: "#dfe6f3", muted: "#8a96ad", faint: "#6e7991",
    green: "#5cf2a0", red: "#ff6b6b", violet: "#8f7bff",
    hair: "rgba(201,211,230,0.12)", hairSoft: "rgba(201,211,230,0.07)", hairStrong: "rgba(201,211,230,0.22)", track: "#141a28",
    rgb: { green: [92, 242, 160], red: [255, 107, 107], violet: [143, 123, 255], ink: [201, 211, 230], hi: [238, 242, 250] },
  };
  const rgba = (name, a) => { const c = C.rgb[name]; return `rgba(${c[0]},${c[1]},${c[2]},${a})`; };
  const MONO = (px) => `${px}px "JetBrains Mono", ui-monospace, monospace`;
  const SANS = (px, w) => `${w || 400} ${px}px "Instrument Sans", -apple-system, sans-serif`;

  Object.assign(UI, { Knob, Seg, Keys, Toggle, fit, el, svg, C, rgba, MONO, SANS, KEY_NAMES, defaultValue, clamp });
})();
