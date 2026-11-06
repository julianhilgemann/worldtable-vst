# 07 — Implementation plan (v1: Sky Model)

Each milestone ends with something you can **hear or see**. Each task becomes
a file `spec/tasks/Tx.y-<slug>.md` (template in `08-agentic-workflow.md`).
Build order from ADR-0002: prototype → C++ Sky Model and resonators →
plug-in renderer → OSC.

## Where we are (2026-11-06)
| Work | State |
|---|---|
| T0.1 skeleton: CMake + JUCE + Catch2, CI macOS + Windows | merged (#1); Windows CI still red (T0.4), tests not in CI (T0.3) |
| TP.1 + TP.2 POC: data → wavetable → synth → FX → WebView UI, ambient shader | merged |
| M1: storm days, Sky Model sketch, sound sketch | merged |
| M2: Sky Model in C++ (T2.1–T2.5) | merged (#7–#11); M2 complete |
| M3: T3.1 resonator bank + exciter | merged (#12) |
| M3: T3.2 harmony (vocabulary, chord hold, scales, MIDI latch) | merged (#13) |
| M3: T3.3 progression (the sky writes the chords) | merged (#14) |
| M3: T3.5a FX I (AutoGain, DC, render chain) | merged (#15) |
| M3: T3.5b FX II (Phaser, Chorus, shimmer fifth) | merged (#16) |
| M3: T3.6 the instrument: SkySound + FX in the plug-in, parameters v1, chord readout | merged |
| M6 (pulled forward): T6.1 the v1 UI: dome, instrument layer, astronomy (T6.1–T6.3) | merged |
| M3: T3.6b raw-field wavetable layer | after T3.4 |
| M3: T3.4 sparkles/whistlers/Breath | after (needs the owner's design/ear) |
| M6: T6.4 UI cost pass | open |

**Reused from the POC:** CMake/CI, `gen_params.py`, `WorldtableRender`,
`WorldtableUICheck`, WebView bridge and mock backend, Drive, SVF, LFO, ADSR,
delay, shimmer reverb, limiter, geomag parser and CMO factory data, cache,
table exchange (raw-field layer).
**Retired (T3.6):** Euclid, `wavespec` source and builder B, the POC voice (`Engine`,
`Voice`, LFOs, envelopes), the world-data feed and its UI panels, wavetable as the main
voice, the yellow-on-navy look and the TP.2 ambient background (T6.1), the ambient-level switcher.

## M0 — Foundations (in parallel with M1)
- T0.3 ctest + pluginval in CI.
- T0.4 Windows: make CI green; WebView2 (new dependency, owner approval).
- T0.5 ADR-0004 (WebView UI) from the POC evidence.

## M1 — Sky Model prototype in the browser
- **T1.1** Storm-day dataset: 10–11 May 2024 (+ a quiet and a moderate day):
  CMO H, Kp, solar wind (v, n, Bz) → `resources/sky/` + `tests/fixtures/`.
  Verifies the historical endpoints in `02-data-sources.md`.
- **T1.2** Sky Model sketch (JS): reservoir, curtain, pulsations,
  assimilation, observer projection, dome view; replays a stored day at
  any speed; exports golden traces.
- **T1.3** Sound sketch (Web Audio): resonator bank + exciter + chord hold
  and progression on the sketch's snapshots; 5-minute Journey bounces; A/B
  against the references with the owner.
**See / hear:** the 10 May 2024 storm as a 60-minute set in a browser tab.
Exit: owner signs off the look and the sound direction; mapping recorded in
`04`/`05`.

## M2 — Sky Model in C++
- T2.1 `SkySnapshot.h` + `SkyModel` interface, header-first review.
- T2.2 The slow sky: track preparation, seeded RNG, sun and MLT, reservoir,
  phases, oval, assimilation; `SkyTimeline` matches the slow goldens. (Re-cut
  in T2.2: the slow step needs all of these at once.)
- T2.3 `SkyModel`: step, seek, speed and snapshot; curtain, pulsations,
  whistlers, events.
- T2.4 Determinism and block-size tests; hand-offs (ADR-0003); the model runs
  in `processBlock`.
- T2.5 Moon, observer projection (az/el, magnetic zenith), snapshot astronomy.
- Matches the T1.2 goldens within tolerance.
**See:** telemetry in the existing UI shows the replayed storm.

## M3 — Sound
- T3.1 ResonatorBank + Exciter (double-banked, click-free retune).
- T3.2 Harmony: chord hold, scales, voicing, MIDI latch, transpose/register.
- T3.3 Progression engine: Mood, Progression amount, sky dramaturgy, voice-leading.
- T3.4 Layers (red/green/violet), sparkles, whistlers, drift and tape wobble, Breath.
- T3.5 FX chain in spec order (new Phaser, Chorus/Ensemble; tape delay;
  shimmer at octave and fifth), AutoGain.
- T3.6 Retire the POC voice; `dsp::Instrument` (SkySound + FX + MIDI latch) in the
  plug-in; `parameters.yaml` v1; chord readout. (Done; the raw-field wavetable layer moved
  to T3.6b.)
**Hear:** the plug-in plays the storm day, in a DAW and in TouchDesigner.

## M4 — Modulation and autopilot
- T4.1 Slow LFOs ×4, random walkers ×2, env follower, sky channels.
- T4.2 ModMatrix: 16 slots, curve, via, mod-of-mod.
- T4.3 Triggers with probability.
- T4.4 Autopilot: Live / Journey / Freeze; state persistence.

## M5 — The world, live
- T5.1 `DataSource.h` + adapters (`geomag`, `kp`, `solarwind`; GFZ and OMNI
  for history) with fixtures.
- T5.2 Scheduler, cache, track builder, bundled storm days.
- T5.3 Freeze into plugin state; offline-render rule.
**Hear:** today's sky over College, Alaska.

## M6 — UI
Pulled forward after M3 on the owner's review of the T3.6 shell (2026-11-06).
- T6.1 Dome renderer (WebGL) fed by `SkySnapshot`. **Done, with T6.2 and T6.3 in one task.**
- T6.2 Instrument layer: header readouts, magnetogram, macros, ticker, tabs. (in T6.1)
- T6.3 Astronomy layer: sun, moon, stars, day shimmer, toggles. (in T6.1)
- T6.4 UI cost pass (redraw on change, GPU budget).

## M7 — OSC and TouchDesigner
- T7.1 `OscOut` (address map in `03-architecture.md`).
- T7.2 Example TouchDesigner project: VST3 + OSC in, fulldome output.

## M8 — Ship (open source)
- Presets (~10), About/credits, `LICENSE` (ADR-0005), README.
- Signing and notarisation (macOS), installer.
- Performance pass (< 5 % CPU audio), host matrix: TouchDesigner, Ableton,
  Bitwig, Logic, Reaper.
- Write-up of the modelling story (data → Sky Model → sound and light) for HyperAdapted.

## Later
- More stations (Tromsø …), ambisonic/binaural output, CLAP, MPE.
- Sister plug-ins on the same frame (ocean, finance): `spec/parked/`.
