/* All-sky dome renderer, as a classic script. Ported for T6.1 from prototype/sky/dome.js;
 * the changes for the plug-in are marked "T6.1". T6.6 (owner: "smoother, higher resolution, more
 * and better bloom") departs from the sketch: a spline curtain over lightly smoothed nodes, the fine
 * rays per pixel in the fragment shader, faded ends, a four-level bloom, dithered output. */
(function () {
  "use strict";
  const { EARTH_R_KM, STARS, altAz, domeXY, noise1 } = window.SKY;
  // All-sky dome renderer (spec/06-ui.md, ADR-0006). WebGL 1; Canvas 2D fallback.
  // Zenith at the centre, horizon at the rim, north up, east LEFT (seen from below).
  //
  // Pipeline: glow buffer (half res, additive): diffuse oval + curtain mesh
  //           → separable blur → screen: background + stars + tone-mapped glow.
  // The curtain is a mesh: columns along the chain (4 per node), rows up the field line
  // (95 → 300 km). Projecting it through the dipole field makes rays converge at magnetic
  // zenith on their own: a curtain overhead becomes a corona.


  const D2R = Math.PI / 180;
  const smoothstep = (a, b, x) => { const t = Math.max(0, Math.min(1, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
  // T6.6: 8 columns per node on a Catmull-Rom spline (was 4, linear) and 24 rows (was 14)
  const COLS_PER_NODE = 8;
  const ROWS = 24;
  const NOISE_PERIOD = 4096;   // the shader's noise repeats here, so time offsets wrap without a seam
  const H_BOTTOM = 92, H_TOP = 310;
  const C = {
    night: [0.020, 0.027, 0.051], green: [0.36, 0.95, 0.63], red: [1.0, 0.42, 0.42], violet: [0.56, 0.48, 1.0],
    rim: [0.55, 0.75, 1.0],
  };

  const VS_QUAD = `attribute vec2 a; varying vec2 v; void main(){ v = a; gl_Position = vec4(a, 0.0, 1.0); }`;

  const FS_BG = `precision highp float; varying vec2 v;
  uniform float uScale, uDay, uTwilight, uSunAz;
  uniform vec3 uNight, uRim;
  void main(){
    vec2 p = v / uScale; float rho = length(p);
    float inside = 1.0 - smoothstep(0.995, 1.005, rho);
    vec3 col = uNight * (1.0 + 0.9 * rho * rho);                          // faint airglow toward the horizon
    float az = atan(-p.x, p.y);
    float toward = 0.5 + 0.5 * cos(az - uSunAz);
    col += uRim * uDay * 0.22 * smoothstep(0.55, 1.0, rho) * (0.7 + 0.3 * toward);     // stylistic day shimmer
    col += vec3(0.30, 0.42, 0.70) * uTwilight * 0.18 * pow(toward, 6.0) * smoothstep(0.7, 1.0, rho);
    gl_FragColor = vec4(mix(uNight * 0.7, col, inside), 1.0);
  }`;

  const FS_OVAL = `precision highp float; varying vec2 v;
  uniform float uScale, uR, uRs;
  uniform vec3 uE, uN, uU, uZm, uXm, uYm;
  uniform float uEq, uPole, uDayShift, uMlt0, uMlon0, uDiffuse, uRed, uQuietRed;
  uniform vec3 uGreen, uRedC;
  const float PI = 3.14159265;
  float layer(vec3 d, float h, out float mlat, out float mlt){
    vec3 P = uU * uRs;
    float b = dot(P, d); float c = uRs * uRs - (uR + h) * (uR + h);
    float s = -b + sqrt(max(b * b - c, 0.0));
    vec3 q = normalize(P + s * d);
    mlat = degrees(asin(clamp(dot(q, uZm), -1.0, 1.0)));
    float mlon = degrees(atan(dot(q, uYm), dot(q, uXm)));
    float dl = mod(mlon - uMlon0 + 540.0, 360.0) - 180.0;
    mlt = uMlt0 + dl / 15.0;
    return s;
  }
  void main(){
    vec2 p = v / uScale; float rho = length(p);
    if (rho > 1.0) { gl_FragColor = vec4(0.0); return; }
    float az = atan(-p.x, p.y); float el = radians(90.0 * (1.0 - rho));
    vec3 d = cos(el) * (sin(az) * uE + cos(az) * uN) + sin(el) * uU;
    float slant = 1.0 + 2.0 * pow(1.0 - sin(el), 3.0);
    float mlat, mlt;
    layer(d, 115.0, mlat, mlt);
    float shift = uDayShift * 0.5 * (1.0 - cos(2.0 * PI * mlt / 24.0));
    float night = 0.25 + 0.75 * (0.5 + 0.5 * cos(2.0 * PI * mlt / 24.0));
    // diffuse aurora sits at the equatorward edge of the oval and fades poleward
    float eq = uEq + shift;
    float band = smoothstep(eq - 1.5, eq + 1.0, mlat) * exp(-max(0.0, mlat - eq - 1.0) / 3.0)
      * (1.0 - smoothstep(uPole + shift - 1.5, uPole + shift + 2.0, mlat));
    vec3 col = uGreen * uDiffuse * band * night * slant;
    layer(d, 250.0, mlat, mlt);
    // high red glow: equatorward part of the oval (storm-time red aurora at mid-latitudes)
    float bandR = smoothstep(eq - 6.0, eq, mlat) * (1.0 - smoothstep(eq + 3.0, eq + 8.0, mlat));
    col += uRedC * (uRed + uQuietRed) * bandR * night * slant;
    float horizon = smoothstep(1.0, 0.96, rho);
    gl_FragColor = vec4(col * horizon, 1.0);
  }`;

  // T6.6: s = (position along the chain in nodes, height 0…1). The coarse ray octave is per vertex
  // (it also stretches the green); the fine striations are per pixel, so they are smooth at any
  // size and slide without the per-column aliasing the sketch's mesh had.
  const VS_MESH = `attribute vec2 a; attribute vec3 c; attribute vec2 s; varying vec3 vc; varying vec2 vs; uniform float uScale;
  void main(){ vc = c; vs = s; gl_Position = vec4(a * uScale, 0.0, 1.0); }`;
  const FS_MESH = `precision highp float; varying vec3 vc; varying vec2 vs;
  uniform float uRays, uOff2, uOff3, uSeed;
  float hash(float i){ return fract(sin(mod(i, ${NOISE_PERIOD}.0) * 12.9898 + uSeed) * 43758.5453); }
  float vnoise(float x){ float i = floor(x), f = x - i; return mix(hash(i), hash(i + 1.0), f * f * (3.0 - 2.0 * f)) * 2.0 - 1.0; }
  void main(){
    float n2 = vnoise(vs.x * 7.6 + uOff2), n3 = vnoise(vs.x * 19.0 + uOff3);
    float amp = uRays * (0.45 + 0.75 * vs.y);                 // rays stand out toward the top
    float fine = max(0.15, 1.0 + amp * (0.6 * n2 + 0.2 * n3));
    float top = 1.0 - smoothstep(0.55 + 0.25 * n2, 1.0, vs.y); // each ray reaches its own height
    gl_FragColor = vec4(vc * fine * mix(1.0, top, min(1.0, uRays)), 1.0);
  }`;

  const FS_BLUR = `precision highp float; varying vec2 v; uniform sampler2D t; uniform vec2 dir;
  void main(){
    vec2 uv = v * 0.5 + 0.5; vec3 s = texture2D(t, uv).rgb * 0.2270270;
    s += (texture2D(t, uv + dir * 1.3846154).rgb + texture2D(t, uv - dir * 1.3846154).rgb) * 0.3162162;
    s += (texture2D(t, uv + dir * 3.2307692).rgb + texture2D(t, uv - dir * 3.2307692).rgb) * 0.0702703;
    gl_FragColor = vec4(s, 1.0);
  }`;

  // T6.6: four bloom levels (½, ¼, ⅛ of the glow buffer, each blurred from the last), bright cores
  // lean a little toward white like an all-sky camera's, and a dither keeps the dark gradients from
  // banding in 8 bits.
  const FS_COMPOSITE = `precision highp float; varying vec2 v; uniform sampler2D sharp, b1, b2, b3; uniform float uScale, uExposure;
  void main(){
    vec2 uv = v * 0.5 + 0.5;
    vec3 g = texture2D(sharp, uv).rgb * 0.85 + texture2D(b1, uv).rgb * 0.62 + texture2D(b2, uv).rgb * 0.46 + texture2D(b3, uv).rgb * 0.42;
    g = 1.0 - exp(-g * uExposure);
    float l = max(g.r, max(g.g, g.b));
    g = mix(g, vec3(l), 0.22 * l * l);
    g += (fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5) / 255.0;
    float rho = length(v / uScale);
    gl_FragColor = vec4(max(g, 0.0) * (1.0 - smoothstep(0.99, 1.01, rho)), 1.0);
  }`;

  const VS_STAR = `attribute vec2 a; attribute vec4 c; varying vec4 vc; uniform float uScale, uDpr;
  void main(){ vc = c; gl_PointSize = c.a * uDpr; gl_Position = vec4(a * uScale, 0.0, 1.0); }`;
  const FS_STAR = `precision mediump float; varying vec4 vc;
  void main(){ float r = length(gl_PointCoord - 0.5) * 2.0; float k = smoothstep(1.0, 0.0, r); gl_FragColor = vec4(vc.rgb * k * k, 1.0); }`;

  function starColor(kelvin) {
    const t = Math.max(0, Math.min(1, (kelvin - 3500) / 9000));
    return [1.0 - 0.25 * t, 0.86 + 0.06 * t, 0.72 + 0.28 * t];
  }

  class DomeRenderer {
    constructor(canvas, overlay, observer, seed = 1) {
      this.canvas = canvas;
      this.overlay = overlay;
      this.obs = observer;
      this.seed = seed;
      this.scale = 0.94;
      this.options = { stars: true, moon: true, dayNight: true, gradientsOnly: false, exposure: 1.6 };
      this.gl = canvas.getContext('webgl', { antialias: false, premultipliedAlpha: false, powerPreference: 'low-power' });
      this.ctx2d = null;
      if (this.gl) this.initGL(); else this.ctx2d = canvas.getContext('2d');
      this.mesh = null;
    }

    get mode() { return this.gl ? 'webgl' : 'canvas2d'; }

    // ---------------------------------------------------------------- GL setup
    initGL() {
      const gl = this.gl;
      const compile = (type, src) => {
        const s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s);
        if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
        return s;
      };
      const program = (vs, fs) => {
        const p = gl.createProgram();
        gl.attachShader(p, compile(gl.VERTEX_SHADER, vs)); gl.attachShader(p, compile(gl.FRAGMENT_SHADER, fs));
        gl.linkProgram(p);
        if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
        const u = {}; const n = gl.getProgramParameter(p, gl.ACTIVE_UNIFORMS);
        for (let i = 0; i < n; i++) { const info = gl.getActiveUniform(p, i); u[info.name] = gl.getUniformLocation(p, info.name); }
        return { p, u, a: gl.getAttribLocation(p, 'a'), c: gl.getAttribLocation(p, 'c'), s: gl.getAttribLocation(p, 's') };
      };
      this.prog = {
        bg: program(VS_QUAD, FS_BG), oval: program(VS_QUAD, FS_OVAL), mesh: program(VS_MESH, FS_MESH),
        blur: program(VS_QUAD, FS_BLUR), comp: program(VS_QUAD, FS_COMPOSITE), star: program(VS_STAR, FS_STAR),
      };
      this.quad = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, this.quad);
      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), gl.STATIC_DRAW);
      const half = gl.getExtension('OES_texture_half_float');
      gl.getExtension('OES_texture_half_float_linear');
      const halfOk = half && gl.getExtension('EXT_color_buffer_half_float');
      this.texType = halfOk ? half.HALF_FLOAT_OES : gl.UNSIGNED_BYTE;
      this.meshPos = gl.createBuffer(); this.meshCol = gl.createBuffer(); this.meshIdx = gl.createBuffer(); this.meshS = gl.createBuffer();
      this.starPos = gl.createBuffer(); this.starCol = gl.createBuffer();
      this.fbos = null;
    }

    makeTarget(w, h) {
      const gl = this.gl;
      const tex = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, w, h, 0, gl.RGBA, this.texType, null);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
      const fb = gl.createFramebuffer();
      gl.bindFramebuffer(gl.FRAMEBUFFER, fb);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
      if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE && this.texType !== gl.UNSIGNED_BYTE) {
        this.texType = gl.UNSIGNED_BYTE; gl.deleteTexture(tex); gl.deleteFramebuffer(fb); return this.makeTarget(w, h);
      }
      return { tex, fb, w, h };
    }

    resize() {
      // T6.1: the editor scales the whole stage with a CSS transform; render for that size.
      const zoom = (window.UI && window.UI.scale) || 1;
      const dpr = Math.min((window.devicePixelRatio || 1) * zoom, 2.5);
      const css = this.canvas.clientWidth;
      const px = Math.max(64, Math.round(css * dpr));
      if (this.canvas.width === px && this.fbos) return;
      this.dpr = dpr;
      this.canvas.width = this.canvas.height = px;
      this.overlay.width = this.overlay.height = px;
      if (this.gl) {
        // T6.1: a small dome keeps the glow at full resolution, or the curtain's edges show the
        // half-res buffer's steps. T6.6: full resolution up to 1400 px (the fine rays live there),
        // then a bloom chain at ½, ¼, ⅛.
        const h = px <= 1400 ? px : px >> 1, l1 = Math.max(8, h >> 1), l2 = Math.max(8, h >> 2), l3 = Math.max(8, h >> 3);
        this.fbos = { glow: this.makeTarget(h, h),
          t1: this.makeTarget(l1, l1), b1: this.makeTarget(l1, l1), t2: this.makeTarget(l2, l2), b2: this.makeTarget(l2, l2),
          t3: this.makeTarget(l3, l3), b3: this.makeTarget(l3, l3) };
      }
    }

    // ---------------------------------------------------------------- geometry
    /** T6.6: the node arrays smoothed along the chain, longitudes unwrapped, so the spline has no
     *  kinks to follow. The model moves the curtain in patches of ~6 nodes: flat runs joined by
     *  ramps of up to 1° over 3 nodes, which the projection sharpens near the horizon. Position: a
     *  9-tap binomial (1-4-6-4-1 twice, σ ≈ 1.4 nodes); colour: 1-2-1. Picture only; the sound
     *  keeps the model's curtain. Returns the scratch arrays. */
    smoothNodes(snap) {
      const n = snap.nodeCount, nd = snap.nodes;
      if (!this.sm || this.sm.n !== n) {
        const f = () => new Float64Array(n);
        this.sm = { n, mlat: f(), mlon: f(), green: f(), red: f(), violet: f(), lon: f(), t1: f(), t2: f() };
      }
      const sm = this.sm, at = (i) => Math.max(0, Math.min(n - 1, i));
      sm.lon[0] = nd.mlon[0];
      for (let i = 1; i < n; i++) sm.lon[i] = sm.lon[i - 1] + ((((nd.mlon[i] - nd.mlon[i - 1]) % 360) + 540) % 360 - 180);
      const b5 = (src, i) => (src[at(i - 2)] + 4 * src[at(i - 1)] + 6 * src[i] + 4 * src[at(i + 1)] + src[at(i + 2)]) / 16;
      for (let i = 0; i < n; i++) { sm.t1[i] = b5(nd.mlat, i); sm.t2[i] = b5(sm.lon, i); }
      for (let i = 0; i < n; i++) { sm.mlat[i] = b5(sm.t1, i); sm.mlon[i] = b5(sm.t2, i); }
      for (let i = 0; i < n; i++) {
        const a1 = at(i - 1), b1 = at(i + 1);
        sm.green[i] = (nd.green[a1] + 2 * nd.green[i] + nd.green[b1]) / 4;
        sm.red[i] = (nd.red[a1] + 2 * nd.red[i] + nd.red[b1]) / 4;
        sm.violet[i] = (nd.violet[a1] + 2 * nd.violet[i] + nd.violet[b1]) / 4;
      }
      return sm;
    }

    /** Build the curtain mesh: positions (dome xy), colours and (along, height) for every vertex. */
    buildMesh(snap, tReal) {
      const n = snap.nodeCount, obs = this.obs;
      const cols = (n - 1) * COLS_PER_NODE + 1;
      if (!this.mesh || this.mesh.cols !== cols) {
        const verts = cols * (ROWS + 1);
        const idx = new Uint16Array((cols - 1) * ROWS * 6);
        let k = 0;
        for (let c = 0; c < cols - 1; c++) for (let r = 0; r < ROWS; r++) {
          const a = c * (ROWS + 1) + r, b = a + ROWS + 1;
          idx[k++] = a; idx[k++] = b; idx[k++] = a + 1; idx[k++] = a + 1; idx[k++] = b; idx[k++] = b + 1;
        }
        // the per-row profile terms that don't depend on the column
        const rowH = new Float64Array(ROWS + 1), rowV = new Float64Array(ROWS + 1), rowR = new Float64Array(ROWS + 1);
        for (let r = 0; r <= ROWS; r++) {
          const h = H_BOTTOM + (H_TOP - H_BOTTOM) * Math.pow(r / ROWS, 1.35);
          rowH[r] = h;
          rowV[r] = Math.exp(-(((h - 100) / 9) ** 2));
          rowR[r] = Math.exp(-(((h - 235) / 55) ** 2)) * (1 - smoothstep(0.72, 1.0, r / ROWS));   // T6.6: the red fades out at the top
        }
        this.mesh = { cols, verts, pos: new Float32Array(verts * 2), col: new Float32Array(verts * 3), st: new Float32Array(verts * 2),
          idx, uploaded: false, stUploaded: false, rowH, rowV, rowR };
      }
      const m = this.mesh, { pos, col, st, rowH, rowV, rowR } = m;
      const sm = this.smoothNodes(snap);
      const P = obs.pos, E = obs.east, N = obs.north, U = obs.up;
      const raysAmount = this.options.gradientsOnly ? 0 : 0.35 + 0.6 * snap.activity + 0.8 * Math.min(1, snap.unloading);
      this.raysAmount = raysAmount;
      const cr = (arr, i, t) => {
        const p0 = arr[Math.max(0, i - 1)], p1 = arr[i], p2 = arr[Math.min(n - 1, i + 1)], p3 = arr[Math.min(n - 1, i + 2)];
        return 0.5 * (2 * p1 + (p2 - p0) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (3 * p1 - p0 - 3 * p2 + p3) * t * t * t);
      };
      const ends = 1.5;   // nodes over which the curtain's ends fade in
      // hot loop (T6.6): no Math.hypot (slow in JS engines), colour constants in locals
      const vr = C.violet[0], vg = C.violet[1], vb = C.violet[2], gr = C.green[0], gg = C.green[1], gb = C.green[2];
      const rr = C.red[0], rg = C.red[1], rb = C.red[2];
      const E0 = E[0], E1 = E[1], E2 = E[2], N0 = N[0], N1 = N[1], N2 = N[2], U0 = U[0], U1 = U[1], U2 = U[2];
      const P0 = P[0], P1 = P[1], P2 = P[2];
      for (let c = 0; c < cols; c++) {
        const f = c / COLS_PER_NODE, i0 = Math.min(n - 2, Math.floor(f)), w = f - i0;
        const mlat = cr(sm.mlat, i0, w), mlon = cr(sm.mlon, i0, w);
        const g = Math.max(0, cr(sm.green, i0, w)), rd = Math.max(0, cr(sm.red, i0, w)), vi = Math.max(0, cr(sm.violet, i0, w));
        // the coarse ray octave, sampled along the chain (T6.6: in node units, not per column)
        const ray = Math.max(0.12, 1 + raysAmount * 0.9 * noise1(this.seed, 61, f * 2.2 + tReal * 0.35));
        const stretch = 0.8 + 0.5 * Math.max(0, ray - 1);
        const fade = smoothstep(0, ends, f) * smoothstep(0, ends, (n - 1) - f);
        const base = obs.pointAt(mlat, mlon, H_BOTTOM);
        const up = obs.fieldUp(mlat, mlon);
        const sinI = Math.max(0.2, Math.abs(Math.sin(Math.atan(2 * Math.tan(mlat * D2R)))));
        // sheet normal ≈ horizontal magnetic north–south at the footprint
        const sx = up[0] - base[0] / (EARTH_R_KM + H_BOTTOM) * sinI, sy = up[1] - base[1] / (EARTH_R_KM + H_BOTTOM) * sinI,
          sz = up[2] - base[2] / (EARTH_R_KM + H_BOTTOM) * sinI;
        const sl = Math.sqrt(sx * sx + sy * sy + sz * sz) || 1;
        const b0 = base[0], b1 = base[1], b2 = base[2], u0 = up[0], u1 = up[1], u2 = up[2];
        const kc = 0.42 * ray * fade;
        for (let r = 0; r <= ROWS; r++) {
          const h = rowH[r];
          const L = (h - H_BOTTOM) / sinI;
          const qx = b0 + u0 * L - P0, qy = b1 + u1 * L - P1, qz = b2 + u2 * L - P2;
          const e = qx * E0 + qy * E1 + qz * E2, nn = qx * N0 + qy * N1 + qz * N2, u = qx * U0 + qy * U1 + qz * U2;
          const hor2 = e * e + nn * nn, hor = Math.sqrt(hor2) || 1e-9, dist = Math.sqrt(hor2 + u * u);
          const el = Math.atan2(u, hor) / D2R;
          // domeXY without the trig: sin(az) = e / hor, cos(az) = nn / hor (T6.6)
          const rho = (90 - el) / 90;
          const v = c * (ROWS + 1) + r;
          pos[2 * v] = -rho * e / hor; pos[2 * v + 1] = rho * nn / hor;
          st[2 * v] = f; st[2 * v + 1] = r / ROWS;
          // edge-on sheets brighten (×2.9 at most; the sketch's ×4 made folds into hot wedges, T6.6)
          const edge = 1 / (0.35 + 0.65 * Math.abs((qx * sx + qy * sy + qz * sz) / (sl * dist)));
          const ext = Math.max(0, Math.min(1, (el + 1) / 7));
          const pg = h < 108 ? Math.exp(-(((h - 108) / 7) ** 2)) : Math.exp(-(h - 108) / stretch / 38);
          const k = kc * edge * ext;
          const pv = rowV[r], pr = rowR[r];
          const a = vi * pv, b = g * pg, cc = rd * pr;
          col[3 * v] = k * (a * vr + b * gr + cc * rr);
          col[3 * v + 1] = k * (a * vg + b * gg + cc * rg);
          col[3 * v + 2] = k * (a * vb + b * gb + cc * rb);
        }
      }
    }

    buildStars(snap) {
      const ms = snap.modelTimeUtc, st = this.obs.station;
      const day = this.dayFactor(snap);
      const pos = [], col = [];
      if (this.options.stars) for (const [ra, dec, v, kelvin] of STARS) {
        const h = altAz(ra, dec, st.lat, st.lon, ms);
        if (h.el < 0) continue;
        const d = domeXY(h.az, h.el);
        const flux = Math.pow(10, -0.4 * (v - 1)) * (0.35 + 0.65 * Math.pow(Math.sin(Math.max(0.02, h.el * D2R)), 0.4));
        const vis = (1 - day) * Math.min(1, 0.25 + 0.5 * flux);
        const c = starColor(kelvin);
        pos.push(d.x, d.y);
        col.push(c[0] * vis, c[1] * vis, c[2] * vis, Math.max(1.6, 4.2 - 0.9 * v));
      }
      return { pos: new Float32Array(pos), col: new Float32Array(col), count: pos.length / 2 };
    }

    dayFactor(snap) {
      if (!this.options.dayNight) return 0;
      return Math.max(0, Math.min(1, (snap.sky.sunEl + 6) / 10));       // civil twilight → full day at +4°
    }

    // ---------------------------------------------------------------- frame
    render(snap, tReal) {
      this.resize();
      if (!this.gl) { this.render2d(snap, tReal); this.drawOverlay(snap); return; }
      const gl = this.gl, P = this.prog, f = this.fbos;
      const t0 = performance.now();
      this.buildMesh(snap, tReal);
      this.meshMs = (this.meshMs || 0) + (performance.now() - t0 - (this.meshMs || 0)) * 0.05;
      const quad = (prog) => {
        gl.bindBuffer(gl.ARRAY_BUFFER, this.quad);
        gl.enableVertexAttribArray(prog.a); gl.vertexAttribPointer(prog.a, 2, gl.FLOAT, false, 0, 0);
        gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
      };
      const target = (t) => { gl.bindFramebuffer(gl.FRAMEBUFFER, t ? t.fb : null); const w = t ? t.w : this.canvas.width; gl.viewport(0, 0, w, t ? t.h : this.canvas.height); };

      // 1. glow buffer: diffuse oval + curtain (additive)
      target(f.glow);
      gl.clearColor(0, 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT);
      gl.enable(gl.BLEND); gl.blendFunc(gl.ONE, gl.ONE);
      const o = this.obs;
      gl.useProgram(P.oval.p);
      const u = P.oval.u;
      gl.uniform1f(u.uScale, this.scale); gl.uniform1f(u.uR, EARTH_R_KM); gl.uniform1f(u.uRs, EARTH_R_KM + o.station.elevKm);
      gl.uniform3fv(u.uE, o.east); gl.uniform3fv(u.uN, o.north); gl.uniform3fv(u.uU, o.up);
      gl.uniform3fv(u.uZm, o.zm); gl.uniform3fv(u.uXm, o.xm); gl.uniform3fv(u.uYm, o.ym);
      gl.uniform1f(u.uEq, snap.eqLat); gl.uniform1f(u.uPole, snap.poleLat); gl.uniform1f(u.uDayShift, snap.dayShiftDeg);
      gl.uniform1f(u.uMlt0, snap.mlt); gl.uniform1f(u.uMlon0, o.mlon);
      gl.uniform1f(u.uDiffuse, 0.008 + 0.035 * snap.activity * snap.gain + 0.04 * Math.min(1.5, snap.unloading));
      gl.uniform1f(u.uRed, 0.055 * snap.stormRed); gl.uniform1f(u.uQuietRed, 0.006);
      gl.uniform3fv(u.uGreen, C.green); gl.uniform3fv(u.uRedC, C.red);
      quad(P.oval);

      gl.useProgram(P.mesh.p);
      gl.uniform1f(P.mesh.u.uScale, this.scale);
      // the fine rays' drift, wrapped on the noise period in double precision here (T6.6)
      const wrap = (x) => ((x % NOISE_PERIOD) + NOISE_PERIOD) % NOISE_PERIOD;
      gl.uniform1f(P.mesh.u.uRays, this.raysAmount || 0);
      gl.uniform1f(P.mesh.u.uOff2, wrap(-tReal * 0.8));
      gl.uniform1f(P.mesh.u.uOff3, wrap(tReal * 1.3));
      gl.uniform1f(P.mesh.u.uSeed, (this.seed * 7.31) % 97);
      gl.bindBuffer(gl.ARRAY_BUFFER, this.meshS);
      if (!this.mesh.stUploaded) { gl.bufferData(gl.ARRAY_BUFFER, this.mesh.st, gl.STATIC_DRAW); this.mesh.stUploaded = true; }
      gl.enableVertexAttribArray(P.mesh.s); gl.vertexAttribPointer(P.mesh.s, 2, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, this.meshPos);
      gl.bufferData(gl.ARRAY_BUFFER, this.mesh.pos, gl.DYNAMIC_DRAW);
      gl.enableVertexAttribArray(P.mesh.a); gl.vertexAttribPointer(P.mesh.a, 2, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ARRAY_BUFFER, this.meshCol);
      gl.bufferData(gl.ARRAY_BUFFER, this.mesh.col, gl.DYNAMIC_DRAW);
      gl.enableVertexAttribArray(P.mesh.c); gl.vertexAttribPointer(P.mesh.c, 3, gl.FLOAT, false, 0, 0);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.meshIdx);
      if (!this.mesh.uploaded) { gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, this.mesh.idx, gl.STATIC_DRAW); this.mesh.uploaded = true; }
      gl.drawElements(gl.TRIANGLES, this.mesh.idx.length, gl.UNSIGNED_SHORT, 0);
      gl.disableVertexAttribArray(P.mesh.c);
      gl.disableVertexAttribArray(P.mesh.s);
      gl.disable(gl.BLEND);

      // 2. blur: half-res soft, quarter-res wide
      const blur = (src, tmp, dst, px) => {
        gl.useProgram(P.blur.p);
        target(tmp); gl.bindTexture(gl.TEXTURE_2D, src.tex); gl.uniform1i(P.blur.u.t, 0);
        gl.uniform2f(P.blur.u.dir, px / tmp.w, 0); quad(P.blur);
        target(dst); gl.bindTexture(gl.TEXTURE_2D, tmp.tex);
        gl.uniform2f(P.blur.u.dir, 0, px / dst.h); quad(P.blur);
      };
      // T6.1: blur radii scale with the buffer, so the halo is the same share of the sky at any size.
      // T6.6: a chain at ½, ¼, ⅛, each from the last: ~10, 25, 55 and 110 px of halo on a 900 px dome.
      const k = f.glow.w / 900;
      const soft = this.options.gradientsOnly ? 2.6 : 1.5;
      blur(f.glow, f.t1, f.b1, soft * k * 2);
      blur(f.b1, f.t2, f.b2, 1.8 * k * 2);
      blur(f.b2, f.t3, f.b3, 2.0 * k * 2);

      // 3. screen: background, stars, glow
      target(null);
      gl.useProgram(P.bg.p);
      const day = this.dayFactor(snap);
      gl.uniform1f(P.bg.u.uScale, this.scale); gl.uniform1f(P.bg.u.uDay, day);
      gl.uniform1f(P.bg.u.uTwilight, this.options.dayNight ? Math.max(0, 1 - Math.abs(snap.sky.sunEl + 6) / 12) : 0);
      gl.uniform1f(P.bg.u.uSunAz, snap.sky.sunAz * D2R);
      gl.uniform3fv(P.bg.u.uNight, C.night); gl.uniform3fv(P.bg.u.uRim, C.rim);
      quad(P.bg);

      gl.enable(gl.BLEND); gl.blendFunc(gl.ONE, gl.ONE);
      const stars = this.buildStars(snap);
      if (stars.count) {
        gl.useProgram(P.star.p);
        gl.uniform1f(P.star.u.uScale, this.scale); gl.uniform1f(P.star.u.uDpr, this.dpr);
        gl.bindBuffer(gl.ARRAY_BUFFER, this.starPos); gl.bufferData(gl.ARRAY_BUFFER, stars.pos, gl.DYNAMIC_DRAW);
        gl.enableVertexAttribArray(P.star.a); gl.vertexAttribPointer(P.star.a, 2, gl.FLOAT, false, 0, 0);
        gl.bindBuffer(gl.ARRAY_BUFFER, this.starCol); gl.bufferData(gl.ARRAY_BUFFER, stars.col, gl.DYNAMIC_DRAW);
        gl.enableVertexAttribArray(P.star.c); gl.vertexAttribPointer(P.star.c, 4, gl.FLOAT, false, 0, 0);
        gl.drawArrays(gl.POINTS, 0, stars.count);
        gl.disableVertexAttribArray(P.star.c);
      }
      gl.useProgram(P.comp.p);
      gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, f.glow.tex); gl.uniform1i(P.comp.u.sharp, 0);
      gl.activeTexture(gl.TEXTURE1); gl.bindTexture(gl.TEXTURE_2D, f.b1.tex); gl.uniform1i(P.comp.u.b1, 1);
      gl.activeTexture(gl.TEXTURE2); gl.bindTexture(gl.TEXTURE_2D, f.b2.tex); gl.uniform1i(P.comp.u.b2, 2);
      gl.activeTexture(gl.TEXTURE3); gl.bindTexture(gl.TEXTURE_2D, f.b3.tex); gl.uniform1i(P.comp.u.b3, 3);
      gl.uniform1f(P.comp.u.uScale, this.scale); gl.uniform1f(P.comp.u.uExposure, this.options.exposure);
      quad(P.comp);
      gl.activeTexture(gl.TEXTURE0);
      gl.disable(gl.BLEND);

      this.drawOverlay(snap);
    }

    /** Canvas 2D fallback: rays as additive strokes, blurred. */
    render2d(snap, tReal) {
      const ctx = this.ctx2d, W = this.canvas.width, R = (W / 2) * this.scale;
      this.buildMesh(snap, tReal);
      ctx.fillStyle = '#05070D'; ctx.fillRect(0, 0, W, W);
      ctx.save();
      ctx.translate(W / 2, W / 2);
      ctx.beginPath(); ctx.arc(0, 0, R, 0, 2 * Math.PI); ctx.fillStyle = '#070A16'; ctx.fill(); ctx.clip();
      ctx.globalCompositeOperation = 'lighter';
      ctx.filter = `blur(${Math.round(W / 160)}px)`;
      const { pos, col, cols } = this.mesh;
      for (let c = 0; c < cols; c += 2) for (let r = 0; r < ROWS; r++) {
        const a = c * (ROWS + 1) + r;
        const s = Math.min(1, col[3 * a] + col[3 * a + 1] + col[3 * a + 2]);
        if (s < 0.01) continue;
        ctx.strokeStyle = `rgba(${(255 * Math.min(1, col[3 * a] * 2)) | 0},${(255 * Math.min(1, col[3 * a + 1] * 2)) | 0},${(255 * Math.min(1, col[3 * a + 2] * 2)) | 0},${s})`;
        ctx.lineWidth = W / 300;
        ctx.beginPath(); ctx.moveTo(pos[2 * a] * R, -pos[2 * a + 1] * R); ctx.lineTo(pos[2 * a + 2] * R, -pos[2 * a + 3] * R); ctx.stroke();
      }
      ctx.restore();
    }

    /** Instrument layer: horizon, elevation rings, compass (E left), magnetic zenith, moon, sun. */
    drawOverlay(snap) {
      const ctx = this.overlay.getContext('2d'), W = this.overlay.width, R = (W / 2) * this.scale, dpr = this.dpr || 1;
      ctx.clearRect(0, 0, W, W);
      ctx.save();
      ctx.translate(W / 2, W / 2);
      const xy = (az, el) => { const d = domeXY(az, el); return [d.x * R, -d.y * R]; };
      ctx.lineWidth = dpr;
      ctx.strokeStyle = 'rgba(201,211,230,0.22)';
      ctx.beginPath(); ctx.arc(0, 0, R, 0, 2 * Math.PI); ctx.stroke();
      ctx.strokeStyle = 'rgba(201,211,230,0.08)';
      for (const el of [30, 60]) { ctx.beginPath(); ctx.arc(0, 0, R * (90 - el) / 90, 0, 2 * Math.PI); ctx.stroke(); }
      ctx.beginPath(); ctx.moveTo(-R, 0); ctx.lineTo(R, 0); ctx.moveTo(0, -R); ctx.lineTo(0, R); ctx.stroke();
      // T6.1: azimuth ticks on the horizon ring, every 10° (30° longer), like an all-sky camera's graticule
      ctx.strokeStyle = 'rgba(201,211,230,0.28)';
      ctx.beginPath();
      for (let az = 0; az < 360; az += 10) {
        const a = az * D2R, l = az % 30 === 0 ? 7 : 3.5;
        const sx = -Math.sin(a), sy = -Math.cos(a);
        ctx.moveTo(sx * R, sy * R); ctx.lineTo(sx * (R - l * dpr), sy * (R - l * dpr));
      }
      ctx.stroke();
      ctx.font = `${11 * dpr}px "JetBrains Mono", ui-monospace, monospace`;
      ctx.fillStyle = 'rgba(138,150,173,0.95)'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
      for (const [label, az] of [['N', 0], ['E', 90], ['S', 180], ['W', 270]]) {
        const [x, y] = xy(az, -3.5); ctx.fillText(label, x, y);
      }
      ctx.font = `${9 * dpr}px "JetBrains Mono", ui-monospace, monospace`;
      for (const el of [30, 60]) ctx.fillText(`${el}°`, 14 * dpr, -R * (90 - el) / 90 - 6 * dpr);
      // magnetic zenith
      const mz = this.obs.magZenith; const [mx, my] = xy(mz.az, mz.el);
      ctx.strokeStyle = 'rgba(201,211,230,0.45)';
      ctx.beginPath(); ctx.moveTo(mx - 5 * dpr, my); ctx.lineTo(mx + 5 * dpr, my); ctx.moveTo(mx, my - 5 * dpr); ctx.lineTo(mx, my + 5 * dpr); ctx.stroke();
      ctx.fillText('MZ', mx + 14 * dpr, my + 9 * dpr);
      // sun above the horizon
      const sky = snap.sky;
      if (sky.sunEl > 0) {
        const [sx, sy] = xy(sky.sunAz, sky.sunEl);
        ctx.strokeStyle = 'rgba(255,230,180,0.6)'; ctx.beginPath(); ctx.arc(sx, sy, 6 * dpr, 0, 2 * Math.PI); ctx.stroke();
      }
      // moon with phase; lit side faces the sun's dome position
      if (this.options.moon && sky.moonEl > -0.5) {
        const [x, y] = xy(sky.moonAz, sky.moonEl);
        const [sx, sy] = xy(sky.sunAz, sky.sunEl);
        const r = 7 * dpr, ang = Math.atan2(sy - y, sx - x), k = sky.moonIllum;
        const day = this.dayFactor(snap);
        ctx.save(); ctx.translate(x, y); ctx.rotate(ang);
        ctx.fillStyle = 'rgba(40,46,62,0.9)'; ctx.beginPath(); ctx.arc(0, 0, r, 0, 2 * Math.PI); ctx.fill();
        ctx.fillStyle = `rgba(232,236,245,${0.95 - 0.4 * day})`;
        ctx.beginPath();
        ctx.arc(0, 0, r, -Math.PI / 2, Math.PI / 2, false);                       // lit half toward the sun (+x)
        ctx.ellipse(0, 0, r * Math.abs(1 - 2 * k), r, 0, Math.PI / 2, -Math.PI / 2, k < 0.5);
        ctx.fill();
        ctx.restore();
      }
      ctx.restore();
    }
  }

  window.SKY.DomeRenderer = DomeRenderer;
})();
