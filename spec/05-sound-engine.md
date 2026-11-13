# 05 — Sound engine

The curtain, heard. Each Sky Model node drives one resonator in a modal bank
tuned to the held chord (12-TET). Mood: positive, shimmering, uplifting,
awe-inspiring; emotion comes from harmony plus slight pitch instability.

Status: **in the plug-in** (T3.6, 2026-11-06). §1–2 were ported from the T1.3 sketch
in T3.1 (`dsp/SkySound`, `ResonatorBank`, `Exciter`, `Voicing`; null test vs the sketch
−135 dB), §3 in T3.2/T3.3 (`dsp/Harmony`: vocabulary, naming, chord hold, scales, MIDI
latch, progression), and §7's chain in T3.5a/b. `dsp::Instrument` now runs `SkySound`,
the chain and the MIDI latch inside `PluginProcessor`, driven by the v1 parameters.
Sparkles, whistlers and the full Breath cycle are T3.4; the raw-field layer (§5) is
T3.6b and stays off. The default Journey opens at 17:20 UT on the storm day (the M3
render window) rather than at 00:00. T3.7 (owner, 2026-11-09) adds Play mode (§13,
ADR-0008), the low resonator (§14) and the filter (§15). T3.8 (owner, 2026-11-13) replaces the
fixed layer ranges, Register, Spread and the low octave with the resonator bands (§16).

```
SkySnapshot ─► Exciter ×N ─► ResonatorBank ×N (red · green · violet layers) × drone gain ─┐
     │                                ▲ tuning                                           │
     │                    Harmony (chord hold, progression, voice-leading)               │
     ├─► LowResonator (the bass note, in the low band from its floor; §14, §16) ─────────┤
     ├─► PlayVoices (Play mode: MIDI notes, the curtain's colours; §13) ─────────────────┼─► mix ─► Filter (§15) ─► Drive
     ├─► Sparkles / whistlers ───────────────────────────────────────────────────────────┤   ─► Phaser ─► Chorus/Ensemble
     └─► (optional) raw-field wavetable layer ───────────────────────────────────────────┘   ─► Tape Delay ─► Shimmer Reverb
                                                                                              ─► AutoGain ─► Limiter ─► out
 Mod sources (sky channels, LFO ×4, random walk ×2, env follower, triggers) ─► ModMatrix (16) ─► any destination
```

## 1. Resonator bank
- **One resonator per curtain node** (64–128). Cheap, stable form: complex
  one-pole (rotating phasor) resonators, so frequency and decay can change
  smoothly without clicks.
- **Tuning:** each node is assigned a chord tone and an octave/harmonic within
  its layer's band (§16: floor, three crossovers, ceiling). Neighbouring nodes get different
  chord tones, so a fold travelling along the curtain moves across pitches and the stereo
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
  (notes held → chord). 12-TET only. **MIDI vs progression** (T3.6, owner decision): while
  notes are latched the held chord is authoritative; the sky keeps sensing (its target
  function and shape advance) but writes nothing. The last note-off clears the latch and the
  sky's current chord returns, so the progression resumes where the sky is then. With
  Progression 0 that is the UI's held chord.
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
- **Pitch range:** global Transpose (semitones), and the bands (§16): moving all edges is
  what Register was (low and deep → high and airy), their spacing what Voicing spread was.

## 4. Life in the detail
- **Sparkles:** sparse, scale-quantised high bleeps triggered by sky events
  (onset sub-pulses, pulsation peaks), always chord tones, soft attack.
- **Whistlers / chorus:** falling or rising tones on whistler events, ending
  on chord tones, sitting under the reverb like natural radio.
- **Drift and nostalgia:** per-voice pitch drift (slow random walk, cents),
  tape-style wow, warm high-end roll-off; all on the Drift macro.
- **Breath:** a slow cycle (4–6 per minute) gently moving level, brightness
  and filter, mirrored in the visuals.

## 5. Raw-field layer (optional, off by default; T3.6b)
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
| Filter | SVF LP12 / LP24 / BP / HP, the sky opens it (§15, T3.7) | `dsp/Svf.h` (reuse) |
| Drive | ADAA tanh + asymmetry, gentle | `dsp/Drive.h` (reuse) |
| Phaser | slow, 4–8 stages, stereo | new |
| Chorus / Ensemble | 3-voice ensemble, slow | new |
| Delay | tape-style, long (≤ 4 s), filtered + saturating feedback, wow | `fx/StereoDelay.h` (rework) |
| Reverb | large FDN, 10–60 s tails, shimmer at octave **and fifth** | `fx/ShimmerReverb.*` (extend) |
| Limiter | lookahead, ceiling −1 dBFS | `fx/Limiter.h` (reuse) |

