# 03 — Architecture

One Sky Model, three renderings (ADR-0002). The model is clocked by audio
sample time (ADR-0007, proposed) and publishes an immutable `SkySnapshot`
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
```cpp
struct WorldSnapshot {                       // immutable once published
    static constexpr int kChannels = 6;      // geomag.h_dev, kp, sw.speed, sw.density, sw.bz, sw.by
    static constexpr int kCapacity = 4320;   // 72 h at 1 min
    double startUtc, stepSeconds;            // track grid
    int    length;
    std::array<std::array<float, kCapacity>, kChannels> value;   // physical units
    std::array<std::array<uint8_t, kCapacity>, kChannels> valid;
    StationId station;
    uint64_t sequence;
};
```
Published by the data thread, picked up by the audio thread once per block
(mechanism: ADR-0003, triple buffer preferred). Pre-allocated pool; a new track
never allocates on the audio thread. The model interpolates the track at its
current model time.

### B. SkySnapshot — Sky Model → sound, UI, OSC
```cpp
struct SkySnapshot {                         // fixed-size POD, written after each model step
    double   modelTimeUtc;
    uint64_t step;                           // model step counter
    SkyPhase phase;  float phaseProgress;    // quiet / growth / onset / recovery
    float    energy, activity, kp, bz, speed, density;
    int      nodeCount;                      // 64…128
    std::array<CurtainNode, kMaxNodes> nodes;// displacement, velocity, brightness, red, green, violet
    float    ovalLatitude;                   // curtain's geomagnetic latitude (equatorward drift)
    std::array<float, 2> pulsation;          // slow brightness pulse, field-line ringing (Pc)
    uint32_t onsetCount, whistlerCount;      // event counters (readers detect edges)
    Astronomy sky;                           // sun alt/az, moon alt/az/phase (low rate)
};
```
Written by the audio thread into a triple buffer; the UI and OSC threads read
the newest one. `SkySnapshot.h` is a protected interface (`CLAUDE.md` rule 6).

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
dsp/        ResonatorBank, Exciter, Harmony (scales, chords, progression,
            voice-leading), Sparkles (event voices, whistlers), Drift (tape wobble),
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
