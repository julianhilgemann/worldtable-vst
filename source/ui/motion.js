/* The motion panels (T4.2, the mockup's #motion, drawLfo, drawEnv): the two LFOs and the envelope,
 * sources of the modulation grid beside the macros.
 *
 * LFO: a 1 | 2 switch picks the LFO shown (each LFO's shape and rate are a set in the panel's grid;
 * the other set is hidden). The wave scrolls under a fixed "now" marker, a dot at the value; what has
 * passed is bright and filled, what still comes dimmer. Drag the picture sideways for the rate.
 * Envelope: the trigger chip, Fire, the shape with three points to drag (attack; decay and sustain;
 * release), the timed hold shaded, a dot running along it while it runs, the four values underneath.
 * Each picture says in its top-left corner what its source moves. Pointing at a panel lights the knobs
 * its source moves (app.js sourceHover). Telemetry `mod.lfo` = [[phase, value, knots…]], `mod.env` =
 * [stage, progress, value] (mod.js). Everything drawn eases or follows the engine's own smooth values. */
(function () {
  "use strict";
  const WT = window.WT, UI = window.UI, C = UI.C;
  const $ = (id) => document.getElementById(id);
  const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
  const lerp = (a, b, t) => a + (b - a) * t;
  const P = (id) => WT.get(id);
  if (!$("lfo") || !WT.META.lfo1_rate) return;   // the engine's parameters are not there yet

  /** Clips a text to `max` px with an ellipsis. */
  const clip = (c, text, max) => {
    if (c.measureText(text).width <= max) return text;
    while (text.length > 4 && c.measureText(text + "…").width > max) text = text.slice(0, -1);
    return text + "…";
  };
  const localXY = (e, cv) => { const r = cv.getBoundingClientRect(), k = r.width / Math.max(1, cv.clientWidth); return { x: (e.clientX - r.left) / k, y: (e.clientY - r.top) / k }; };
  const setHint = (text) => { const n = $("hint"); if (n && n.textContent !== text) n.textContent = text; };

  // ================================================================ the LFO
  let shown = 0;   // 0: LFO 1, 1: LFO 2
  const lfoKey = () => "lfo" + (shown + 1);
  const rateId = () => `lfo${shown + 1}_rate`, shapeId = () => `lfo${shown + 1}_shape`;
  const ICON = ["M2 8 C4.5 0.5, 7.5 0.5, 10 8 S15.5 15.5, 18 8", "M2 12 L7 3 L13 13 L18 4", "M2 9 C5 3, 7 13, 10 8 S14 3, 18 10", "M2 11 H6 V4 H10 V13 H14 V7 H18"];
  for (const seg of document.querySelectorAll(".lfo-shape .seg, .lfo-shape.seg")) {
    seg.classList.add("icos");
    seg.querySelectorAll("button").forEach((b, i) => {
      b.innerHTML = `<svg width="20" height="16" viewBox="0 0 20 16" fill="none" aria-hidden="true"><path d="${ICON[i]}" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/></svg>`;
    });
  }
  const lfoPanel = $("lfo"), nSeg = $("lfo-n"), nButtons = [...nSeg.querySelectorAll("button")];
  const lfoSource = UI.sourceHover(lfoPanel, lfoKey, "A slow wave that moves other controls. Drag sideways for its speed.", 2);
  function showLfo(i, focus) {
    if (lfoPanel.matches(":hover") || lfoPanel.contains(document.activeElement)) lfoSource.light(false)();
    shown = i;
    nButtons.forEach((b, k) => { b.setAttribute("aria-checked", String(k === i)); b.tabIndex = k === i ? 0 : -1; });
    $("lfo1-set").hidden = i !== 0;
    $("lfo2-set").hidden = i !== 1;
    nSeg.setAttribute("aria-valuetext", "LFO " + (i + 1));
    lfoSource.say();
    if (lfoPanel.matches(":hover") || lfoPanel.contains(document.activeElement)) lfoSource.light(true)();
    if (focus) nButtons[i].focus({ preventScroll: true });
  }
  nButtons.forEach((b, i) => b.addEventListener("click", () => showLfo(i)));
  nSeg.addEventListener("keydown", (e) => {
    const k = e.key, to = k === "ArrowRight" || k === "ArrowDown" || k === "End" ? 1 : k === "ArrowLeft" || k === "ArrowUp" || k === "Home" ? 0 : null;
    if (to === null) return;
    e.preventDefault();
    showLfo(to, true);
  });
  showLfo(0);

  // drag the picture sideways for the rate: 110 px for the whole range, on a log scale (the mockup's)
  const lfoCv = $("cv-lfo");
  const LO = Math.log(0.01), HI = Math.log(2);
  UI.dragPicture(lfoCv, (p) => {
    const id = rateId(), n0 = (Math.log(P(id)) - LO) / (HI - LO);
    return { ids: [id], move: (q, ev) => WT.set(id, Math.exp(LO + (HI - LO) * clamp(n0 + (q.x - p.x) / 110 * (ev.shiftKey ? 0.2 : 1), 0, 1))) };
  });

  /** The shapes as SlowLfo.h has them; `knot(n)` the seeded knot n cycles from now's (−2 … 2), or null. */
  function lfoValue(shape, c, knot) {
    const i = Math.floor(c), p = c - i;
    if (shape === 0) return Math.sin(2 * Math.PI * p);
    if (shape === 1) return 1 - 4 * Math.abs(p - 0.5);
    const a = knot(i), b = knot(i + 1);
    if (a === null) return null;
    if (shape === 2) return b === null ? null : a + (b - a) * p * p * (3 - 2 * p);
    return a;
  }
  // without the knots (the contract carries only [phase, value]) a random shape's past is what the
  // page has seen: a short history per LFO, in cycles
  const hist = [[], []];
  const lfoState = [{ cycles: 0, phase: null, at: 0 }, { cycles: 0, phase: null, at: 0 }];
  let lfoSig = "";
  function drawLfo(t) {
    const L = t && t.lfo && t.lfo[shown];
    const st = lfoState[shown], shape = Math.round(P(shapeId())), rate = P(rateId()), now = performance.now() / 1000;
    // the phase between telemetry frames runs on at the rate, so the wave scrolls smoothly
    let cyc = st.cycles + (st.phase || 0);
    if (L && st.phase !== null) cyc += clamp(now - st.at, 0, 0.1) * rate;
    const knots = L && L.length >= 7 ? L.slice(2, 7) : null;
    const base = Math.floor(st.cycles + (st.phase || 0));
    const knot = (i) => (knots && i - base >= -2 && i - base <= 2 ? knots[i - base + 2] : null);
    const { ctx: c, w, h } = UI.fit(lfoCv);
    const sig = [w, h, UI.scale, shape, cyc.toFixed(4), L ? L[1].toFixed(3) : "", UI.movesText(lfoKey(), 2)].join("|");
    if (sig === lfoSig) return;
    lfoSig = sig;
    const xn = Math.round(w * 0.62), mid = h / 2 + 5, amp = h / 2 - 17, span = 2.2;
    const cAt = (x) => cyc + (x - xn) / w * span;
    const past = hist[shown];
    const val = (x) => {
      const v = lfoValue(shape, cAt(x), knot);
      if (v !== null || x > xn) return v;
      const cx = cAt(x);   // a random shape without knots: the history
      for (let k = past.length - 1; k > 0; k--) if (past[k - 1][0] <= cx) return past[k][1];
      return null;
    };
    const Y = (x) => mid - amp * clamp(val(x), -1, 1);
    c.clearRect(0, 0, w, h);
    c.strokeStyle = "rgba(201,211,230,0.1)"; c.lineWidth = 1;
    c.beginPath(); c.moveTo(0, Math.round(mid) + 0.5); c.lineTo(w, Math.round(mid) + 0.5); c.stroke();
    const trace = (x0, x1) => { let on = false; c.beginPath(); for (let x = x0; x <= x1; x++) { const v = val(x); if (v === null) { on = false; continue; } const y = mid - amp * clamp(v, -1, 1); if (on) c.lineTo(x, y); else c.moveTo(x, y); on = true; } };
    // what has passed: filled to the middle, bright
    trace(0, xn); c.lineTo(xn, mid); c.lineTo(0, mid); c.closePath();
    const g = c.createLinearGradient(0, 0, xn, 0); g.addColorStop(0, "rgba(201,211,230,0)"); g.addColorStop(1, "rgba(201,211,230,0.22)");
    c.fillStyle = g; c.fill();
    trace(0, xn); c.strokeStyle = C.inkHi; c.lineWidth = 1.6; c.lineJoin = "round"; c.stroke();
    // what still comes, dimmer
    trace(xn, w); c.strokeStyle = "rgba(201,211,230,0.35)"; c.lineWidth = 1.2; c.stroke();
    // now: a fixed marker, the dot at the value the engine has
    c.fillStyle = "rgba(255,255,255,0.25)"; c.fillRect(xn, 20, 1, h - 26);
    const vNow = L ? L[1] : val(xn);
    if (vNow !== null && isFinite(vNow)) { c.fillStyle = "#fff"; c.beginPath(); c.arc(xn + 0.5, mid - amp * clamp(vNow, -1, 1), 3.2, 0, 2 * Math.PI); c.fill(); }
    c.fillStyle = C.muted; c.font = UI.SANS(10.5); c.textAlign = "left"; c.textBaseline = "alphabetic";
    c.fillText(clip(c, "moves " + UI.movesText(lfoKey(), 2), w - 12), 6, 13);
  }
  /** Follows the LFOs' cycles from the telemetry (a wrap of the phase is a new cycle). */
  function trackLfo(t) {
    if (!t || !t.lfo) return;
    const now = performance.now() / 1000;
    t.lfo.forEach((L, k) => {
      const st = lfoState[k];
      if (!st || !L) return;
      if (st.phase !== null && L[0] < st.phase - 0.5) st.cycles += 1;
      st.phase = L[0];
      st.at = now;
      const hh = hist[k];
      hh.push([st.cycles + L[0], L[1]]);
      if (hh.length > 900) hh.splice(0, hh.length - 900);
    });
  }

  // ================================================================ the envelope
  const envPanel = $("env"), envCv = $("cv-env"), envWell = $("env-well");
  UI.Chip($("env-trig"), "env_trigger", { label: "on", cycle: [0, 1, 2] });
  $("env-fire").addEventListener("click", (e) => { e.stopPropagation(); WT.native("envFire"); });
  UI.sourceHover(envPanel, () => "env", "A shape that runs once each time it fires.", 2);
  const T0 = 0.01, TN = Math.log(1000);   // 10 ms … 10 s
  const tNorm = (s) => clamp(Math.log(s / T0) / TN, 0, 1), tFrom = (n) => T0 * Math.exp(TN * clamp(n, 0, 1));
  const S = () => P("env_sustain");
  /** The mockup's envShape: the stage `seg` (0 attack … 3 release) at progress `f`. */
  const envShape = (seg, f) => (seg === 0 ? f : seg === 1 ? 1 - (1 - S()) * (1 - (1 - f) ** 2) : seg === 2 ? S() : S() * (1 - f) ** 2);
  /** The shape's frame: the three timed stages share the width; the hold is a fixed 24 px. */
  function envG() {
    const w = envCv.clientWidth, h = envCv.clientHeight, top = 22, bot = h - 20, av = (w - 44) / 3, seg = (n) => 10 + (av - 10) * n;
    const x0 = 10, xa = x0 + seg(tNorm(P("env_attack"))), xd = xa + seg(tNorm(P("env_decay"))), xs = xd + 24, xr = xs + seg(tNorm(P("env_release")));
    return { w, h, top, bot, av, xs: [x0, xa, xd, xs, xr], ys: bot - (bot - top) * S(), norm: (px) => clamp((px - 10) / (av - 10), 0, 1) };
  }
  const POINTS = ["attack", "decay", "release"];
  const pts = (g) => [[g.xs[1], g.top], [g.xs[2], g.ys], [g.xs[4], g.bot]];
  // the four times' proxies on the three points (spec/09 §5): decay and sustain share the middle one
  const proxies = {};
  [["env_attack", 0, "Envelope attack"], ["env_decay", 1, "Envelope decay"], ["env_sustain", 1, "Envelope sustain"], ["env_release", 2, "Envelope release"]]
    .forEach(([id, k, label]) => { proxies[id] = { el: UI.Proxy(envWell, id, { cls: "env-pt", label }), k }; });
  let placed = "";
  function placeProxies(g) {
    const p = pts(g), s = p.map((q) => q[0].toFixed(1) + "," + q[1].toFixed(1)).join(" ");
    if (s === placed) return;
    placed = s;
    for (const { el, k } of Object.values(proxies)) { el.style.left = p[k][0].toFixed(1) + "px"; el.style.top = p[k][1].toFixed(1) + "px"; }
  }
  const nearest = (q) => { const g = envG(); let best = -1, bd = 12; pts(g).forEach((p, i) => { const d = Math.hypot(q.x - p[0], q.y - p[1]); if (d < bd) { bd = d; best = i; } }); return best; };
  let hot = -1;
  const ENV_HINT = ["Attack: drag sideways for how fast it rises.", "Decay and sustain: sideways for how fast it falls, up and down for the level it holds.",
    "Release: drag sideways for how long it takes to fade."];
  envCv.addEventListener("pointermove", (e) => {
    const i = nearest(localXY(e, envCv));
    if (i !== hot) { hot = i; envSig = ""; }
    envCv.style.cursor = hot >= 0 ? "grab" : "default";
    setHint(ENV_HINT[hot] || "A shape that runs once each time it fires. Drag its three points.");
  });
  envCv.addEventListener("pointerleave", () => { hot = -1; envSig = ""; });
  UI.dragPicture(envCv, (p) => {
    const i = nearest(p);
    if (i < 0) return null;
    const g = envG();
    const ids = i === 0 ? ["env_attack"] : i === 1 ? ["env_decay", "env_sustain"] : ["env_release"];
    proxies[ids[0]].el.focus({ preventScroll: true });
    envCv.style.cursor = "grabbing";
    return {
      ids,
      move(q) {
        if (i === 0) WT.set("env_attack", tFrom(g.norm(q.x - g.xs[0])));
        else if (i === 1) { WT.set("env_decay", tFrom(g.norm(q.x - g.xs[1]))); WT.set("env_sustain", clamp((g.bot - q.y) / (g.bot - g.top), 0, 1)); }
        else WT.set("env_release", tFrom(g.norm(q.x - g.xs[3])));
      },
      end() { envCv.style.cursor = hot >= 0 ? "grab" : "default"; },
    };
  });
  envCv.addEventListener("dblclick", (e) => {
    const i = nearest(localXY(e, envCv));
    if (i === 0) UI.commit("env_attack", UI.defaultValue("env_attack"));
    else if (i === 1) { UI.commit("env_decay", UI.defaultValue("env_decay")); UI.commit("env_sustain", UI.defaultValue("env_sustain")); }
    else if (i === 2) UI.commit("env_release", UI.defaultValue("env_release"));
  });

  const fsec = (x) => (x < 1 ? Math.round(x * 1000) + "ms" : x.toFixed(1) + "s");
  let envSig = "";
  function drawEnv(t) {
    const E = t && t.env, g = envG();
    placeProxies(g);
    const ids = ["env_attack", "env_decay", "env_sustain", "env_release"];
    const focused = Object.values(proxies).findIndex((p) => p.el === document.activeElement);
    const sig = [g.w, g.h, UI.scale, ids.map(P).join(), E ? E.map((x) => x.toFixed(3)).join() : "", hot, focused, UI.movesText("env", 2)].join("|");
    if (sig === envSig) return;
    envSig = sig;
    const { ctx: c, w, h } = UI.fit(envCv), X = g.xs, Yv = (v) => g.bot - (g.bot - g.top) * v;
    c.clearRect(0, 0, w, h);
    c.strokeStyle = "rgba(201,211,230,0.12)"; c.lineWidth = 1;
    c.beginPath(); c.moveTo(0, g.bot + 0.5); c.lineTo(w, g.bot + 0.5); c.stroke();
    const trace = () => { c.moveTo(X[0], g.bot); for (let s = 0; s < 4; s++) for (let k = 1; k <= 16; k++) c.lineTo(lerp(X[s], X[s + 1], k / 16), Yv(envShape(s, k / 16))); };
    c.beginPath(); trace(); c.lineTo(X[4], g.bot); c.closePath();
    const gr = c.createLinearGradient(0, g.top, 0, g.bot); gr.addColorStop(0, "rgba(201,211,230,0.24)"); gr.addColorStop(1, "rgba(201,211,230,0.03)");
    c.fillStyle = gr; c.fill();
    c.beginPath(); trace(); c.strokeStyle = C.inkHi; c.lineWidth = 1.6; c.lineJoin = "round"; c.stroke();
    c.fillStyle = "rgba(201,211,230,0.1)"; c.fillRect(X[2], g.top - 4, X[3] - X[2], g.bot - g.top + 4);   // while it is held
    const focusK = focused >= 0 ? Object.values(proxies)[focused].k : -1;
    pts(g).forEach((p, i) => {
      const on = i === hot || i === focusK;
      c.fillStyle = C.night; c.beginPath(); c.arc(p[0], p[1], on ? 6.5 : 5.5, 0, 2 * Math.PI); c.fill();
      c.strokeStyle = on ? "#fff" : C.inkHi; c.lineWidth = 1.8; c.stroke();
    });
    // while it runs: a dot on the shape at the engine's value, a faint line under it
    if (E && E[0] >= 1 && E[0] <= 4) {
      const seg = E[0] - 1, x = lerp(X[seg], X[seg + 1], clamp(E[1], 0, 1)), y = Yv(clamp(E[2], 0, 1));
      c.fillStyle = "rgba(255,255,255,0.25)"; c.fillRect(Math.round(x), y, 1, g.bot - y);
      c.fillStyle = "#fff"; c.beginPath(); c.arc(x, y, 3.2, 0, 2 * Math.PI); c.fill();
    }
    c.fillStyle = C.muted; c.font = UI.SANS(10.5); c.textAlign = "left"; c.textBaseline = "alphabetic";
    c.fillText(clip(c, "moves " + UI.movesText("env", 2), w - 12), 6, 13);
    c.font = UI.MONO(10);
    c.fillText(`A ${fsec(P("env_attack"))}  D ${fsec(P("env_decay"))}  S ${Math.round(S() * 100)}%  R ${fsec(P("env_release"))}`, 6, h - 6);
  }
  // a focused point is drawn lit: redraw when the focus moves between them
  envWell.addEventListener("focusin", () => { envSig = ""; });
  envWell.addEventListener("focusout", () => { envSig = ""; });

  let telSeen = null;
  UI.motion = {
    /** One frame (app.js, 30 fps): both pictures from the telemetry's `mod`. */
    draw(tel) {
      const t = UI.mod.tel;
      if (t !== telSeen) { telSeen = t; trackLfo(t); }
      drawLfo(t);
      drawEnv(t);
    },
    showLfo,
  };
  // review screenshots: #lfo=2 shows LFO 2
  const hash = new URLSearchParams(location.hash.slice(1));
  if (hash.get("lfo") === "2") showLfo(1);
})();
