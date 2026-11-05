/* Browser-only mock backend: a synthetic "ocean" table and animated telemetry,
 * driven by the current parameter values. Inactive inside the plug-in. */
(function () {
  "use strict";
  if (!window.WT.isMock) return;
  const WT = window.WT;

  function makeTable() {
    const frames = 64, wp = 128, hc = 48, rp = 64;
    const wave = [], harmonics = [], raw = [], times = [];
    const now = Date.now() / 1000;
    for (let f = 0; f < frames; f++) {
      const t = f / (frames - 1);
      const peak = 0.12 + 0.1 * Math.sin(t * 5.5) + 0.05 * Math.sin(t * 17);
      const width = 0.05 + 0.02 * Math.cos(t * 9);
      const swell = 0.6 + 0.4 * Math.sin(t * 3 + 1);
      for (let i = 0; i < rp; i++) {
        const x = i / (rp - 1);
        const v = swell * Math.exp(-Math.pow((x - peak) / width, 2)) + 0.35 * swell * Math.exp(-Math.pow((x - peak * 2.1) / (width * 1.6), 2));
        raw.push(Math.min(1, v));
      }
      const mags = [];
      for (let h = 1; h <= hc; h++) {
        const x = (h - 1) / 37;
        let m = swell * Math.exp(-Math.pow((x - peak) / (width * 1.3), 2)) + 0.3 * Math.exp(-Math.pow((x - peak * 2.1) / (width * 2), 2));
        if (h === 1) m = Math.max(m, 0.3);
        m *= Math.pow(h, -0.5);
        mags.push(m);
      }
      const mx = Math.max(...mags);
      for (const m of mags) harmonics.push(m / mx);
      const w = [];
      let pk = 0;
      for (let i = 0; i < wp; i++) {
        let s = 0;
        for (let h = 1; h <= 24; h++) s += mags[h - 1] * Math.sin(2 * Math.PI * h * i / wp + h * h * 0.7 * WT.get("wt_phase"));
        w.push(s); pk = Math.max(pk, Math.abs(s));
      }
      for (const s of w) wave.push(0.9 * s / pk);
      times.push(now - (frames - 1 - f) * 1800);
    }
    return {
      generation: 1, source: WT.get("wt_source"), station: "46026", place: "San Francisco",
      provider: "NOAA NDBC", origin: "factory", fetchedAt: 0, frames, wavePoints: wp,
      harmonicCount: hc, rawPoints: rp, wave, harmonics, raw, times, rawMin: 0, rawMax: 9.8,
      rawUnit: "m²/Hz", rawAxis: "0.033 – 0.49 Hz", isSpectrum: true,
    };
  }

  // Magnetosphere sources: one frame = one window of the H component (series-as-cycle),
  // with substorm bursts that come and go across the 64 frames.
  function makeGeomagTable(alaska) {
    const frames = 64, wp = 128, hc = 48, rp = 64;
    const wave = [], harmonics = [], raw = [], times = [];
    const now = Date.now() / 1000;
    const rnd = (a, b) => { const s = Math.sin(a * 12.9898 + b * 78.233) * 43758.5453; return s - Math.floor(s); };
    for (let f = 0; f < frames; f++) {
      const t = f / (frames - 1);
      const storm = (alaska ? 0.35 : 0.1) + (alaska ? 0.65 : 0.3) * Math.pow(Math.max(0, Math.sin(t * 4.2 + 0.6)), 3);
      const series = [];
      for (let i = 0; i < rp; i++) {
        const x = i / (rp - 1);
        const bay = storm * 0.35 * Math.sin(2 * Math.PI * 3 * x + f * 0.9) * Math.exp(-Math.pow((x - 0.6) / 0.22, 2));
        series.push(Math.min(1, Math.max(0, 0.5 + 0.12 * Math.sin(2 * Math.PI * x + f * 0.3) + bay + storm * 0.18 * (rnd(f, i) - 0.5))));
      }
      raw.push(...series);
      const w = [];
      let mean = 0;
      for (let i = 0; i < wp; i++) { const p = i / wp * (rp - 1), k = Math.floor(p); w.push(series[k] + (series[Math.min(k + 1, rp - 1)] - series[k]) * (p - k)); mean += w[i] / wp; }
      let pk = 1e-9;
      for (let i = 0; i < wp; i++) { w[i] -= mean; pk = Math.max(pk, Math.abs(w[i])); }
      for (const s of w) wave.push(0.9 * s / pk);
      const mags = [];
      for (let h = 1; h <= hc; h++) {
        let re = 0, im = 0;
        for (let i = 0; i < wp; i++) { re += w[i] * Math.cos(2 * Math.PI * h * i / wp); im += w[i] * Math.sin(2 * Math.PI * h * i / wp); }
        mags.push(Math.hypot(re, im));
      }
      const mx = Math.max(...mags, 1e-9);
      for (const m of mags) harmonics.push(m / mx);
      times.push(now - (frames - 1 - f) * 1800);
    }
    return {
      generation: 1, source: WT.get("wt_source"), station: alaska ? "CMO" : "BOU",
      place: alaska ? "College, Alaska" : "Boulder", provider: "USGS Geomag", origin: "factory",
      fetchedAt: 0, frames, wavePoints: wp, harmonicCount: hc, rawPoints: rp, wave, harmonics, raw, times,
      rawMin: alaska ? 13380 : 20850, rawMax: alaska ? 13640 : 20910, rawUnit: "nT", rawAxis: "H component", isSpectrum: false,
    };
  }

  let gen = 1;
  function sendTable() {
    const src = WT.get("wt_source");
    const t = src >= 4 ? makeGeomagTable(src === 5) : makeTable();
    t.generation = gen++;
    WT.dispatch("table", t);
  }

  for (const id of ["wt_source", "wt_spread", "wt_tilt", "wt_contrast", "wt_phase"])
    WT.onParam(id, () => setTimeout(sendTable, 0));

  const t0 = performance.now();
  let step = 0, lastStep = -1, hits = 0;
  const lfoPhase = [0, 0];
  let last = t0;

  function shape(s, p) {
    switch (s) {
      case 0: return Math.sin(2 * Math.PI * p);
      case 1: { const q = (p + 0.25) % 1; return 1 - 4 * Math.abs(q - 0.5); }
      case 2: return 2 * p - 1;
      case 3: return p < 0.5 ? 1 : -1;
      default: return Math.sin(2 * Math.PI * p) * 0.6 + Math.sin(5.3 * p) * 0.3;
    }
  }

  function tick() {
    const now = performance.now();
    const dt = (now - last) / 1000;
    last = now;
    const t = (now - t0) / 1000;
    for (let l = 0; l < 2; l++) {
      lfoPhase[l] = (lfoPhase[l] + dt * WT.get(`lfo${l + 1}_rate`) * 40) % 1;
    }
    const lfo1 = shape(WT.get("lfo1_shape"), lfoPhase[0]);
    const lfo2 = shape(WT.get("lfo2_shape"), lfoPhase[1]);
    const stepsPerSec = WT.get("bpm") / 60 * 4;
    step = Math.floor(t * stepsPerSec) % WT.get("eu_steps");
    if (step !== lastStep) { lastStep = step; hits++; }
    const spectrum = [];
    for (let b = 0; b < 120; b++) {
      const f = 20 * Math.pow(1000, b / 120);
      let db = -30 - 18 * Math.log10(f / 200) * (f > 200 ? 1 : -0.4) + 6 * Math.sin(b * 0.7 + t * 2) * 0.5;
      if (Math.abs(f - 220) < 30 || Math.abs(f - 440) < 40 || Math.abs(f - 660) < 50) db += 14;
      spectrum.push(Math.max(-100, db - (f > WT.get("flt_cutoff") ? 12 * Math.log2(f / WT.get("flt_cutoff")) : 0)));
    }
    const env = 0.75 + 0.2 * Math.sin(t * 0.7);
    WT.dispatch("telemetry", {
      pos: Math.min(1, Math.max(0, WT.get("osc_pos") + 0.5 * WT.get("lfo1_amt") * lfo1)),
      ampEnv: env, fltEnv: 0.35 + 0.25 * Math.sin(t * 0.4),
      cutoff: WT.get("flt_cutoff") * Math.pow(2, 0.6 + 0.4 * Math.sin(t * 0.5)),
      lfo1, lfo2, lfo1Phase: lfoPhase[0], lfo2Phase: lfoPhase[1],
      peakL: 0.5 + 0.2 * Math.sin(t * 3.1), peakR: 0.48 + 0.2 * Math.sin(t * 2.7),
      gr: 0.92 + 0.08 * Math.sin(t * 1.3), verb: 0.2 + 0.1 * Math.sin(t), fade: 1,
      step, hits, voices: 4, bpm: WT.get("bpm"), notes: [50, 57, 64, 65], spectrum,
    });
    requestAnimationFrame(tick);
  }

  // index.html#trig_mode=1&flt_cutoff=600 presets parameters (for screenshots/reviews);
  // #status=offline shows the feed as it looks when the network is blocked.
  let mockStatus = "live";
  for (const kv of (location.hash || "").slice(1).split("&")) {
    const [k, v] = kv.split("=");
    if (k === "status") mockStatus = v;
    else if (k && WT.META[k] !== undefined) WT.set(k, parseFloat(v));
  }

  window.addEventListener("load", () => {
    sendTable();
    const now = Date.now() / 1000;
    WT.dispatch("status", mockStatus === "offline"
      ? { state: "offline", message: "Offline (no connection) · playing cache data", lastSuccess: 0, nextFetch: now + 60, now }
      : { state: "live", message: "NOAA NDBC 46026", lastSuccess: now - 180, nextFetch: 0, now });
    requestAnimationFrame(tick);
  });
})();
