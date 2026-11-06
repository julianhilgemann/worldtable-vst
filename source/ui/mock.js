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
    });

    // ---- harmony: the sketch's progression, voiced like SkySound (5 slots per layer)
    const FN = ["home", "tension", "awe", "borrowed"];
    const harmony = new H.Harmony({ seed: 1, mood: H.MOODS[Math.round(WT.get("mood"))], key: Math.round(WT.get("key")), progression: WT.get("progression") });
    harmony.start(model.snapshot().phase);
    let chord = harmony.chord(), changes = 0, lastTake = 0, slots = null, notes = [[], [], []];
    function voice(c) {
      const reg = WT.get("register_semis") + WT.get("transpose"), spread = WT.get("spread");
      const next = H.LAYERS.map((L) => H.layerSlots(c, c.key, L));
      slots = slots ? next.map((s, l) => H.leadSlots(slots[l], s).slots) : next;
      notes = H.LAYERS.map((L, l) => {
        const [lo, hi] = H.layerRange(L, reg, spread), centre = Math.round((lo + hi) / 2);
        const prev = notes[l];
        return slots[l].map((pc, k) => H.nearestNote(pc, prev[k] ?? centre, lo, hi));
      });
    }
    voice(chord);
    WT.onParam("key", (v) => harmony.setKey(Math.round(v)));
    WT.onParam("mood", (v) => harmony.setMood(H.MOODS[Math.round(v)]));
    WT.onParam("progression", (v) => harmony.setProgression(v));

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

    function telemetry(s, newOnsets, r) {
      const nd = s.nodes, n = s.nodeCount;
      const act = s.activity, breath = 0.5 - 0.5 * Math.cos(2 * Math.PI * (5 / 60) * r);
      const level = 0.14 + 0.22 * act + 0.05 * Math.sin(r * 0.7);
      return {
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
        chord: {
          label: chord.label, roman: chord.roman, key: chord.key, function: FN.indexOf(chord.fn), shape: chord.index,
          voices: notes[0].length, bank: changes % 2, latched: false, pending: !!harmony.pending, changes, tick: 0, notes,
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
