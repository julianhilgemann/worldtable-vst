/* Browser-only mock backend (spec/06 "Mock first", T6.1): the T1.2/T1.3 sketch *is* the mock.
 * It imports the sketch's Sky Model and Harmony from prototype/sky/, replays a recorded day on
 * real time and posts the same "telemetry" and "day" events the plug-in's editor sends. Inside
 * the plug-in the mock is inactive and nothing is imported.
 *
 * Serve the repo root (python3 -m http.server 8765) and open /source/ui/. The URL hash picks the
 * moment, like the sketch: #day=2024-05-10&t=2024-05-11T08:00&speed=24 */
(function () {
  "use strict";
  const WT = window.WT;
  if (!WT || !WT.isMock) return;

  const hash = new URLSearchParams(location.hash.slice(1));
  const DAY = hash.get("day") || "2024-05-10";
  const START = { "2024-05-10": "2024-05-10T17:20", "2025-01-19": "2025-01-19T09:30", "2025-01-25": "2025-01-25T09:00" };
  const SPEED = Number(hash.get("speed")) || 24;
  const base = new URL("../../prototype/sky/", location.href).href;

  Promise.all(["data.js", "model.js", "observer.js", "sound/harmony.js"].map((f) => import(base + f)))
    .then((mods) => start(...mods))
    .catch((e) => console.warn("mock: the sketch modules are not reachable (serve the repo root)", e));

  async function start(D, M, O, H) {
    const observer = new O.Observer();
    const track = await D.loadDay(DAY);
    const startMs = Date.parse((hash.get("t") || START[DAY] || START["2024-05-10"]) + "Z");
    const model = new M.SkyModel(track, { seed: 1, startMs, speed: SPEED, observer });

    // ---- the day chart, as SkyFeed builds it from the timeline
    const trace = M.runSlow(track, { seed: 1, observer });
    const st = observer.station;
    WT.dispatch("day", {
      day: DAY, sequence: 1, t0: track.startMs / 1000, step: 60,
      station: { code: st.code, name: st.name, lat: st.lat, lon: st.lon, elevKm: st.elevKm, mlat: observer.mlat, mlon: observer.mlon,
        poleLat: O.DIPOLE_POLE.lat, poleLon: O.DIPOLE_POLE.lon, mzAz: observer.magZenith.az, mzEl: observer.magZenith.el },
      dh: Array.from(trace.dh), dhModel: Array.from(trace.dhModel), coupling: Array.from(trace.coupling),
      energy: Array.from(trace.energy), threshold: Array.from(trace.threshold), phase: Array.from(trace.phase),
      onsets: trace.events.filter((e) => e.type === "onset").map((e) => ({ utc: e.t / 1000, kind: e.kind, strength: e.strength })),
      // T6.10: the keogram's inputs, two decimals as the editor sends them
      ...Object.fromEntries(["eqLat", "poleLat", "activity", "gain", "unloading", "stormRed", "mlt", "kp"]
        .map((k) => [k, Array.from(trace[k], (v) => Math.round(v * 100) / 100)])),
      stationMlat: Math.round(observer.mlat * 100) / 100, dayShift: M.DEFAULTS.dayShiftDeg, arcPosition: M.DEFAULTS.arcPosition,
    });

    // ---- harmony: the sketch's progression, voiced like SkySound (5 slots per layer) in the bands
    // (T3.8: dsp::Voicing's clampEdges, bandRange, bandSlots, without the per-voice spread)
    const FN = ["home", "tension", "awe", "borrowed"];
    const harmony = new H.Harmony({ seed: 1, mood: H.MOODS[Math.round(WT.get("mood"))], key: Math.round(WT.get("key")), progression: WT.get("progression") });
    harmony.start(model.snapshot().phase);
    let chord = harmony.chord(), changes = 0, lastTake = 0, slots = null, notes = [[], [], []];
    const TONES_AS = ["root", "red", "green", "violet", "green"];   // Root, Root + fifth, Chord, Colours, All
    function bandEdges() {
      const want = ["band_floor", "band_x1", "band_x2", "band_x3", "band_ceiling"].map((id) => Math.round(WT.get(id)));
      const max = [60, 84, 96, 103, 108], minW = [0, 12, 5, 5, 5], e = [];
      for (let i = 0; i < 5; i++) e.push(i === 0 ? Math.min(Math.max(want[0], 21), 60) : Math.min(Math.max(want[i], e[i - 1] + minW[i]), Math.max(max[i], e[i - 1] + minW[i])));
      return e;
    }
    function voice(c) {
      const e = bandEdges(), reach = Math.round(WT.get("band_blend")), L = ["red", "green", "violet"];
      const next = L.map((name, l) => {
        const as = TONES_AS[Math.round(WT.get(name + "_tones"))], shift = Math.round(WT.get(name + "_shift"));
        const pcs = as === "root" ? Array(H.SLOTS).fill((c.key + c.deg) % 12) : H.layerSlots(c, c.key, as);
        return pcs.map((pc) => (((pc + shift) % 12) + 12) % 12);
      });
      slots = slots ? next.map((s, l) => H.leadSlots(slots[l], s).slots) : next;
      notes = L.map((name, l) => {
        const lo = Math.max(e[0], e[l + 1] - reach), hi = Math.min(e[4] - 1, e[l + 2] - 1 + (l === 2 ? 0 : reach));
        const centre = Math.round((lo + hi) / 2), prev = notes[l];
        return slots[l].map((pc, k) => H.nearestNote(pc, prev[k] ?? centre, lo, hi));
      });
    }
    voice(chord);
    // T6.6: a click on the day strip seeks (T6.10: a drag scrubs), as SkyRuntime::seek does in the plug-in
    window.WT_MOCK_NATIVE = { seek(utc) {
      const before = model.snapshot().phase;
      model.seek(utc * 1000);
      const s = model.snapshot();
      if (s.phase !== before) {
        const minute = Math.floor(s.modelTimeUtc / 60000);
        if (M.PHASE_NAMES[s.phase] === "onset") harmony.onOnset(0.8, minute); else harmony.onPhase(s.phase, minute);
      }
      return true;
    } };
    WT.onParam("key", (v) => harmony.setKey(Math.round(v)));
    WT.onParam("mood", (v) => harmony.setMood(H.MOODS[Math.round(v)]));
    WT.onParam("progression", (v) => harmony.setProgression(v));
    let revoiceTimer = null, armed = false;   // T3.8: a band change re-voices once still for 300 ms
    for (const id of ["band_floor", "band_x1", "band_x2", "band_x3", "band_ceiling", "band_blend", "red_tones", "green_tones",
                      "violet_tones", "red_shift", "green_shift", "violet_shift"])
      WT.onParam(id, () => {
        if (!armed) return;   // onParam calls once at once with the current value
        clearTimeout(revoiceTimer);
        revoiceTimer = setTimeout(() => { voice(chord); changes += 1; }, 300);
      });
    armed = true;

    // ---- real time → model ticks (10 ms), 30 telemetry frames a second
    const t0 = performance.now();
    let ticks = 0;
    const realS = () => (performance.now() - t0) / 1000;
    setInterval(() => {
      const want = Math.floor(realS() * 100);
      const newOnsets = [];
      for (let n = 0; ticks < want && n < 50; n++, ticks++) {
        model.step();
        for (const e of model.takeEvents()) {
          const minute = Math.floor(e.t / 60000);
          if (e.type === "phase") harmony.onPhase(e.phase, minute);
          else if (e.type === "onset") { harmony.onOnset(e.strength, minute); newOnsets.push({ utc: e.t / 1000, kind: e.kind, strength: e.strength }); }
        }
      }
      ticks = Math.max(ticks, want - 50);
      const r = realS();
      harmony.update(r);
      if (harmony.pending && r - lastTake > (harmony.pending.urgent ? 1.5 : 6)) {
        harmony.take(r);
        chord = harmony.chord();
        voice(chord);
        changes += 1;
        lastTake = r;
      }
      WT.dispatch("telemetry", telemetry(model.snapshot(), newOnsets, r));
    }, 33);

    // ---- Play mode (T3.7): there is no MIDI in a browser, so a slow demo phrase stands in
    const PHRASE = [
      { notes: [50, 57, 61, 64, 66], label: "Dmaj9" }, { notes: [47, 54, 61, 62, 64], label: "Bm11" },
      { notes: [43, 50, 54, 59, 61], label: "Gmaj7♯11" }, { notes: [45, 52, 59, 64, 69], label: "Asus2" },
    ];
    const lowNoteOf = (pc) => { const f = bandEdges()[0]; return f + ((((pc - f) % 12) + 12) % 12); };   // from the floor up (T3.8)
    function playDemo(r) {
      const bar = 3, i = Math.floor(r / bar) % PHRASE.length, t = r % bar, held = t < 2.2;
      const rel = Math.max(0.02, WT.get("play_release"));
      const fade = (dt) => Math.pow(10, -3 * dt / rel);
      const cur = PHRASE[i], prev = PHRASE[(i + PHRASE.length - 1) % PHRASE.length];
      const voices = cur.notes.map((n) => [n, held ? 0.85 : 0.85 * fade(t - 2.2), held]);
      for (const n of prev.notes) if (!cur.notes.includes(n)) voices.push([n, 0.85 * fade(t + bar - 2.2), false]);
      const live = voices.filter((v) => v[1] > 0.01);
      const heldNotes = held ? cur.notes : [];
      const lay = [heldNotes.flatMap((n) => [n - 12, n - 12]), heldNotes.flatMap((n) => [n, n + 12]), heldNotes.flatMap((n) => [n + 19, n + 19])];
      return { voices: live, low: lowNoteOf(cur.notes[0]), chord: { play: true, held: heldNotes.length, label: cur.label, roman: "", notes: lay, voices: lay[0].length, changes } };
    }

    function telemetry(s, newOnsets, r) {
      const nd = s.nodes, n = s.nodeCount;
      const act = s.activity, breath = 0.5 - 0.5 * Math.cos(2 * Math.PI * (5 / 60) * r);
      const level = 0.14 + 0.22 * act + 0.05 * Math.sin(r * 0.7);
      const playing = WT.get("play") ? playDemo(r) : null;
      const cut = WT.get("eq4_freq") * Math.pow(2, WT.get("eq_sky") * 3 * Math.min(1, act));   // T3.9: the sky opens the EQ's cut
      const eqCutHz = cut >= 19980 ? 20000 : cut;
      return {
        play: { mix: playing ? 1 : 0, voices: playing ? playing.voices : [] },
        low: { note: playing ? playing.low : lowNoteOf(chord.key + chord.deg), level: WT.get("low_level") },
        eqCutHz, sampleRate: 48000,
        peakL: level * (1 + 0.04 * Math.sin(r * 3.1)), peakR: level * (1 + 0.04 * Math.cos(r * 2.7)),
        gr: level > 0.3 ? 0.96 : 1, autoGain: 1.4, verb: 0.18 + 0.14 * act, breath,
        spectrum: spectrumFor(notes, act),
        sky: {
          day: DAY, utc: s.modelTimeUtc / 1000, speed: SPEED, phase: M.PHASE_NAMES[s.phase], progress: s.phaseProgress,
          kp: s.kp, bz: s.bz, wind: s.speed, dh: s.dh, dhModel: s.dhModel, energy: s.energy, activity: act,
          unloading: s.unloading, gain: s.gain, coupling: s.coupling, density: s.density, mlt: s.mlt, arcLat: s.ovalLatitude,
          onsets: s.onsetCount, whistlers: s.whistlerCount, newOnsets,
        },
        dome: {
          n, mlat: Array.from(nd.mlat.subarray(0, n)), mlon: Array.from(nd.mlon.subarray(0, n)),
          green: Array.from(nd.green.subarray(0, n)), red: Array.from(nd.red.subarray(0, n)), violet: Array.from(nd.violet.subarray(0, n)),
          eqLat: s.eqLat, poleLat: s.poleLat, dayShift: s.dayShiftDeg, stormRed: s.stormRed,
          sunAz: s.sky.sunAz, sunEl: s.sky.sunEl, moonAz: s.sky.moonAz, moonEl: s.sky.moonEl, moonIllum: s.sky.moonIllum,
        },
        chord: playing ? playing.chord : {
          label: chord.label, roman: chord.roman, key: chord.key, function: FN.indexOf(chord.fn), shape: chord.index,
          voices: notes[0].length, bank: changes % 2, latched: false, pending: !!harmony.pending, changes, tick: 0, notes,
          edges: bandEdges(), blend: WT.get("band_blend"),
        },
      };
    }
  }

  /** A plausible analyser picture: a soft slope plus the voiced partials. */
  function spectrumFor(notes, act) {
    const out = [];
    for (let b = 0; b < 120; b++) {
      const f = 20 * Math.pow(1000, b / 120);
      let db = -78 + 10 * Math.exp(-(Math.log2(f / 300) ** 2) / 6) + 8 * act;
      for (const layer of notes) for (const n of layer) {
        const fn = 440 * Math.pow(2, (n - 69) / 12);
        db = Math.max(db, -34 - 6 * Math.abs(Math.log2(f / fn)) * 12);
      }
      out.push(Math.round(db * 10) / 10);
    }
    return out;
  }
})();
