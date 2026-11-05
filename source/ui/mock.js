/* Browser-only mock backend: a synthetic Sky Model replay and a chord cycle, so
 * the UI can be developed and reviewed without a DAW. Inactive inside the plug-in. */
(function () {
  "use strict";
  if (!window.WT.isMock) return;
  const WT = window.WT;

  const STORM_UTC = Date.UTC(2024, 4, 10, 17, 20, 0) / 1000;
  const SPEEDS = 24;

  // a few chords of the bright vocabulary, voiced roughly like SkySound voices them
  const CHORDS = [
    { label: "Dmaj7sus2", roman: "I", root: 50, tones: [0, 2, 7, 11] },
    { label: "B♭maj7", roman: "♭VI", root: 58, tones: [0, 4, 7, 11] },
    { label: "Gm add9", roman: "iv", root: 55, tones: [0, 3, 7, 14] },
    { label: "Aadd9", roman: "V", root: 57, tones: [0, 4, 7, 14] },
  ];

  function voicesOf(chord, l) {
    const out = [];
    const lo = [31, 50, 72][l], hi = [49, 74, 90][l];
    const tones = l === 0 ? [chord.tones[0], chord.tones[2] ?? 7] : chord.tones;
    for (let i = 0; i < 8; i++) {
      const t = tones[i % tones.length];
      let n = chord.root + t + 12 * Math.floor(i / tones.length);
      while (n < lo) n += 12;
      while (n > hi) n -= 12;
      out.push(n);
    }
    return out;
  }

  function dispatchTelemetry(t, chord, phase) {
    const spectrum = [];
    for (let b = 0; b < 120; b++) {
      const f = 20 * Math.pow(1000, b / 120);
      let db = -34 - 15 * Math.log10(f / 200) * (f > 200 ? 1 : -0.4) + 5 * Math.sin(b * 0.7 + t * 0.7) * 0.5;
      if (Math.abs(f - 220) < 30 || Math.abs(f - 330) < 40 || Math.abs(f - 440) < 50) db += 13;
      if (phase === 2) db += 3 * Math.exp(-Math.pow((b - 80) / 18, 2));
      spectrum.push(Math.max(-100, db));
    }
    const env = 0.6 + 0.25 * Math.sin(t * 0.8);
    WT.dispatch("telemetry", {
      peakL: env * (0.75 + 0.1 * Math.sin(t * 2.7)),
      peakR: env * (0.73 + 0.1 * Math.sin(t * 2.3)),
      gr: 0.94 + 0.06 * Math.sin(t * 1.1),
      autoGain: 0.9,
      verb: 0.18 + 0.08 * Math.sin(t * 0.9),
      spectrum,
      sky: {
        day: "2024-05-10",
        utc: STORM_UTC + (t * SPEEDS),
        speed: SPEEDS,
        phase: ["quiet", "growth", "onset", "recovery"][phase],
        kp: 5.2 + 1.6 * Math.sin(t * 0.05),
        onsets: 3,
        energy: 0.4 + 0.4 * Math.sin(t * 0.1),
        activity: 0.5,
      },
      chord: {
        label: chord.label,
        roman: chord.roman,
        latched: false,
        pending: false,
        voices: 8,
        changes: Math.floor(t / 8),
        notes: [voicesOf(chord, 0), voicesOf(chord, 1), voicesOf(chord, 2)],
      },
    });
  }

  let mockStatus = "live";
  for (const kv of (location.hash || "").slice(1).split("&")) {
    const [k, v] = kv.split("=");
    if (k === "status") mockStatus = v;
    else if (k && WT.META[k] !== undefined) WT.set(k, parseFloat(v));
  }

  const t0 = performance.now();
  function tick() {
    const t = (performance.now() - t0) / 1000;
    const phase = Math.floor((t / 8) % 4);
    const chordIndex = Math.floor(t / 8) % CHORDS.length;
    dispatchTelemetry(t, CHORDS[chordIndex], phase);
    requestAnimationFrame(tick);
  }

  window.addEventListener("load", () => requestAnimationFrame(tick));
})();