Every effect parameter is a matrix destination.

**Status (T3.5a/b):** the whole chain runs in `WorldtableRender --sky`: Drive → **Phaser** →
**Chorus** → tape delay → ShimmerReverb (**octave and fifth**) → AutoGain → DC → limiter.
Drive, the delay, the reverb and the limiter are the POC units; Phaser, Chorus, AutoGain, the
DC blocker and the shimmer's fifth are new. **The plug-in runs this chain since T3.6**
(`dsp::Instrument`), in the same order. Phaser and chorus carry a loudness make-up so engaging
them does not duck the mix. The reverb's size and pre-delay glide per sample and its line
coefficients update on an absolute 64-sample grid, so a parameter move never clicks and the
sound does not depend on the host's block size.

## 8. Unattended safety
- AutoGain: slow loudness normaliser toward ≈ −18 LUFS short-term, ±12 dB,
  time constant ≥ 10 s, before the limiter. (T3.5a: −18 dBFS RMS, no K-weighting yet —
  `fx/AutoGain`, 3 s measure / 10 s slew.)
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
  Data so far (§12): in JS, 64 per layer costs 9 % of a core, 128 costs 16 %.
  In C++ (T3.1, Release, this Mac, sky + sound, no effects): 64 costs 1.4 %,
  128 costs 3.1 %. Both fit the 5 % budget before the effects; 64 stays the
  default (T1.3) until the owner compares the `-n64` and `-n128` bounces by ear.

## 11. Real-time checklist (copy into every DSP task)
- [ ] `prepareToPlay` allocates everything (max block size, max nodes).
- [ ] No allocation/lock/I/O in `processBlock` or the Sky Model step.
- [ ] Denormals flushed (`juce::ScopedNoDenormals`).
- [ ] All parameters smoothed; chord changes crossfade.
- [ ] Sample-rate independent (44.1/48/96 kHz) and block-size independent.
- [ ] No MIDI, default settings → sound (drone); MIDI latches the chord, or plays notes in
      Play mode (§13) at their sample positions.

## 12. Sketch mapping and constants (T1.3, 2026-11-05)
Source of truth: `prototype/sky/sound/engine.js` `SOUND_DEFAULTS` and
`sound/harmony.js` `VOCABULARY`. First pass; the owner signed off the direction
on 2026-11-05 ("sounds fine … run with it"), constants unchanged. N = 64 per layer.

| Sky | Sound |
|---|---|
| node colour r / g / v (already × brightness) | gain of the node's red, green, violet resonator: level × √colour × presence, slewed 2 s |
| brightness rising | excitation (0.4 per unit/s, smoothed 0.3 s) |
| activity | excitation bed 0.3 × (0.5 + activity) |
| onset | strike: 0.3 + 0.35 × strength (≤ 1.5), attack 80 ms, tail τ 6 s, 0.5 + 0.5 × Gaussian around the onset node (σ 12 nodes); always on the newest chord |
| phase | chord function: quiet → home, growth → tension, onset → awe, recovery → borrowed; onset strength < 0.3 → tension only |
| displacement y | ± 3 cents at ± 160 km; drift ± 4 cents (seeded noise, 20 s knots) |
| dome azimuth / elevation | equal-power pan, −sin(az) × 0.9 (east left), slewed 0.5 s / presence 0.45 … 1 (0° … 50° elevation) |

- **Layers:** red = root + fifth, MIDI 31–49 (T60 20 s); green = the chord,
  50–74 (6 s); violet = chord tones above root and fifth, 72–90 (2.5 s). Register
  shifts them; Voicing spread scales their width. Default key D, Mood bright.
  *Since T3.8 the ranges are the bands' (§16): red 34–51, green 48–74, violet 71–90 at the
  defaults; the sketch keeps the ranges above.*
- **Resonators:** complex one-pole, input × √(1−r²); double-banked. A change
  moves the bed excitation to the retuned bank over 12 s (equal power) while the
  old bank rings out. A sky or user change that finds the old bank still ringing
  damps it to T60 3 s instead of waiting (wanders wait).
- **Progression:** amount 1 (follow the sky). Each move is a seeded roll from
  the model minute, so the same sky always picks the same chords. Wander inside a
  function every (150–360 s) / (0.5 + amount) of real time.
