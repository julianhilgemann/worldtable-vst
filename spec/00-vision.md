# 00 — Vision

## One sentence
An instrument that turns the measurable state of the Earth and its
surroundings into a slow, warm, never-repeating ambient sound — you load it,
press nothing, and hear the world breathing.

## Experience principles
1. **Zero-touch beautiful.** With no MIDI and default settings it must already
   produce a pleasant drone ("Listen mode"). Playing notes is optional.
2. **Slow is the point.** Changes unfold over seconds to hours. Data is never
   mapped so directly that it becomes jittery or alarming.
3. **Legible, not literal.** The user should *feel* that an earthquake or a
   solar storm changed the sound, and the UI can tell them which datum did
   what — but the sound stays musical (scales, consonance, soft dynamics).
4. **Reproducible when needed.** A producer can "freeze the world" so a session
   renders identically tomorrow; or let it drift live for installations.
5. **Offline-safe.** No internet → it still plays from cache or bundled
   datasets. It never stalls, never crashes the DAW.

## Non-goals (v1)
- Not a general-purpose wavetable synth (no 3-osc Serum competitor).
- No user-written API adapters in v1 (fixed curated source list).
- No sample-accurate data events; data is a slow modulation field.
- No sequencer or arpeggiator.

## Targets
- Formats: VST3, AU, Standalone. (CLAP optional later via clap-juce-extensions.)
- Platforms: macOS (Apple Silicon first), Windows.
- CPU: < 5 % of one core at 48 kHz in Listen mode.
- Usable inside a DAW *and* as a standalone "radio" for installations
  (e.g. feeding a TouchDesigner visual via OSC — see stretch goals).
