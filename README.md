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

**Status (2026-11-05).** The proof of concept (wavetable synth fed by ocean
and magnetometer data, WebView UI) works end to end on macOS (PR #2). The v1
direction above is specified; next is the Sky Model prototype in the browser
(`spec/tasks/T1.1-*`, `T1.2-*`).

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
build/release/WorldtableRender_artefacts/Release/WorldtableRender --out renders/x.wav --seconds 45
open source/ui/index.html            # UI with mock data, no build needed
```
More options (live fetch, UI from disk, UI check): `spec/tasks/TP.1-poc-sound.md`.

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
  transform/              raw-field wavetable builder
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