- **Chords** (degrees from the key; first pass): bright home Imaj7 · Iadd9 ·
  I6/9; tension Imaj7sus2 · IVadd9 · Vsus4add9; awe ♭VImaj7 · IIIadd9 · VIadd9;
  borrowed iv(add9) · ♭VIIadd9 · ♭VIadd9. Bittersweet and nocturnal in `VOCABULARY`.
- **Stand-in FX:** 8-line FDN reverb (T60 9 s, mix 0.35), auto-gain toward
  −18 dBFS RMS (±12 dB, τ 10 s, no K-weighting), DC blocker, 5 ms lookahead
  limiter at −1 dBFS.
- **Cost (JS, this Mac):** 64 per layer 11.5× real time in Node (9 % of a
  core), 10.5× in Chrome's AudioWorklet; 128 per layer 6.4× (16 %), 5.7×.
  **C++ (T3.1, Release):** the Sky Model and the resonator bank together cost
  1.4 % of a core at 64 per layer and 3.1 % at 128, without SIMD; the
  stand-in reverb and limiter add 0.5 %.
- **Levels:** storm and moderate bounces sit at −17 to −19 dBFS RMS. The quiet
  day sits at −26 dBFS RMS: the auto-gain's +12 dB isn't enough. Calm and sparse
  is intended, but it may be too quiet.
- **Plug-in (T3.6):** the default Journey is the storm from 17:20 UT at 24×; the macros are
  neutral at 0.5 (Breath 0.35), the render constants above are the defaults. Breath is a
  5-per-minute cycle on the layer gains, evaluated on the tick grid (first pass; T3.4 deepens
  it). Register and Voicing spread take effect at the next voicing (chord change), so a move
  is heard on the next chord, not immediately. Scale is stored for the sparkles (T3.4) and does
  not change the chords today. A 120 s plug-in render from 17:20: peak −1.0 dBFS, RMS −17.3,
  max step 0.063, 3.3 % of a core in Release (sky + sound 2.0 %, FX 1.3 %).

## 13. Play mode (T3.7, ADR-0008)
The owner's request (2026-11-09): play the instrument from a MIDI keyboard, a sequencer or an
Ableton clip. A **Play** button (`play`) switches MIDI from latching the chord to playing notes.
- **The voices** (`dsp/PlayVoices`): 24 slots, 16 held notes at most (the oldest held note gives
  way; a stolen voice fades out in 10 ms first; the same note again retriggers its voice). A note
  sounds from three points of the curtain; each has four resonators, the layers in small: red an
  octave down (0.7), green the note (1) and its octave (0.45), violet the twelfth (0.35, opened by
  velocity). Complex one-pole resonators with the layer decays as T60s, excited by a strike at
  note-on (full up to a 20 ms attack, none from 320 ms: a slow attack bows in) and a bow while the
  key is down (2.7 × the bed, × (0.5 + activity)). An envelope (linear Attack, exponential Release
  to −60 dB) shapes the output; Velocity sets how much of a 30 dB range velocity covers.
- **The sky in the notes:** each point takes its node's pan, presence, detune (displacement) and
  drift; the node's emission balance weights the partials (0.5 … 1.5: a red-heavy node favours the
  octave below, a violet one the twelfth); an onset re-strikes the held notes softly (0.35).
- **The drone** glides over 2 s to `play_drone` (0 by default) and keeps running; leaving Play
  mode glides it back and releases every note. Entering Play clears the MIDI latch.
- **Levels:** AutoGain holds unity in Play mode (glides there over 2 s) so dynamics stay the
  player's. Dry (no delay, reverb, chorus, phaser, low), velocity 100: one note sustains near
  −27 dBFS RMS, a 4-note chord near −20 (its strike ~5 dB above), an 8-note chord near −17 with
  its strike peaking at −2.6 dBFS; velocity 40 sits ~8 dB under 100 at the default Velocity.
- **MIDI:** note on/off at their sample (the block is split per event), sustain pedal (CC64), all
  notes off (CC123), all sound off (CC120). Transpose applies to played notes. Pitch bend and the
  mod wheel are not used yet.
- **Cost:** the Play demo (`WorldtableRender --set play=1 --midi …`) costs 3.3 % of a core in
  Release with the drone computed underneath.

