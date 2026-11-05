# Worldtable — a drone that plays Earth's magnetic field

> Working title. A JUCE/C++ VST3 / AU / Standalone instrument that plays
> Earth's magnetic field as the sun shakes it: live, slow, never repeating,
> with an interface that *is* the aurora. Open source, built for
> TouchDesigner, DAWs and long listening.

**How it works.** Live magnetometer and solar-wind data (USGS, NOAA) nudge a
generative **Sky Model**: an aurora curtain that loads energy from the solar
wind and releases it in substorms. The same model state is *heard* (a modal
resonator bank tuned to a slowly progressing chord), *seen* (an all-sky dome
above a magnetometer station in Alaska) and *sent* (OSC to TouchDesigner).

**Status (2026-11-06).** M3's instrument is playable in a DAW: the Sky Model
replays the 10–11 May 2024 storm, the curtain is heard through the resonator
bank, the slow progression writes the chords, and the spec's FX chain
(Drive → Phaser → Chorus → tape delay → shimmer reverb → limiter) runs in the
plug-in. `parameters.yaml` is the v1 set and the editor shows the chord. Next:
sparkles/whistlers/Breath (T3.4), the dome UI (M6), live data (M5); see
`spec/07-implementation-plan.md`.

## Spec
Read in this order: `spec/00-vision.md` → `spec/03-architecture.md` → the
topic you work on → the task file.

| File | What |
|---|---|
| `spec/00-vision.md` | what it is, principles, non-goals, targets |
| `spec/01-functional-spec.md` | what it does, open `DECIDE:` questions |
| `spec/02-data-sources.md` | magnetosphere sources, adapter contract |
| `spec/03-architecture.md` | threads, hand-offs, module map, OSC map |
| `spec/04-sky-model.md` | the generative aurora model |
| `spec/05-sound-engine.md` | resonators, harmony, modulation, FX |
| `spec/06-ui.md` | visual identity, dome, layout |
| `spec/07-implementation-plan.md` | milestones M0–M8 |
| `spec/08-agentic-workflow.md` | how ideas become tasks and PRs |
| `spec/decisions/` | ADRs |
| `spec/parked/` | research for sister plug-ins |

Agent rules: `CLAUDE.md`.

## Build and try (macOS)
```sh
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
cmake --preset release -DWORLDTABLE_COPY_PLUGINS=ON && cmake --build --preset release   # installs AU + VST3
open build/release/Worldtable_artefacts/Release/Standalone/Worldtable.app
build/release/WorldtableRender_artefacts/Release/WorldtableRender --seconds 120 --out renders/x.wav
build/release/WorldtableRender_artefacts/Release/WorldtableRender --sky --from 17:20 \
  --seconds 120 --progression 1 --out renders/sky.wav      # the validation path
open source/ui/index.html            # UI with mock data, no build needed
```
`--set progression=0 --set mood=Bittersweet --set drive=0.3` sets any parameter by
its real value. More options (UI from disk, `WorldtableUICheck`): `spec/tasks/T3.6-instrument.md`.

## Repository layout
```
CLAUDE.md                 rules every agent session reads first
spec/                     the spec (see above), parameters.yaml, tasks/
prototype/sky/            browser sketch of the Sky Model (M1, plain JS)
source/
  PluginProcessor.*, PluginEditor.*
  data/                   network, parsing, caching — never the audio thread
  sky/                    Sky Model (M2): reservoir, curtain, observer, astronomy
  dsp/                    real-time code: resonators, harmony, mod, fx/
  osc/                    OSC out to TouchDesigner (M7)
  transform/              raw-field wavetable builder (kept for T3.6b)
  params/                 generated from spec/parameters.yaml — never hand-edit
  ui/                     WebView UI (HTML/CSS/JS) + mock backend
resources/                bundled data (factory world, storm days)
tests/unit/, tests/fixtures/
tools/                    gen_params.py, render/ (WorldtableRender), uicheck/, host/
```
`sky/`, `osc/` and `prototype/` arrive with their milestones.

## Data credits
USGS Geomagnetism Program; NOAA Space Weather Prediction Center; GFZ Potsdam
(Kp, CC BY 4.0); NASA OMNI. Sources and licences: `spec/02-data-sources.md`.
