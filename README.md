# Worldtable — an ambient wavetable synth that listens to the planet

> Working title. A JUCE/C++ VST3/AU/Standalone plugin that pulls live data from
> public scientific APIs (seismics, space weather, oceans, atmosphere …),
> transforms it into wavetables and slow modulation, and renders it as an
> endless, comfortable ambient drone. "Hearing the world."

This folder is the **spec**. It is written to be read by you *and* by a coding
agent. Nothing here is code yet; it is the contract the code must fulfil.

## The three production phases

| Phase | Goal | Output | Spec file |
|---|---|---|---|
| 1. Define | Decide what the synth does, which data it hears, how data becomes sound | Filled-in functional spec + parameter contract | `spec/01-functional-spec.md`, `spec/02-data-sources.md`, `spec/parameters.yaml` |
| 2. Mock | Make the UI visible before any DSP exists, so intent is unambiguous | Clickable HTML mock (later the real UI) | `spec/05-ui-mock.md` |
| 3. Build | Implement modules behind fixed interfaces, assemble, test, ship | Working plugin, passing pluginval | `spec/03-architecture.md`, `spec/04-dsp-engine.md`, `spec/06-implementation-plan.md` |

How to run all of this with agents: `spec/07-agentic-workflow.md`.
Agent rules of the road: `CLAUDE.md`.

## Proposed repository layout

```
worldtable/
├── CLAUDE.md                     # rules every agent session reads first
├── README.md                     # this file
├── CMakeLists.txt                # JUCE via CPM/FetchContent, pinned tag
├── cmake/
├── spec/                         # ← THE SPEC (this folder)
│   ├── 00-vision.md
│   ├── 01-functional-spec.md
│   ├── 02-data-sources.md
│   ├── 03-architecture.md
│   ├── 04-dsp-engine.md
│   ├── 05-ui-mock.md
│   ├── 06-implementation-plan.md
│   ├── 07-agentic-workflow.md
│   ├── parameters.yaml           # single source of truth for all parameters
│   ├── decisions/                # ADRs: one file per irreversible choice
│   └── tasks/                    # one file per agent task (generated in Phase 3)
├── mock/                         # Phase 2: HTML/CSS/JS UI mock
├── source/
│   ├── PluginProcessor.{h,cpp}
│   ├── PluginEditor.{h,cpp}
│   ├── data/                     # network, parsing, caching — NEVER audio thread
│   │   ├── DataSource.h          # interface
│   │   ├── sources/              # one file per API adapter
│   │   ├── DataScheduler.{h,cpp}
│   │   ├── DataCache.{h,cpp}
│   │   └── WorldSnapshot.h       # immutable, lock-free handoff to audio
│   ├── transform/                # normalise, smooth, map → mod signals & tables
│   │   ├── Normaliser.{h,cpp}
│   │   ├── WavetableBuilder.{h,cpp}
│   │   └── MappingMatrix.{h,cpp}
│   ├── dsp/                      # real-time code only
│   │   ├── WavetableOsc.{h,cpp}
│   │   ├── Voice.{h,cpp}
│   │   ├── DroneEngine.{h,cpp}
│   │   ├── SlowLfo.{h,cpp}
│   │   ├── ModMatrix.{h,cpp}
│   │   ├── Filter.{h,cpp}
│   │   └── fx/ (Chorus, Delay, Reverb)
│   ├── params/                   # generated from spec/parameters.yaml
│   └── ui/                       # WebView UI (grows out of mock/)
├── tests/
│   ├── unit/                     # Catch2: DSP + transforms, deterministic
│   ├── fixtures/                 # recorded API responses (JSON/CSV)
│   └── golden/                   # reference renders for regression
├── tools/
│   ├── gen_params.py             # parameters.yaml → C++ + JS
│   ├── record_fixtures.py        # snapshot real API responses into fixtures
│   └── render_offline.cpp        # headless render to WAV for tests/listening
└── .github/workflows/            # build mac/win, unit tests, pluginval
```
