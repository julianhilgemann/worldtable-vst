# ADR-0002 — v1 direction: a magnetosphere drone played by a Sky Model

**Status:** Accepted (owner, phone sessions 1–7, 2026-11-04/05; recorded in the
project brief and moved into the repo on 2026-11-05)
**Supersedes:** the wavetable-first, multi-source direction of the original spec
and the POC (TP.1/TP.2). The planned "ADR-0002 Wavetable generation method" is
no longer needed.

## Context
The POC proved the pipeline (live data → wavetable → synth → FX → WebView UI),
but a wavetable synth fed by many unrelated sources (ocean, quakes, weather …)
has no single story, and the sound and the image disagree. The owner wants one
phenomenon, played as a drone, with an interface that *is* that phenomenon.

## Decision
1. **One phenomenon: the magnetosphere** (geomagnetic field, solar wind, Kp,
   aurora). Ocean and every other source leave v1; ocean may return as a
   sister plug-in on the same frame (`spec/parked/`).
2. **Drone first, MIDI second.** It plays continuously with no input. MIDI is
   optional and only latches root and chord (Chord Hold).
3. **A Sky Model is the single source of truth.** One simplified, generative
   aurora model, nudged by real data, holds the state. Sound, plug-in visuals
   and TouchDesigner (OSC) are three renderings of that state. The data sets
   the mean level; the model adds plausible, seeded detail around it
   (`spec/04-sky-model.md`).
4. **Main voice: a modal resonator bank** driven by the curtain nodes and tuned
   to the held chord in 12-TET. The wavetable engine survives only as an
   optional "raw field" layer (audified magnetometer data).
5. **Cut from v1:** the Euclid sequencer, the ocean source and builder B,
   step sequencing, melodic MIDI play beyond chord latch, per-voice editing,
   a complex preset browser, user-built modules or cables, audio-rate FM,
   microtonal tuning.
6. **Build order:** prototype the Sky Model as a browser sketch replaying a
   recorded storm day → port to C++ and drive the resonator bank → plug-in
   renderer → OSC out.

## Consequences
- The data contract gets simpler: every v1 source is a scalar time series, so
  the scalar `DataReading` contract fits and the open "vector readings"
  question moves to the ocean sister (`spec/02-data-sources.md`).
- New protected interface: `SkySnapshot.h` (the one state every renderer reads).
- POC code to retire or demote: Euclid, `wavespec` source and builder B, the
  wavetable as the main voice, the yellow-on-navy look, the ambient-level
  switcher. Reused: build, codegen, offline render, UI check harness, WebView
  bridge, delay/reverb/limiter, drive, SVF, LFO, ADSR, the geomag parser and
  factory data, the table exchange (for the raw field layer).
- `spec/parameters.yaml` is rewritten for v1 when the C++ engine lands
  (nothing is released, IDs are still free).
