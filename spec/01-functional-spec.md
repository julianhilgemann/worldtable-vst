# 01 — Functional spec (Phase 1: what you have to define)

Phase 1 is a **decision phase**, not a coding phase. Every `DECIDE:` line below
is a question only you can answer. Fill them in (or let an agent interview you
through them), then lock the answers into `parameters.yaml` and an ADR.

## 1. Signal flow (v1 proposal)

```
                 ┌──────────── WORLD (background threads) ────────────┐
 APIs ─► Adapters ─► Cache ─► Normaliser ─► WorldSnapshot (8–16 "channels")
                                   │                         │
                                   ▼                         ▼
                          WavetableBuilder           Data mod sources
                          (tables A & B)             (D1…Dn, 0..1, slewed)
                 └──────────────────┼─────────────────────────┼─────────┘
                                    ▼                         ▼
 ┌──────────────────────────── AUDIO THREAD ──────────────────────────────┐
 │ Note source (MIDI or Drone chord) ─► Voices ×N                          │
 │   Voice = WT Osc A + WT Osc B (morph) + Sub/Noise ─► SVF filter ─► VCA  │
 │ Slow LFO ×4 + Data D1…Dn + Envelopes ─► Mod Matrix ─► any target        │
 │ Mix ─► Chorus ─► Delay ─► Reverb (large, FDN) ─► Soft clip ─► Out      │
 └────────────────────────────────────────────────────────────────────────┘
```

## 2. Modules and the decisions they need

### 2.1 Note source
- **Listen mode (default):** internal drone chord, no MIDI needed.
- **Play mode:** MIDI notes; data still modulates.
- DECIDE: drone chord — fixed root + scale picker, or root *driven by data*?
- DECIDE: polyphony (proposal: 6 voices, drone uses 3–4).
- DECIDE: does data ever trigger notes (e.g. each quake = a soft bell)? (Proposal: v1.1 "Events" layer.)

### 2.2 Wavetable oscillators (×2 per voice)
- 2048-sample frames, up to 64 frames per table, band-limited mipmaps.
- Table sources: (a) bundled factory tables, (b) **world tables** built live from data.
- Position (frame) morph, smooth; A/B crossfade.
- DECIDE: how is a world table built? Pick one or more for v1 (see `04-dsp-engine.md §2`):
  - [ ] *Time-series-as-waveform* (last N readings resampled to one cycle)
  - [ ] *Data-as-harmonics* (readings → partial amplitudes → inverse FFT)
  - [ ] *Spectral morph* (each frame = one snapshot in time; position = history)
- DECIDE: unison (proposal: up to 4, wide detune, slow spread drift).

### 2.3 Filter
- State-variable LP/BP, 12 dB, gentle drive. Cutoff key-tracked and modulatable.

### 2.4 Slow LFOs (×4)
- Rates 0.001 Hz (~17 min) to 2 Hz; optional tempo sync.
- Shapes: sine, triangle, smooth-random (interpolated noise), "breath" (asymmetric sine).
- Free-running and phase-continuous across play/stop; seeded so they're recallable.
- DECIDE: should LFO *rates* themselves be modulated by data? (Proposal: yes, via matrix.)

### 2.5 Data modulation sources
- `D1…Dn` = normalised, slewed channels from `WorldSnapshot`, 0..1.
- Each has: source binding, normalisation mode, slew time, curve, invert.
- DECIDE: how many channels in v1 (proposal: 8).

### 2.6 Mod matrix
- 16 slots: source → target, amount (bipolar), optional via-source.
- Sources: LFO1–4, D1–Dn, Env, Velocity, ModWheel, Random-per-voice.
- Targets: any parameter flagged `modulatable` in `parameters.yaml`.

### 2.7 Effects
- Chorus/ensemble, stereo delay (long, filtered feedback), large reverb (FDN,
  10–60 s tails, freeze). Reverb is part of the instrument, not optional garnish.
- DECIDE: shimmer (pitch-shift in reverb loop) in v1?

### 2.8 World controls
- **Region focus:** global / continent / lat-lon point (affects which quakes, weather, buoys).
- **Time scale:** real-time, or "compress the last 24 h / 7 d / 1 y into a loop".
- **Freeze world:** stop fetching; snapshot is stored in plugin state.
- **Sensitivity:** how much data moves the sound overall (macro).
- **Update interval** per source (respecting API limits).

### 2.9 Presets
- Preset = synth params + mappings + (optional) frozen snapshot.
- DECIDE: 10 factory presets themed by source ("Seismic Sleep", "Solar Wind", "North Atlantic Swell" …).

## 3. Musical guard-rails (always on)
- Pitch-affecting data is quantised to the selected scale.
- Output limiter + soft clip; loudness target ≈ −18 LUFS default.
- Any discontinuity in data → slewed; any table swap → crossfade over ≥ 1 s.

## 4. Phase-1 exit criteria
- All `DECIDE:` lines answered.
- `parameters.yaml` complete (IDs, ranges, defaults, units, modulatable flag).
- Data sources chosen and verified (`02-data-sources.md` table filled with real tested URLs).
- ADR-0001 (tech stack) and ADR-0002 (wavetable generation method) written.
