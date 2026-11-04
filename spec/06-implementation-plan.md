# 06 — Implementation plan (Phase 3)

Each milestone ends with something you can **hear or click**. Each task becomes
a file `spec/tasks/Tx.y-<slug>.md` (template in `07-agentic-workflow.md`).

## M0 — Skeleton (½–1 day)
- T0.1 CMake + JUCE (pinned tag) + Catch2; builds VST3/AU/Standalone; CI on macOS + Windows.
- T0.2 `gen_params.py`: YAML → `ParamIDs.h`, `createLayout()`, `params.js`.
- T0.3 pluginval + ctest wired in CI.
**Hear:** silence, but the plugin loads in your DAW.

## M1 — A sound without the world (2–4 days)
- T1.1 `WavetableOsc` with mipmaps + factory tables.
- T1.2 `Voice`, `DroneEngine` (Listen mode), long envelopes.
- T1.3 `SlowLfo` ×4, `ModMatrix` (LFO sources only).
- T1.4 Filter + FX chain (chorus, delay, FDN reverb, limiter).
- T1.5 `render_offline` tool + `NoClicks`/`NoNaN` tests.
**Hear:** a beautiful static drone. *If this isn't lovely, the data won't save it.*

## M2 — The world, offline (2–3 days)
- T2.1 `DataSource` interface + 3 adapters (`quake`, `kp`, `weather`) with fixture tests.
- T2.2 `Normaliser` strategies + tests.
- T2.3 `DataCache` + factory world (recorded 30 days).
- T2.4 `WorldSnapshot` hand-off; D1…D8 as mod sources; replay fixture history at ×1000 speed.
**Hear:** recorded world modulating the drone.

## M3 — The world, live (2–3 days)
- T3.1 `DataScheduler`: intervals, backoff, timeouts, offline detection.
- T3.2 Remaining adapters (solar wind, ocean, tides, CO₂, sunspots, ISS).
- T3.3 Freeze world into plugin state; offline-render rule.
**Hear:** today's planet.

## M4 — World tables (3–5 days)
- T4.1 `WavetableBuilder` A (series-as-cycle) and B (harmonics).
- T4.2 Pending/active swap with crossfade; pool allocation.
- T4.3 (stretch) Builder C spectral morph + time-scale.
**Hear:** the timbre itself is made of data.

## M5 — UI (3–6 days)
- T5.1 Port `mock/` into WebView editor; parameter relays.
- T5.2 Wavetable 3D view, data sparklines, event ticker.
- T5.3 Drag-to-modulate.

## M6 — Polish & ship
- Presets ×10, About/credits, licences, signing/notarisation (macOS), installer.
- Performance pass (< 5 % CPU), host matrix test (Ableton, Bitwig, Logic, Reaper).

## Stretch
- Event voices (quakes/flares trigger soft bells/granular grains).
- OSC out of D-channels → TouchDesigner visuals in sync with the sound.
- CLAP format; MPE.
