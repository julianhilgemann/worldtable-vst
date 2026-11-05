/* Ambient background (spec/tasks/TP.2, simplified in T3.6).
 *   0 off · 1 CSS gradients · 2 WebGL shader (the sky's activity drives the aurora)
 * Per-viewer UI state only (no plug-in parameters). Reads window.__wtState.tel,
 * never talks to C++. The POC level switcher is gone (spec/07); the default is 2. */
(function () {
  "use strict";
  const root = document.getElementById("ambient");
  if (!root) return;

  const RES = 0.5;          // canvas pixels per CSS pixel: soft gradients need no detail
  const FPS = 30;           // the motion is slow; 30 fps is smooth and halves the GPU work
  const KEY = "wt.ambient";
  const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
  const lerp = (a, b, t) => a + (b - a) * t;
  const follow = (cur, target, dt, tau) => cur + (target - cur) * (1 - Math.exp(-dt / tau));

  // ---------- level 1: CSS blobs ----------
  const blobs = document.createElement("div");
  blobs.className = "blobs";
  for (let i = 1; i <= 4; i++) {
    const b = document.createElement("div");
    b.className = "blob b" + i;
    blobs.appendChild(b);
  }
  root.appendChild(blobs);

  // ---------- levels 2/3: WebGL ----------
  const VS = "attribute vec2 a; varying vec2 v_uv;" +
             "void main() { v_uv = a * 0.5 + 0.5; gl_Position = vec4(a, 0.0, 1.0); }";

  const FS = `
precision highp float;
varying vec2 v_uv;
uniform vec2  u_res;
uniform float u_time;    // integrated phase, seconds
uniform float u_mode;    // 0 ocean ... 1 aurora (crossfades)
uniform float u_energy;  // 0..1 swell energy / geomagnetic activity
uniform float u_scale;   // spatial scale (ocean: wave period)
uniform float u_glow;    // 0..1 output level
uniform float u_pulse;   // 0..1 note transients
uniform float u_drift;   // -1..1 LFO 1

float hash(vec2 p) { p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
float noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  vec2 u = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x),
             mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}
float fbm(vec2 p) {
  float v = 0.0, a = 0.5;
  mat2 m = mat2(1.6, 1.2, -1.2, 1.6);
  for (int i = 0; i < 5; i++) { v += a * noise(p); p = m * p; a *= 0.5; }
  return v;
}

// Swell trains rolling towards the bottom edge; t * 0.8 rad/s ≈ one crest every 8 s.
vec3 ocean(vec2 uv, float t) {
  float aspect = u_res.x / u_res.y;
  vec2 p = vec2(uv.x * aspect + u_drift * 0.15, uv.y);
  vec2 w = vec2(fbm(p * 1.3 + vec2(t * 0.025, 0.0)), fbm(p * 1.3 + vec2(3.1, 7.7 + t * 0.02))) - 0.5;
  vec2 q = p + w * 0.45;                         // bend the lines so they are never straight
  float k = u_scale * 9.0;                       // wavenumber: long period → long waves
  float h = 0.55 * sin(q.y * k + q.x * k * 0.22 + t * 0.8)
          + 0.30 * sin(q.y * k * 1.63 - q.x * k * 0.31 + t * 1.1 + 1.7)
          + 0.15 * sin(q.y * k * 2.71 + q.x * k * 0.55 + t * 1.6 + 4.1)
          + 0.50 * (fbm(q * k * 0.3 + vec2(0.0, t * 0.3)) - 0.5);
  h = h * 0.5 + 0.5;
  float crest = smoothstep(0.70, 0.97, h);
  float amt = 0.30 + 0.55 * u_energy;            // swell energy → how much the crests light up (≤ 0.85)
  vec3 col = mix(vec3(0.010, 0.026, 0.056), vec3(0.028, 0.090, 0.180), smoothstep(0.2, 0.8, h));
  col = mix(col, vec3(0.075, 0.250, 0.400), crest * amt);
  col += vec3(1.0, 0.76, 0.26) * pow(crest, 3.0) * amt * (0.06 + 0.16 * u_glow + 0.20 * u_pulse);  // glints
  float light = smoothstep(1.3, 0.1, distance(uv * vec2(aspect, 1.0), vec2(0.35 * aspect, 0.95)));
  return col * (0.5 + 0.7 * light);
}

vec3 aurora(vec2 uv, float t) {
  float aspect = u_res.x / u_res.y;
  float x = uv.x * aspect + u_drift * 0.1;
  float speed = 0.5 + 1.2 * u_energy;
  float tall = 0.10 + 0.30 * u_energy;
  vec3 col = mix(vec3(0.010, 0.020, 0.045), vec3(0.020, 0.035, 0.075), uv.y);
  for (int k = 0; k < 2; k++) {
    float fk = float(k);
    float base = 0.50 + 0.12 * fk + 0.32 * (fbm(vec2(x * 0.7 + fk * 3.1 + t * 0.02 * speed, fk + t * 0.03)) - 0.5);
    float h = uv.y - base;
    float rays = fbm(vec2(x * 9.0 + 2.0 * fbm(vec2(x * 1.5, t * 0.08 * speed)) + fk * 11.0, t * 0.12 * speed));
    // bright lower edge that fades softly below and slowly upwards, broken into rays
    float band = exp(-max(-h, 0.0) * 22.0) * exp(-max(h, 0.0) / tall) * (0.25 + 1.4 * rays * rays);
    vec3 c = mix(vec3(0.30, 0.95, 0.62), vec3(0.55, 0.30, 0.95), smoothstep(0.0, tall * 1.6, h));
    col += c * band * (0.16 + 0.30 * u_energy + 0.12 * u_glow + 0.15 * u_pulse) * (1.0 - 0.35 * fk);
  }
  col += vec3(1.0, 0.75, 0.25) * 0.025 * smoothstep(0.35, 0.0, uv.y) * (0.5 + u_glow);
  return col;
}

void main() {
  vec3 col;
  if (u_mode < 0.001)      col = ocean(v_uv, u_time);
  else if (u_mode > 0.999) col = aurora(v_uv, u_time);
  else                     col = mix(ocean(v_uv, u_time), aurora(v_uv, u_time), u_mode);
  col += (hash(gl_FragCoord.xy + fract(u_time)) - 0.5) / 255.0;   // dither: no banding in dark gradients
  gl_FragColor = vec4(col, 1.0);
}`;

  let canvas = null, gl = null, prog = null, U = {}, glFailed = false;

  function compile(type, src) {
    const s = gl.createShader(type);
    gl.shaderSource(s, src);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
      console.warn("ambient: shader compile failed", gl.getShaderInfoLog(s));
      return null;
    }
    return s;
  }

  function ensureGL() {
    if (gl) return true;
    if (glFailed) return false;
    canvas = document.createElement("canvas");
    root.appendChild(canvas);
    gl = canvas.getContext("webgl", { alpha: false, antialias: false, depth: false, stencil: false,
                                       powerPreference: "low-power", preserveDrawingBuffer: false });
    const vs = gl && compile(gl.VERTEX_SHADER, VS), fs = gl && compile(gl.FRAGMENT_SHADER, FS);
    if (vs && fs) {
      prog = gl.createProgram();
      gl.attachShader(prog, vs);
      gl.attachShader(prog, fs);
      gl.linkProgram(prog);
    }
    if (!prog || !gl.getProgramParameter(prog, gl.LINK_STATUS)) {
      glFailed = true; gl = null; canvas.remove(); canvas = null;
      return false;
    }
    gl.useProgram(prog);
    gl.bindBuffer(gl.ARRAY_BUFFER, gl.createBuffer());
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);  // one big triangle
    const a = gl.getAttribLocation(prog, "a");
    gl.enableVertexAttribArray(a);
    gl.vertexAttribPointer(a, 2, gl.FLOAT, false, 0, 0);
    for (const n of ["u_res", "u_time", "u_mode", "u_energy", "u_scale", "u_glow", "u_pulse", "u_drift"])
      U[n] = gl.getUniformLocation(prog, n);
    canvas.addEventListener("webglcontextlost", (e) => {
      e.preventDefault();
      gl = null; glFailed = true;
      setLevel(1);
    });
    resize();
    return true;
  }

  function resize() {
    if (!canvas) return;
    canvas.width = Math.max(2, Math.round(window.innerWidth * RES));
    canvas.height = Math.max(2, Math.round(window.innerHeight * RES));
    if (gl) gl.viewport(0, 0, canvas.width, canvas.height);
  }
  window.addEventListener("resize", resize);

  function targets() {
    const S = window.__wtState || {}, tel = S.tel || {};
    const sky = tel.sky || {};
    const tg = { mode: 1, energy: clamp(sky.activity ?? 0.55, 0, 1), scale: 2.0, speed: 1.0, glow: 0.3, pulse: 0, drift: 0 };
    const pk = Math.max(tel.peakL || 0, tel.peakR || 0);
    tg.glow = clamp(((pk > 0 ? 20 * Math.log10(pk) : -100) + 42) / 36, 0, 1);
    tg.speed = 0.5 + 1.3 * clamp(sky.energy ?? 0.5, 0, 1);
    tg.drift = clamp(((sky.energy ?? 0.5) - 0.5) * 2, -1, 1);
    return tg;
  }

  // smoothed values (what the shader actually sees)
  const sm = { mode: 0, energy: 0.55, scale: 2.0, speed: 1.0, glow: 0.3, drift: 0, envFast: 0, envSlow: 0 };
  let phase = 0, last = performance.now(), lastDraw = 0;

  function tick(now) {
    requestAnimationFrame(tick);
    const dt = Math.min(0.1, (now - last) / 1000);
    last = now;
    if (level < 2 || !gl || document.hidden) return;

    const tg = targets();
    sm.mode = follow(sm.mode, tg.mode, dt, 1.2);
    sm.energy = follow(sm.energy, tg.energy, dt, 2.5);
    sm.scale = follow(sm.scale, tg.scale, dt, 3.0);
    sm.speed = follow(sm.speed, tg.speed, dt, 2.5);
    sm.glow = follow(sm.glow, tg.glow, dt, 0.6);
    sm.drift = follow(sm.drift, tg.drift, dt, 1.5);
    sm.envFast = follow(sm.envFast, tg.pulse, dt, tg.pulse > sm.envFast ? 0.06 : 0.5);
    sm.envSlow = follow(sm.envSlow, tg.pulse, dt, 3.0);
    phase += dt * sm.speed;               // integrate speed: changing it never jumps the picture

    if (now - lastDraw < 1000 / FPS - 2) return;
    lastDraw = now;
    gl.uniform2f(U.u_res, canvas.width, canvas.height);
    gl.uniform1f(U.u_time, phase);
    gl.uniform1f(U.u_mode, clamp(sm.mode, 0, 1));
    gl.uniform1f(U.u_energy, sm.energy);
    gl.uniform1f(U.u_scale, sm.scale);
    gl.uniform1f(U.u_glow, sm.glow);
    gl.uniform1f(U.u_pulse, clamp((sm.envFast - sm.envSlow) * 2.5, 0, 1));
    gl.uniform1f(U.u_drift, sm.drift);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  }

  // ---------- switching ----------
  let level = 0;
  function setLevel(l) {
    if (l >= 2 && !ensureGL()) l = 1;
    level = l;
    const c = document.body.classList;
    c.toggle("ambient-on", l > 0);
    c.toggle("ambient-1", l === 1);
    c.toggle("ambient-gl", l >= 2);
    if (l >= 2) resize();
    try { localStorage.setItem(KEY, String(l)); } catch (e) { /* storage may be unavailable */ }
  }

  // #ambient=N picks a level; #...&bgonly hides the panels (for reviewing the background itself)
  if (/(?:^|[#&])bgonly(?:&|$)/.test(location.hash || "")) document.body.classList.add("bg-only");
  let initial = null;
  const m = /(?:^|[#&])ambient=(\d)/.exec(location.hash || "");
  if (m) initial = +m[1];
  if (initial === null) {
    try { const v = localStorage.getItem(KEY); if (v !== null) initial = +v; } catch (e) { /* ignore */ }
  }
  if (initial === null) {
    const reduce = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    initial = reduce ? 0 : 2;
  }
  setLevel(clamp(initial | 0, 0, 2));
  requestAnimationFrame(tick);

  window.__wtAmbient = { get level() { return level; }, setLevel, smoothed: sm };
})();
