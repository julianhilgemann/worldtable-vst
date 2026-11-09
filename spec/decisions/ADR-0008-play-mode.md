# ADR-0008 — Play mode: MIDI notes play the resonators

**Status:** Accepted (owner, 2026-11-09: "I want a simple "play" mode by pressing a keyboard button
somewhere that enables me to play it regularly using either a sequencer or via MIDI keyboard or
within ableton"). Designed and built by Claude Code in T3.7 the same day.

## Context
v1 was specified as a drone first: "MIDI is optional and only sets root and chord", with "no
melodic MIDI play beyond chord latch" among the non-goals (spec/00, spec/01 §11). The chord latch
(T3.2, T3.6) turns held notes into the drone's chord, which then crossfades in over seconds, so a
keyboard or a sequencer cannot play the instrument. The owner wants to play it like an instrument
from a keyboard, a sequencer or a clip in Ableton, without losing what makes it Worldtable.

## Decision
- **A mode, not a second instrument.** A `play` parameter (a Play button in the header) switches
  MIDI's role. Off: MIDI latches the drone's chord, as before. On: MIDI notes play the resonators
  polyphonically, at their sample positions; the drone fades out over 2 s (rule 4) to a `play_drone`
  level (0 by default), and the sky keeps running underneath.
- **The same sound, in small.** Played notes are the curtain's complex one-pole resonators (layer
  decays and levels shared with the drone), struck at note-on and bowed while held, from three
  points of the curtain each: those nodes' pan, presence, detune, drift and emission balance
  colour every note; onsets re-strike held notes. So a played phrase still sounds and moves like
  the sky, and the dome still shows what colours it.
- **On the sky's clock.** The played notes (and the low resonator and filter of T3.7) run as
  SkySound's companion (`dsp::Sources`), ticked and rendered on the same grid as the curtain; the
  instrument splits the block at each MIDI event. Same MIDI at the same samples → the same output
  at any block size (rule 5).
- **The player's dynamics stay theirs.** In Play mode AutoGain glides to unity instead of
  normalising (a normaliser would lift the first note after a pause by up to 12 dB).
- Sustain pedal, all-notes-off and all-sound-off are honoured. 16 held notes at most, 24 voices
  with their releases; a stolen voice fades out in 10 ms first.

## Alternatives
- **Faster latch** (keep the drone, make chord changes instant): still one chord at a time, no
  melodies, and instant retunes of ringing resonators are audible as pitch jumps.
- **A separate synth voice** (oscillators, ADSR, the POC's voice): plays well but is not the
  instrument; the sky would not be in the notes.

## Consequences
- spec/00 and spec/01 §11 drop "no melodic MIDI beyond chord latch"; the step sequencer and Euclid
  stay out (the host's sequencer plays Play mode).
- The real-time checklist's "no MIDI, default settings → drone" still holds: Play is off by default.
- `SkySound` has a companion hook; protected interfaces (`SkySnapshot.h` etc.) are unchanged.
- Open: pitch bend, mod wheel and MPE (Later in spec/07), clickable keys in the UI.
