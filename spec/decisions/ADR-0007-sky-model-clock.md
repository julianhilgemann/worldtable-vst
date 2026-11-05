# ADR-0007 — Sky Model clock and determinism

**Status:** Accepted (owner, T2.1 review, 2026-11-05). Proposed by Claude Code
the same day. Deviates from the brief's "own thread" wording; the owner
accepted the deviation.

## Context
The brief places the Sky Model at ~100 Hz control rate "on its own thread".
Two other brief requirements pull against a free-running thread:
- **Freeze and offline render must be reproducible** ("seeded randomness keeps
  it reproducible"). A DAW bounce runs faster or slower than real time; a
  wall-clock thread would step the model a different number of times per
  rendered second, so the same session renders differently.
- **Sound and image must agree.** If the audio thread reads a model that
  advances on its own clock, the two drift apart under load.

## Decision
- **The model is clocked by audio sample time.** `processBlock` steps it every
  `sampleRate / 100` samples (an exact integer at 44.1, 48, 88.2, 96 and
  192 kHz), counted by a running sample counter so the step grid is
  independent of block size.
- **The step is real-time safe:** fixed-size state, no allocation, no locks,
  seeded RNG, bounded work (N ≤ 128 nodes). It obeys the audio-thread rules
  in `CLAUDE.md`. Estimated cost: well under 1 % of a core.
- **Readers get published snapshots.** After each step the audio thread writes
  a `SkySnapshot` (fixed-size POD) into a triple buffer; the UI and the OSC
  thread read the newest one. They never touch model state.
- **Model time is separate from sample time.** Live: model time = UTC now.
  Journey: model time advances by the compression factor (e.g. 24 h → 60 min).
  Freeze: model time stops; the curtain keeps breathing on real time
  (`spec/01-functional-spec.md` §3). Journey speeds go up to 1440×.
- **Same seed + same data + same sample rate → bit-identical snapshot
  sequence**, whatever the block size. This is a unit test.

## Alternative
A dedicated thread at ~100 Hz with the audio thread interpolating between its
snapshots. Simpler to profile, but offline renders are not reproducible and
the model needs its own sync to host time. Rejected unless the step proves too
expensive for the audio thread.

## Consequences
- `source/sky/**` follows the audio-thread rules (`CLAUDE.md` rule 1).
- Offline render (`WorldtableRender`) and the DAW play the same sky.
- Snapshot and track hand-off mechanism: ADR-0003.
