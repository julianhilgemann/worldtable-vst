# 05 — Sound engine

The curtain, heard. Each Sky Model node drives one resonator in a modal bank
tuned to the held chord (12-TET). Mood: positive, shimmering, uplifting,
awe-inspiring; emotion comes from harmony plus slight pitch instability.

```
SkySnapshot ─► Exciter ×N ─► ResonatorBank ×N (red · green · violet layers) ─┐
     │                                ▲ tuning                              │
     │                    Harmony (chord hold, progression, voice-leading)  ├─► mix ─► Drive ─► Phaser ─► Chorus/Ensemble
     ├─► Sparkles / whistlers ────────────────────────────────────────────┤        ─► Tape Delay ─► Shimmer Reverb
     └─► (optional) raw-field wavetable layer ────────────────────────────┘        ─► AutoGain ─► Limiter ─► out
 Mod sources (sky channels, LFO ×4, random walk ×2, env follower, triggers) ─► ModMatrix (16) ─► any destination
```

## 1. Resonator bank
- **One resonator per curtain node** (64–128). Cheap, stable form: complex
  one-pole (rotating phasor) resonators, so frequency and decay can change
  smoothly without clicks.
- **Tuning:** each node is assigned a chord tone and an octave/harmonic within
  Register and Voicing spread. Neighbouring nodes get different chord tones,
  so a fold travelling along the curtain moves across pitches and the stereo
  field. Only scale and chord tones; detune ≤ a few cents for thickness.
- **Per node:** brightness → partial gain; displacement → micro-detune
  (± few cents) and stereo position; dome azimuth → pan; elevation ×
  brightness → presence (gain + gentle high shelf).
- **Layers by emission colour:** red → low drone layer, long decay
  (~10–30 s); green → body (~3–10 s); violet → shimmering upper partials
  (~1–5 s). Layer levels follow the node's r/g/v mix.
- **Chord changes without clicks:** resonators are double-banked. On a change
  the new tuning receives the excitation while the old bank rings out, so
  voices crossfade over seconds to minutes; no frequency jumps on ringing energy.

## 2. Exciter
- Filtered noise per node (pink/brown, band-limited around its resonator),
  amplitude shaped by brightness change (rising brightness excites) plus a low
  continuous bed so a quiet sky still sounds.
- Substorm onset: a broadband strike across the bank, shaped (fast attack,
  long tail), never a click.

## 3. Harmony
- **Chord Hold:** root, scale, chord shape from the UI or latched from MIDI
  (notes held → chord; the last chord stays). 12-TET only.
- **Scales:** major, Lydian, major pentatonic (bright defaults); minor,
  Dorian, minor pentatonic.
- **Chord vocabulary by Mood:**
  - *bright:* maj7, add9, sus2, 6/9, Lydian ♯11 colours;
  - *bittersweet:* modal mixture (minor iv and ♭VI in major), add9 over minor;
  - *nocturnal:* minor 9, sus voicings, wide open fifths.
  Chromatic-mediant shifts (C → E, C → A♭) are the awe moves in every mood.
- **Progression engine:** Progression amount from "hold one chord" (0) to
  "follow the sky" (1). The sky phase sets the move: quiet → home; growth →
  suspensions and rising tension; onset → chromatic-mediant shift; recovery →
  borrowed chord resolving home. Optional: on sky events one voice steps to a
  neighbouring scale tone with a probability.
- **Voice-leading:** voices move by the smallest steps (minimal total
  semitone movement); changes take minutes; long resonator crossfades.
- **Pitch range:** global Transpose (semitones) and Register (low and deep →
  high and airy).

## 4. Life in the detail
- **Sparkles:** sparse, scale-quantised high bleeps triggered by sky events
  (onset sub-pulses, pulsation peaks), always chord tones, soft attack.
- **Whistlers / chorus:** falling or rising tones on whistler events, ending
  on chord tones, sitting under the reverb like natural radio.
- **Drift and nostalgia:** per-voice pitch drift (slow random walk, cents),
  tape-style wow, warm high-end roll-off; all on the Drift macro.
- **Breath:** a slow cycle (4–6 per minute) gently moving level, brightness
  and filter, mirrored in the visuals.

