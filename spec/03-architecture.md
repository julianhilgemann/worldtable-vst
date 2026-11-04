# 03 — Architecture

## Threads and ownership

| Thread | Owns | May do | May never do |
|---|---|---|---|
| **Audio** (`processBlock`) | Voices, DSP state, current snapshot pointer | read atomics, read tables, math | allocate, lock, I/O, wait |
| **Data** (`DataScheduler`, `juce::Thread`) | Adapters, HTTP, cache | network, disk, parse, normalise | touch DSP objects |
| **Builder** (`juce::ThreadPool`, 1 worker) | WavetableBuilder | FFT, allocate new tables | write into a table the audio thread is reading |
| **Message/UI** | Editor, parameter attachments | show data, edit mappings | block on network |

## The two hand-offs (the core of the design)

### A. WorldSnapshot — data → audio
```cpp
struct WorldSnapshot {                    // immutable once published
    std::array<float, kMaxChannels> value;     // normalised 0..1, pre-slew
    std::array<uint8_t, kMaxChannels> valid;
    double fetchedAtUtc;
    uint64_t sequence;
};
```
- Data thread builds a new snapshot, publishes with
  `std::atomic<std::shared_ptr<const WorldSnapshot>>` *or* a triple-buffer
  (preferred: no refcount work on audio thread — see ADR-0003).
- Audio thread reads the latest once per block; per-sample **slew** happens in DSP.

### B. Wavetable swap — builder → audio
- Each oscillator slot holds two `WavetableSet` buffers: `active`, `pending`.
- Builder fills `pending` (pre-allocated pool, no allocation at swap time),
  sets an atomic flag. Audio thread sees the flag at block start and begins a
  crossfade (≥ 1 s) from old to new; old buffer returned to pool after fade.

## Module map

```
data/      DataSource (iface) ← UsgsQuake, NoaaKp, NoaaSolarWind, OpenMeteo, …
           DataScheduler  — timers, HTTP (juce::URL/WebInputStream), backoff
           DataCache      — disk history, factory world, freeze
           Normaliser     — per-channel strategies (pure)
transform/ MappingMatrix  — channel → D-slot binding (UI-editable)
           WavetableBuilder — history/snapshot → WavetableSet (+ mipmaps)
dsp/       WavetableOsc, Voice, DroneEngine, SlowLfo, ModMatrix, Filter, fx/*
params/    generated APVTS layout + ID constants (from parameters.yaml)
ui/        WebView editor ↔ APVTS relay; data inspector view
```

## State & persistence
- `AudioProcessorValueTreeState` for all automatable params.
- Extra `ValueTree` children: mappings, world focus, frozen snapshot, table seeds.
- Versioned state (`stateVersion` attribute) with migration function from day one.

## UI technology (see ADR-0004)
Recommended: **JUCE 8 WebView UI**. The Phase-2 HTML mock evolves directly into
the shipping UI; parameters bridge via JUCE's WebView relays. Fallback: native
JUCE components if WebView proves unstable in a target host.

## Testing architecture
- Pure units (`parse`, `Normaliser`, `WavetableBuilder`, `SlowLfo`) → Catch2.
- `tools/render_offline` → headless processor, renders N seconds from a fixture
  world to WAV; tests assert no NaN, no clicks (max sample-delta threshold),
  loudness window, CPU budget.
- Golden renders compared by spectral distance, not bit-exactness.
- `pluginval` in CI for host-robustness.
