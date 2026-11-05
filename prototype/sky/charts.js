// Strip chart under the dome: the whole day at a glance, click to seek.
// Lanes: magnetogram (measured vs model ΔH), coupling and reservoir, phase band + onsets.

const INK = '#C9D3E6', MUTED = '#8A96AD', GREEN = '#5CF2A0', RED = '#FF6B6B', VIOLET = '#8F7BFF';
const PHASE_COLORS = ['rgba(138,150,173,0.25)', 'rgba(143,123,255,0.55)', 'rgba(92,242,160,0.95)', 'rgba(92,242,160,0.4)'];

export class StripChart {
  constructor(canvas, onSeek) {
    this.canvas = canvas;
    this.onSeek = onSeek;
    this.base = document.createElement('canvas');
    canvas.addEventListener('click', (e) => {
      if (!this.trace) return;
      const r = canvas.getBoundingClientRect();
      const f = (e.clientX - r.left - this.padL) / (r.width - this.padL - this.padR);
      if (f >= 0 && f <= 1) onSeek(this.t0 + f * (this.t1 - this.t0));
    });
  }

  /** New day or new constants: redraw the static layer. */
  setData(track, trace) {
    this.track = track; this.trace = trace;
    this.t0 = track.startMs; this.t1 = track.endMs;
    this.drawBase();
  }

  layout() {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = Math.round(this.canvas.clientWidth * dpr), h = Math.round(this.canvas.clientHeight * dpr);
    if (this.canvas.width !== w || this.canvas.height !== h) {
      this.canvas.width = this.base.width = w; this.canvas.height = this.base.height = h;
      this.dpr = dpr;
      if (this.trace) this.drawBase();
    }
    this.padL = 92; this.padR = 12;
  }

  drawBase() {
    this.layout();
    const ctx = this.base.getContext('2d'), W = this.base.width, H = this.base.height, d = this.dpr;
    const L = this.padL * d, R = W - this.padR * d, T = this.trace, n = T.t.length;
    ctx.clearRect(0, 0, W, H);
    const x = (i) => L + (R - L) * (i / (n - 1));
    const lanes = { mag: [6 * d, H * 0.52], load: [H * 0.56, H * 0.84], phase: [H * 0.88, H - 4 * d] };
    ctx.font = `${10 * d}px "JetBrains Mono", ui-monospace, monospace`;
    ctx.textBaseline = 'middle';

    // time grid every 6 h
    ctx.strokeStyle = 'rgba(201,211,230,0.07)'; ctx.fillStyle = MUTED; ctx.lineWidth = d;
    for (let m = 0; m < n; m += 360) {
      ctx.beginPath(); ctx.moveTo(x(m), 0); ctx.lineTo(x(m), H); ctx.stroke();
      const label = new Date(T.t[m]).toISOString().slice(11, 16);
      ctx.fillText(m === 0 ? new Date(T.t[m]).toISOString().slice(5, 10) : label, x(m) + 3 * d, lanes.load[0] - 1 * d);
    }

    // magnetogram: measured and model ΔH, symmetric scale
    let max = 50;
    for (let i = 0; i < n; i++) max = Math.max(max, Math.abs(T.dh[i]), Math.abs(T.dhModel[i]));
    const [m0, m1] = lanes.mag, mid = (m0 + m1) / 2, sy = (m1 - m0) / 2 / max;
    ctx.strokeStyle = 'rgba(201,211,230,0.12)';
    ctx.beginPath(); ctx.moveTo(L, mid); ctx.lineTo(R, mid); ctx.stroke();
    const line = (arr, color, width) => {
      ctx.strokeStyle = color; ctx.lineWidth = width * d; ctx.beginPath();
      for (let i = 0; i < n; i++) { const y = mid - arr[i] * sy; i ? ctx.lineTo(x(i), y) : ctx.moveTo(x(i), y); }
      ctx.stroke();
    };
    line(T.dhModel, 'rgba(143,123,255,0.85)', 1);
    line(T.dh, GREEN, 1);
    ctx.fillStyle = INK; ctx.fillText('MAGNETOGRAM', 8 * d, m0 + 8 * d);
    ctx.fillStyle = MUTED;
    ctx.fillText(`ΔH ±${Math.round(max)} nT`, 8 * d, m0 + 22 * d);
    ctx.fillStyle = GREEN; ctx.fillText('— CMO', 8 * d, m0 + 36 * d);
    ctx.fillStyle = VIOLET; ctx.fillText('— model', 8 * d, m0 + 50 * d);

    // coupling (red) and reservoir energy (ink) with threshold
    const [l0, l1] = lanes.load;
    let emax = 2; for (let i = 0; i < n; i++) emax = Math.max(emax, T.coupling[i]);
    let wmax = 1.5; for (let i = 0; i < n; i++) wmax = Math.max(wmax, T.energy[i], T.threshold[i]);
    const lane = (arr, top, color, width, fill) => {
      ctx.beginPath();
      for (let i = 0; i < n; i++) { const y = l1 - (arr[i] / top) * (l1 - l0); i ? ctx.lineTo(x(i), y) : ctx.moveTo(x(i), y); }
      if (fill) { ctx.lineTo(R, l1); ctx.lineTo(L, l1); ctx.closePath(); ctx.fillStyle = fill; ctx.fill(); }
      else { ctx.strokeStyle = color; ctx.lineWidth = width * d; ctx.stroke(); }
    };
    lane(T.coupling, emax, null, 1, 'rgba(255,107,107,0.22)');
    lane(T.threshold, wmax, 'rgba(201,211,230,0.25)', 1);
    lane(T.energy, wmax, INK, 1);
    ctx.fillStyle = RED; ctx.fillText(`E ≤ ${emax.toFixed(0)} mV/m`, 8 * d, l0 + 8 * d);
    ctx.fillStyle = INK; ctx.fillText('energy / thr', 8 * d, l0 + 22 * d);

    // phase band + onset ticks
    const [p0, p1] = lanes.phase;
    for (let i = 0; i < n; i++) { ctx.fillStyle = PHASE_COLORS[T.phase[i]]; ctx.fillRect(x(i), p0, Math.max(1, (R - L) / n + 0.5), p1 - p0); }
    for (const e of T.events) {
      if (e.type !== 'onset') continue;
      const i = (e.t - this.t0) / 60000;
      ctx.fillStyle = e.kind === 'forced' ? RED : e.kind === 'pseudo' ? MUTED : GREEN;
      ctx.beginPath(); ctx.moveTo(x(i), p0 - 1 * d); ctx.lineTo(x(i) - 4 * d, p0 - 8 * d); ctx.lineTo(x(i) + 4 * d, p0 - 8 * d); ctx.fill();
    }
    ctx.fillStyle = MUTED; ctx.fillText('phase', 8 * d, (p0 + p1) / 2);
    this.lanes = lanes;
  }

  /** Copy the static layer and draw the now-marker. */
  draw(nowMs) {
    this.layout();
    const ctx = this.canvas.getContext('2d'), W = this.canvas.width, H = this.canvas.height, d = this.dpr;
    ctx.clearRect(0, 0, W, H);
    ctx.drawImage(this.base, 0, 0);
    if (!this.trace) return;
    const L = this.padL * d, R = W - this.padR * d;
    const xNow = L + (R - L) * ((nowMs - this.t0) / (this.t1 - this.t0));
    ctx.strokeStyle = RED; ctx.lineWidth = d; ctx.setLineDash([2 * d, 3 * d]);
    ctx.beginPath(); ctx.moveTo(xNow, 0); ctx.lineTo(xNow, H); ctx.stroke(); ctx.setLineDash([]);
  }
}