## 5. Raw-field layer (optional, off by default)
The POC wavetable oscillator playing audified CMO H data (builder A,
series-as-cycle; 64 × 2048, mipmapped, table crossfade ≥ 1.5 s). One level
control, tuned to the chord root.

## 6. Modulation
- **Sources:** sky channels (energy, onset, brightness, fold motion,
  pulsation, Kp, Bz, wind), 4 slow LFOs (0.0005–2 Hz; sine, triangle,
  smooth-random, drift, breath), 2 random walkers, output envelope follower,
  event triggers (onset, whistler).
- **Matrix:** 16 slots: source → destination, bipolar amount, curve, optional
  *via* source scaling the amount. Destinations include other modulators'
  rate and depth.
- **Triggers:** event → action (resonator strike, reverb swell, chord step)
  with a probability.
- Evaluated at block rate with per-sample ramps; data-driven targets slew
  ≥ 2 s by default. Target = base + Σ(amount × source), clamped, smoothed.
- LFO phase in double precision (0.0005 Hz must not drift); optional
  world-locked phase (from UTC) so two instances are in phase.

## 7. Effects (fixed order, one tab each)
| Stage | Notes | POC code |
|---|---|---|
| Drive | ADAA tanh + asymmetry, gentle | `dsp/Drive.h` (reuse) |
| Phaser | slow, 4–8 stages, stereo | new |
| Chorus / Ensemble | 3-voice ensemble, slow | new |
| Delay | tape-style, long (≤ 4 s), filtered + saturating feedback, wow | `fx/StereoDelay.h` (rework) |
| Reverb | large FDN, 10–60 s tails, shimmer at octave **and fifth** | `fx/ShimmerReverb.*` (extend) |
| Limiter | lookahead, ceiling −1 dBFS | `fx/Limiter.h` (reuse) |

Every effect parameter is a matrix destination.

## 8. Unattended safety
- AutoGain: slow loudness normaliser toward ≈ −18 LUFS short-term, ±12 dB,
  time constant ≥ 10 s, before the limiter.
- Final limiter ceiling −1 dBFS; DC blocker; NaN/inf guard that resets a
  resonator rather than propagating.
- Every parameter, preset and table change is smoothed or crossfaded.

## 9. References
Candidates, one per role (pick a final three):
- *Theme:* Biosphere — Substrata.
- *The real phenomenon:* VLF natural-radio recordings of auroral chorus and whistlers.
- *Drone body:* Stars of the Lid — And Their Refinement of the Decline.
- *Uplift and shimmer:* Global Communication — 76:14; Kaitlyn Aurelia Smith — Ears.
- *Generative behaviour:* Brian Eno — Reflection, Music for Airports; Laurie Spiegel — The Expanding Universe.
- *Taste anchors (owner's):* Boards of Canada (unexpected chord shifts, tape
  drift → Drift macro), Gas (dense, blurred texture), Aphex Twin — Selected
  Ambient Works Volume II.

**Method:** for each final reference, write what to take from it in synth
terms (register, density, attack, movement speed, brightness). Render 5-minute
Journey bounces and A/B them against the references.

## 10. Open questions (owner)
- **FM vs guardrail:** session 6 says "FM and cross-modulation stay harmonic
  by default (integer ratios)", session 4 says "control-rate only (no
  audio-rate FM)". Integer ratios only matter for audio-rate FM. Is there an
  FM layer (e.g. on sparkles) after all, or does "FM" mean slow
  cross-modulation only?
- N = 64 or 128 resonators (CPU vs density), decided with the T1.3 sketch.

## 11. Real-time checklist (copy into every DSP task)
- [ ] `prepareToPlay` allocates everything (max block size, max nodes).
- [ ] No allocation/lock/I/O in `processBlock` or the Sky Model step.
- [ ] Denormals flushed (`juce::ScopedNoDenormals`).
- [ ] All parameters smoothed; chord changes crossfade.
- [ ] Sample-rate independent (44.1/48/96 kHz) and block-size independent.
- [ ] No MIDI, default settings → sound (drone); MIDI only latches the chord.
