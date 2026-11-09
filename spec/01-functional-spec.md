# 01 — Functional spec (v1)

What the instrument does, from the user's side. How it is built:
`03-architecture.md`, `04-sky-model.md`, `05-sound-engine.md`, `06-ui.md`.
`DECIDE:` lines are open owner questions.

## 1. Signal flow

```
 USGS magnetometer ─┐
 NOAA Kp ───────────┼─► Data layer ─► WorldSnapshot ─► SKY MODEL (stepped at 100 Hz of audio time)
 NOAA solar wind ───┘   (cache, bundled       │          reservoir · curtain · pulsations · events
                        storm days)            │                         │
                                               │                  SkySnapshot
                                               ▼          ┌──────────────┼───────────────┐
                                    raw-field wavetable   ▼              ▼               ▼
                                    layer (optional)   SOUND          DOME (UI)     OSC → TouchDesigner
                                                       resonators,
                                                       harmony, FX
```

## 2. Observer and sky
- **Station:** the observer stands at a magnetometer station (ADR-0006).
  v1: CMO (College, Alaska). The header shows station code, coordinates, UT.
- **The sky shows:** the aurora curtain projected onto an all-sky dome, plus
  sun, moon (position and phase) and bright stars for the site.
- **Sky toggles (UI state, not sound):** day/night tracking on/off, stars and
  moon on/off, gradients only.
- DECIDE: second station for v1 (candidate Tromsø; licence and data access to check).

## 3. Autopilot: press play, get a set
| Mode | Model time | Data |
|---|---|---|
| **Live** (default when online) | UTC now | latest fetched data, cached |
| **Journey** | a recorded day, compressed (e.g. 24 h → 60 min) | bundled or cached day; default: 10–11 May 2024 storm |
| **Freeze** | stops (speed 0); the curtain keeps breathing in place | held |

The substorm cycle (quiet → growth → onset → recovery) gives a Journey its
long-form arc. With no network, Live falls back to the newest cached day.
- Freeze holds the data and the storm phase while the curtain keeps
  breathing in place; the sound keeps its detail on the held chord (owner,
  T2.1 review, 2026-11-05). Freeze is speed 0: model time stops, real time
  goes on.
- Journey speeds go up to **1440×** (24 h per real minute; owner, T2.1
  review, 2026-11-05).
- DECIDE (with the UI): which speeds get buttons (proposal: 1×, 6×, 24×, 96×).

## 4. Harmony: a slow progression engine
- **Chord Hold:** root, scale and chord shape set in the UI, or latched from
  MIDI. Held until changed. 12-TET only.
- **Play mode** (T3.7, ADR-0008): a Play button switches MIDI from latching
  the chord to playing the resonators note by note (polyphonic, velocity,
  sustain pedal, Attack, Release, Velocity, and a Drone level for the sky
  drone under the notes). The sky keeps colouring and moving every note.
- **Scales:** bright families by default (major, Lydian, major pentatonic);
  minor and Dorian available.
- **Voicing:** Voicing spread (octaves), Detune (cents, a few at most),
  Drift (slow, independent pitch wander per voice), global Transpose and
  Register (low and deep → high and airy).
- **Progression amount:** from "hold one chord" to "follow the sky".
- **Mood:** bright · bittersweet · nocturnal — selects the chord vocabulary:
  colourful consonant chords (maj7, add9, sus2, 6/9), modal mixture (minor iv
  in major) for bittersweetness, chromatic-mediant shifts (C → E or A♭) for awe.
- **The sky writes the dramaturgy:** quiet → stable home chord; growth →
  suspensions and rising tension; onset → a surprising chromatic-mediant shift;
  recovery → a bittersweet borrowed chord resolving home.
- **Voice-leading:** smallest steps; changes take minutes, with long crossfades.
- **Nostalgia layer:** subtle tape-style pitch wobble and warm high-end
  roll-off, both on the Drift macro.

## 5. Sound
Detail in `05-sound-engine.md`.
- The curtain is heard: one resonator per curtain node, layered by emission
  colour (red → low drone with long decay, green → body, violet → shimmering
  upper partials).
- Life in the detail: sparse, scale-quantised high sparkles on sky events;
  whistler and chorus tones when activity is high.
- Optional raw-field layer: audified magnetometer data (the POC wavetable).
- Effects, fixed order, one tab each: Drive → Phaser → Chorus/Ensemble →
  Delay (tape-style, long) → Reverb (large, shimmer at octave and fifth) →
  Limiter.

## 6. Macros
Large top-level controls; everything else lives in tabs.
- Proposal (Hero Concepts canvas): **Storm** (sensitivity to the sky),
  **Breath** (slow cycle, 4–6 per minute, mirrored in the visuals), **Glow**,
  **Space**, **Drift**.
- DECIDE: confirm the macro set (brief: 4 to 6) and what each one moves.

## 7. Modulation: modular feel, fixed frame
- **Sources:** sky channels (energy, onset, brightness, fold motion,
  pulsation, Kp, Bz, wind), 4 LFOs (slow rates, smooth-random and drift
  shapes), 2 random walkers, an output envelope follower (the patch hears
  itself), event triggers (onset, whistler).
- **Matrix:** 16 slots: source → destination, amount, curve, optional *via*
  source that scales the amount. Modulators can target other modulators' rate
  and depth.
- **Triggers:** events fire actions (resonator strike, reverb swell, chord
  step), each with a probability.
- **Guardrails:** control rate only, everything slewed (≥ 2 s default for
  data-driven targets), no audio-rate FM.
- Every effect parameter is a matrix destination.

## 8. OSC out
Sky state (nodes, phase, energy, events, astronomy), modulator values and the
output envelope, for TouchDesigner. Address map in `03-architecture.md` §OSC.
- DECIDE: default port and rate (proposal: UDP 127.0.0.1:9000, 60 Hz).

## 9. Presets
Preset = sound and modulation parameters + station + autopilot mode
(+ optional frozen sky). Small, flat list in v1.
- DECIDE: factory set (proposal: ~10, named for sky states, e.g. "Quiet Arc",
  "Substorm", "Corona", "Pulsating Dawn").

## 10. Unattended safety (always on)
Auto-gain, final limiter (ceiling −1 dBFS), slewing on all modulation, table
and preset crossfades, no clicks on any change. Loudness target ≈ −18 LUFS.

## 11. Out of v1
Step sequencer (Play mode is played from the host), per-voice editing, ocean
source, complex preset browser, ambisonic/binaural output, fully adaptive
environment-aware artworks (parked, see HyperAdapted).

## 12. Open questions (owner)
- Product and family name (Worldtable is the working title).
- Final three sound references (`05-sound-engine.md` §References).
- The `DECIDE:` lines above.
