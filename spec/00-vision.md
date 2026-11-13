# 00 — Vision

Source of truth for direction: this repo's `spec/` (owner, 2026-11-13: "This
repo leads now"). The vision below came from the owner's project brief (Claude
Docs, "Worldtable — project brief", sessions 1–7). Decisions are recorded in
`spec/decisions/` (ADR-0002, -0005, -0006, -0008).

## One sentence
A drone instrument that plays Earth's magnetic field as the sun shakes it:
live, slow, never repeating, with an interface that *is* the aurora.

## What it is
- **One phenomenon:** the magnetosphere (geomagnetic field, solar wind, Kp,
  aurora). Nothing else in v1.
- **A drone first:** it plays continuously with no input, the way the Earth
  absorbs the field nonstop. MIDI is optional: it sets root and chord, or, in
  Play mode (ADR-0008, owner 2026-11-09), plays the resonators note by note
  from a keyboard, a sequencer or a DAW clip, in the sky's colours.
- **An eye-candy drone synth:** visuals first, technically useful underneath.
  Esoteric in concept, clean and modern in execution, easy to use (dashboard
  thinking).
- **One model, three renderings:** a generative Sky Model, nudged by real data,
  is heard (resonator bank), seen (all-sky dome) and sent (OSC to
  TouchDesigner). Sound and image always agree because they read the same state.

## The signal has four jobs
The same datum drives sound and image:
1. **Source:** magnetometer data drives the Sky Model's disturbance (and,
   optionally, the raw-field wavetable layer).
2. **Modulator:** Kp, solar-wind speed, density and Bz become slow modulation
   sources (filter, phaser and chorus depth, space …).
3. **Weather:** overall storm level sets the character of the whole patch,
   from calm and sparse to shimmering and wide.
4. **Image:** the same channels move the dome and go out over OSC.

## Experience principles
1. **Zero-touch beautiful.** Load it, press nothing: a pleasant drone.
2. **Slow is the point.** Changes unfold over seconds to hours, never jittery
   or alarming. The substorm cycle gives a set its long-form arc.
3. **Positive and awe-inspiring.** Shimmering, uplifting, with emotional
   harmony (unexpected but smooth chord changes, bittersweet borrowed chords,
   slight pitch instability). Western tonal space only.
4. **Legible, not literal.** You feel that a substorm changed the sound, and
   the UI says which event did what.
5. **Reproducible when needed.** Freeze or replay a recorded day so a session
   renders the same tomorrow; or let it follow the live sky.
6. **Unattended-safe.** It can run for hours: auto-gain, final limiter,
   everything slewed, no clicks on any change.
7. **Offline-safe.** No internet → it plays from cache or bundled storm days.
   It never stalls the host.

## Who it is for
Built for the owner, open source, no monetisation (ADR-0005). Shareable with
friends who produce; no target-group research drives decisions.

Use cases, in order of weight:
1. **TouchDesigner (main home):** loaded as a VST3, sky state out over OSC,
   dome mapped to fulldome projection.
2. Ambient and drone production in a DAW.
3. Relaxation and meditation listening (Breath macro, Standalone). A side
   effect, not a goal.

## The family
The first of several sister plug-ins on the same frame (later ocean data,
finance data). Each sister keeps the shared layout and gets its own
physics-derived palette.

## Non-goals (v1)
- Not a general-purpose synth; no wavetable editor.
- No step sequencer, no Euclid (the host's sequencer plays Play mode), no
  per-voice editing, no complex preset browser.
- No user-built modules or cables (VCV Rack / Cardinal are the place for that).
- No audio-rate FM, no microtonal tuning (detune = a few cents of thickness).
- No sources beyond the magnetosphere; no user-written data adapters.

## Targets
- Formats: VST3, AU, Standalone. CLAP later.
- Hosts: TouchDesigner (VST3), Ableton Live, Bitwig, Logic, Reaper.
- Platforms: macOS (Apple Silicon first), Windows.
- CPU: < 5 % of one core at 48 kHz for the default patch (audio only); the UI
  has its own budget, to be measured.

## Taste references
- Look: FabFilter clarity, NI Razor's clean spectral look, Teenage
  Engineering's graphic restraint, a modern spacecraft-instrument feel,
  historical observatory magnetograms, mission-control readouts. Unmistakably
  its own, not FabFilter-like (`spec/06-ui.md`).
- Sound: see `spec/05-sound-engine.md` §References.