## 14. The low resonator (T3.7)
The owner's request: "the sound generally needs some more low end … some resonator around the
low end that we can set up using the UI". A resonator layer under red (`dsp/LowResonator`), mono:
- **Tuning:** the chord's root (drone mode) or the lowest held note (Play mode), as the lowest note
  of its pitch class at or above the low band's floor (§16, `band_floor`; C1 by default, so
  C1–B1: T3.7's Sub octave; a floor of C2 is its Bass). A pair of resonators ± 1.5 ct around the
  note (slow beating) and one an octave up at 0.35 (the body, so it speaks on small speakers).
- **Excitation:** drone mode, the exciter's bed × (0.5 + activity) plus the onset swell (80 ms
  attack, τ 6 s); Play mode, a bow (1.4 × the bed) while a note is held and a strike when a new
  lowest note comes in, through the Play envelope.
- **Changes:** a new note goes to the other bank and the excitation crossfades over (4 s for a
  chord change, the attack in Play mode); the old bank is damped to a T60 of at most twice the
  crossfade.
- **Level:** `low_level` 0.5 by default; the red emission share nudges it (× 0.7 + 0.3 × the
  curtain's red weight). On the storm render the 31.5 Hz octave band rises from −41.9 to
  −28.1 dBFS (level with the 63 and 125 Hz bands); at 1 it is −23.9.

## 15. The filter (T3.7)
The owner's request: the POC's filter back, in the chain. `fx/Filter`, between the sources and
Drive, inside the sources' tick so the sky reaches it on the tick grid:
- Two TPT SVF stages per channel (the POC's `Svf.h`): LP12 = stage 1 low, LP24 = a second stage
  (k = √2) on it, BP = stage 1 band × k (unity peak), HP = stage 1 high. Resonance 0 … 1 →
  damping 2 … 0.25 (Q ≤ 4, +12 dB).
- Nothing jumps: the cutoff glides per sample in log frequency (τ 20 ms) with coefficients per
  sample; a type change crossfades the outputs over ~20 ms.
- **Sky:** the activity, slewed 2 s, opens it by up to 3 octaves × `flt_sky` (LP and BP upward, HP
  downward). Default: LP12 open at 20 kHz, so the drone is unchanged until it is used.


## 16. The resonator bands (T3.8)
The owner's request (2026-11-13): "the resonators need to have a way of being tuned somehow
separately"; "resonator crossover frequencies that change the bands of colors … if dragged, one
band gets elongated, while the other one gets shorter." `dsp/Voicing` (`BandEdges`, `bandRange`,
`bandSlots`, `lowNote`), applied by `SkySound`:
- **One pitch axis, four bands:** low [floor, x1), red [x1, x2), green [x2, x3), violet
  [x3, ceiling), MIDI notes (`band_floor` 24, `band_x1` 36, `band_x2` 50, `band_x3` 73,
  `band_ceiling` 91). Minimum widths: low 12 (the root always has a place), the others 5. The
  engine keeps the order and the widths whatever is automated, clamped from the floor upward.
- **Blend** (`band_blend`, 1.5 st): a layer rings its band widened by round(blend) on each side
  that touches another band, never below the floor nor up to the ceiling. Defaults: red 34–51,
  green 48–74, violet 71–90.
- **Tones** per band (`red_tones` Root + fifth, `green_tones` Chord, `violet_tones` Colours; also
  Root and All = Chord): the pitch classes the band's voices take, padded to five slots as before.
  **Shift** (`*_shift`, ±12 st) moves the band's pitch classes against the chord (±12 is the same
  set: the band's range sets the octave).
- **Low:** the low resonator's note is the lowest of its pitch class at or above the floor.
- **Applying a change:** once the bands have been still for 300 ms (30 ticks), the current chord
  is voiced again into the other bank and the bed crossfades over (12 s), as a key change by hand;
  a chord already waiting is voiced with the new bands when it lands. An old bank still ringing
  is damped to a T60 of 0.5 s (not 3 s), so a change is heard within a second. A voice keeps its
  place within a band that moved (its old note mapped onto the new range), so a band moved an
  octave keeps its voices spread. A blend move that keeps its rounding re-voices nothing. Play
  mode's voices keep their own partials; only the low note's folding follows the floor.
- **Telemetry:** the chord readout carries the five edges and the blend the current bank was
  voiced with (`ChordState::edges`, `blend`; the editor's `chord.edges`, `chord.blend`).
- **Removed:** `register_semis`, `spread`, `low_octave` (state version 3: an older state loads with
  the default bands). The sound golden is the port's own since T3.8 (the sketch keeps its fixed
  ranges): `WorldtableTests "[.regen-sound-golden]"` re-exports it.
