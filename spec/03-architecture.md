# 03 — Architecture

One Sky Model, three renderings (ADR-0002). The model is clocked by audio
sample time (ADR-0007) and publishes an immutable `SkySnapshot`
that the sound, the UI and OSC all read.

## Threads and ownership

| Thread | Owns | May do | May never do |
|---|---|---|---|
| **Audio** (`processBlock`) | Sky Model state, resonators, harmony, mod matrix, FX, current `WorldSnapshot` pointer | step the model every `sampleRate/100` samples, read atomics and tables, math | allocate, lock, I/O, log, wait |
| **Data** (`DataScheduler`, `juce::Thread`) | adapters, HTTP, cache, resampling | network, disk, parse, build tracks | touch model or DSP objects |
| **Builder** (`juce::ThreadPool`, 1 worker) | raw-field `WavetableBuilder` (optional layer) | FFT, allocate tables | write a table the audio thread reads |
| **OSC** (`juce::Thread`, ~60 Hz) | `OscOut` | read the newest `SkySnapshot`, send UDP | touch model state |
| **Message/UI** | editor, parameter attachments, telemetry | read the newest `SkySnapshot`, emit to the web view | block on network |

## The hand-offs (the core of the design)

### A. WorldSnapshot — data → Sky Model
Defined in `source/data/WorldSnapshot.h` (T2.1, protected). Six channels on a
1-minute grid, 72 h capacity, `float`: H deviation from the station's quiet
level, Kp, solar-wind speed, density, Bz, By. Gaps are filled by the data
thread, and `valid` flags mark measured samples. It also carries the station,
the quiet level and a sequence number; about 130 KB.
Published by the data thread, picked up by the audio thread once per block
(mechanism: ADR-0003: a slot exchange, `SlotExchange<T, N>`, the POC's
`TableExchange` pattern). Pre-allocated pool; a new track
never allocates on the audio thread. The model interpolates the track at its
current model time.

With each track (or seed or tuning change) the data thread also builds a
`SkyTimeline` (`source/sky/SkyModel.h`): the slow sky's state at every model
minute. A seek copies one checkpoint and replays at most 59 slow steps, so it
stays bounded on the audio thread.

### B. SkySnapshot — Sky Model → sound, UI, OSC
Defined in `source/sky/SkySnapshot.h` (T2.1, protected; `CLAUDE.md` rule 6).
It is a fixed-size POD of 4.8 KB at 128 nodes, written after each model step,
and contains:
- model time, tick and speed;
- phase and progress, plus energy, activity, unloading, gain and coupling;
- Kp, Bz, wind speed and density, and ΔH measured and modelled;
- MLT, the oval (arc, equatorward and poleward edges, day shift), storm red,
  the Pc phase and the recovery pulse;
- event counters with the last onset (time, strength, node, kind, ΔH/5 min)
  and the last whistler;
- sun and moon, and the observer (station, dipole pole, magnetic zenith);
- per node: displacement, velocity, brightness, green/red/violet, geomagnetic
  latitude and longitude, pulse.
Written by the audio thread into a triple buffer per reader (ADR-0003); the UI
and OSC threads read the newest one, and get each tick's events through a FIFO. The sound runs on the audio thread and reads the model's own
copy (`SkyModel::current()`), together with the events each `step()` returns:
`SkyRuntime::process` calls a `TickListener` (the sound, `dsp/SkySound`) after
every step and then for the samples up to the next tick, so the sound sees each
tick at its sample whatever the block size (T3.1); the same listener feeds the tick's phase and
onset events to `dsp/Harmony`, whose progression requests become chord changes through the
double-banked queue (T3.3).

### C. Raw-field table swap — builder → audio (optional layer)
The POC's `TableExchange`: builder fills a pending `WavetableSet` from a pool,
flags it; the audio thread crossfades over ≥ 1.5 s and returns the old one.

## Determinism
- Same seed + same `WorldSnapshot` track + same sample rate → bit-identical
  `SkySnapshot` sequence, whatever the host block size.
- The model's RNG is a seeded, counter-based generator owned by the model;
  nothing in `source/sky/` reads wall-clock time or `std::random_device`.
- Live mode maps model time to UTC once per block from the host's sample
  counter and the session start; Journey and Freeze never read UTC.

## Module map

```
data/       DataSource (iface), DataRequest, sources/{UsgsGeomag, SwpcKp, SwpcSolarWind,
            GfzKp, OmniSolarWind}, DataScheduler, DataCache, TrackBuilder, WorldSnapshot
sky/        SkyModel (step, reset, seed), Reservoir (loading, substorm phases),
            Curtain (node chain, damped driven wave equation), Pulsations, Events,
            Assimilation (H, Kp nudging), Observer (station, geomagnetic frame,
            dome projection), Astronomy (sun, moon, stars), Rng, SkySnapshot
dsp/        SkySound (the curtain → resonators, per tick; chord double-banking),
            ResonatorBank, Exciter, Voicing (layer slots, ranges, voice-leading),
            Harmony (scales, chords, chord hold, MIDI latch, progression), Sparkles (event voices,
            whistlers), Drift (tape wobble),
            mod/ (SlowLfo, RandomWalk, EnvFollower, ModMatrix, Triggers),
            RawField (POC WavetableOsc + table exchange), AutoGain,
            fx/ (Drive, Phaser, Chorus, TapeDelay, ShimmerReverb, Limiter)
osc/        OscOut (sender thread, address map below) — juce_osc, no new dependency
params/     generated from spec/parameters.yaml
ui/         WebView: dome renderer (WebGL), instrument layer, tabs, mock backend
```

## OSC address map (draft)
| Address | Args | Rate |
|---|---|---|
| `/wt/sky/phase` | int phase, float progress | on change + 1 Hz |
| `/wt/sky/scalars` | energy, activity, kp, bz, speed, density, ovalLat | 60 Hz |
| `/wt/sky/nodes` | blob: N × (displacement, brightness, r, g, v) float32 | 60 Hz |
| `/wt/sky/event` | string type (`onset`, `whistler`), float strength | on event |
| `/wt/sky/astro` | sunAlt, sunAz, moonAlt, moonAz, moonPhase | 1 Hz |
| `/wt/mod/<name>` | float 0..1 per mod source | 60 Hz |
| `/wt/out/env` | float L, float R | 60 Hz |

## State & persistence
- `AudioProcessorValueTreeState` for all automatable parameters.
- Extra `ValueTree` children: station, autopilot mode, Journey day and
  compression, frozen sky (track + model state + seed), sky toggles.
- Versioned state (`stateVersion`) with a migration function; the POC state
  migrates to defaults.

## UI technology
JUCE 8 WebView (POC evidence; ADR-0004 to write). The dome renderer is a WebGL
shader fed by `SkySnapshot` telemetry (node arrays as a small texture). The
same page runs in a normal browser with a mock backend, which is where the
T1.2 prototype lives.

## Testing architecture
- Pure units (`parse`, `TrackBuilder`, `Reservoir`, `Curtain`, `Harmony`,
  `ResonatorBank`, `SlowLfo`) → Catch2, fixtures only.
- **Sky Model goldens:** the T1.2 prototype exports reference traces (phase,
  energy, node brightness) for the 10 May 2024 day; the C++ port must match
  within tolerance.
- `WorldtableRender` → headless processor renders N seconds (or a whole
  Journey) from a fixture world to WAV; tests assert no NaN/inf, no clicks
  (max sample-delta), peak ≤ −1 dBFS, loudness window, CPU budget.
- Determinism and block-size independence tests for `SkyModel`.
- `pluginval --strictness-level 8` and `WorldtableUICheck` in CI.
