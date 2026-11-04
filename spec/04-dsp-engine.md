# 04 — DSP engine

## 1. Wavetable oscillator
- Table: up to 64 frames × 2048 samples, float.
- Band-limiting: per table, build ~10 mipmap levels (one per octave) by FFT,
  zeroing partials above Nyquist for that level's top note. Select level from
  current pitch; crossfade between neighbouring levels to avoid stepping.
- Playback: phase accumulator (double), linear or cubic interpolation within
  frame, linear interpolation between frames for position morph.
- Unison: up to 4 copies, detune spread + stereo spread, each with its own
  very slow drift LFO (gives the "alive" chorus without a chorus).

## 2. World tables — turning data into waveforms
Three builders; v1 ships at least (A) and (B).

**A. Series-as-cycle.** Take the last *N* readings of a channel, detrend,
normalise to ±1, resample to 2048 with windowed sinc, enforce zero-crossing
continuity at the wrap (blend end into start). Frames = successive windows in
time → table position scrolls through history.
*Character:* jagged/noisy data → buzzy; smooth data → soft. Apply an optional
low-pass in the spectral domain ("smoothness" param) to keep it ambient.

**B. Data-as-harmonics (recommended default).** Map a vector of channel values
(or one channel's history buckets) to partial amplitudes 1…64, multiply by a
gentle spectral tilt (−6 to −12 dB/oct), randomise phases from a fixed seed,
inverse FFT → one frame. Each new snapshot = a new frame. Always musical,
because it's additive with controlled rolloff.

**C. Spectral morph (stretch).** Frames are snapshots over the last 24 h; LFO
or "time scale" sweeps position → you literally hear the day pass.

All builders output a `WavetableSet` with RMS-normalised frames (no loudness
jumps between frames) and run on the builder thread.

## 3. Voice & drone engine
- Voice: OscA + OscB (A/B mix), sub sine, pink-noise layer (data-driven level),
  SVF filter, amp envelope with long times (attack up to 30 s, release up to 60 s).
- DroneEngine (Listen mode): holds a chord (root + intervals from scale),
  occasionally re-voices one note at a time with long crossfades — rate set by
  a slow LFO or by data activity. Never all notes change at once.

## 4. Slow LFOs
- Phase accumulator in double precision (rates down to 0.001 Hz must not drift).
- Shapes: sine, tri, smooth-random (cubic-interpolated seeded noise), breath.
- Output passes through a one-pole smoother so shape changes never click.
- Phase can be "world-locked": derived from UTC time → two instances anywhere
  on Earth are in phase. (Nice installation feature, cheap to do.)

## 5. Mod matrix
- Evaluated per block (block-rate) for slow sources, with per-sample linear
  ramping on targets; per-sample only for audio-rate needs (none in v1).
- Target value = base + Σ(amount × source), clamped to param range, then smoothed.

## 6. Effects chain
Chorus/ensemble → stereo delay (up to 4 s, LP in feedback, modulated) →
FDN reverb (8–16 lines, modulated delays, damping, size, freeze, optional
shimmer) → tilt EQ → soft clip → true-peak-safe limiter.

## 7. Real-time checklist (copy into every DSP task)
- [ ] `prepareToPlay` allocates everything (max block size, max voices).
- [ ] No allocation/lock/I/O in `processBlock` (verify with a test allocator hook in debug).
- [ ] Denormals flushed (`juce::ScopedNoDenormals`).
- [ ] All params smoothed (`juce::SmoothedValue` or one-pole).
- [ ] Sample-rate independent (test 44.1/48/96 kHz).
- [ ] Silent input + no MIDI in Listen mode still produces sound; in Play mode produces silence.
