# Sky and sound sketch (T1.2, T1.3)

Browser sketch of `spec/04-sky-model.md`: a generative aurora, nudged by the
recorded days in `resources/sky/`, seen from CMO (College, Alaska) on an
all-sky dome, and heard through the resonator bank of `spec/05-sound-engine.md`
(press **sound on**). Plain ES modules, WebGL 1, AudioWorklet, no dependencies,
no build step.

## Run
```sh
python3 -m http.server 8765 --bind 127.0.0.1        # from the repo root
open "http://127.0.0.1:8765/prototype/sky/#day=2025-01-19&t=2025-01-20T13:08&speed=6"
```
The URL hash keeps the review state (`day`, `t` UTC, `speed`, `seed`, any
tuned sky constant as `p.<name>=<value>` and sound constant as `s.<name>=<value>`),
so a moment can be shared as a link. Audio needs a click: press **sound on**.

Good moments to look at:
| Link hash | What |
|---|---|
| `day=2025-01-19&t=2025-01-20T13:08&speed=6` | a data-forced onset (CMO bay −946 nT), arc near zenith |
| `day=2025-01-19&t=2025-01-19T11:25&speed=6` | onset in the northern sky |
| `day=2024-05-10&t=2024-05-10T17:00&speed=24` | the Gannon storm arrives; arc sweeps south |
| `day=2024-05-10&t=2024-05-11T08:00&speed=6` | storm night, twilight, red-topped rays in the south |
| `day=2025-01-25&t=2025-01-25T08:00&speed=96` | quiet: faint arc low in the north, soft pseudo-breakups |

## How it reads
- **Dome:** zenith centre, horizon rim, north up, **east left** (as seen lying
  on your back, head to the north). `MZ` = magnetic zenith (77° elevation, SSW):
  rays converge there because the curtain is projected along dipole field lines.
- **Strip chart:** CMO's measured ΔH (green) vs the model's (violet); coupling
  E = v·Bs (red area) and stored energy vs threshold (white); phase band
  (grey quiet, violet growth, bright green onset, dim green recovery); onset
  ticks (red = data-forced, green = loaded, grey = pseudo-breakup). Click to jump.
- **Tuning:** sliders for the constants in `model.js` `DEFAULTS`;
  "copy constants" copies the changed ones as JSON.

## Two clocks
- **Model time** (UTC): data, reservoir, phases, assimilation, oval. A fixed 1 s
  grid, so the slow sky is identical at every Journey speed (tested).
- **Real time** (100 Hz ticks of audio time, ADR-0007): curtain waves,
  brightness, pulsations, whistlers, so motion looks natural at any speed.

## Sound (T1.3)
**sound on** moves the Sky Model into an AudioWorklet: the audio callback steps
it every `sampleRate / 100` samples (ADR-0007), and the dome draws the snapshots
the worklet posts, the way the plug-in's UI will. Sound off hands the sky back
to the page without a jump.

What you hear:
| Sky | Sound |
|---|---|
| curtain node colour (red / green / violet, includes brightness) | gain of that node's red (root + fifth, low, T60 20 s), green (chord, mid, 6 s) and violet (upper chord tones, high, 2.5 s) resonators, √-curved, slewed 2 s |
| rising brightness, activity | excitation (seeded noise) above a continuous bed |
| substorm onset | a strike across the bank (strongest at the onset longitude) on the new chord |
| phase | the chord: quiet → home, growth → tension, onset → chromatic mediant, recovery → borrowed; weak onsets (pseudo-breakups) only add tension |
| fold displacement | ± 3 cents; plus a slow per-voice drift of ± 4 cents |
| dome azimuth / elevation | pan (east left, like the dome) / presence |

The event ticker shows each chord change and its reason. **VOICES** 64 or 128
resonators per layer (128 = two per curtain node). SOUND TUNING holds the
constants (`sound/engine.js` `SOUND_DEFAULTS`); "copy sound constants" copies the
changed ones. The reverb, auto-gain and limiter are stand-ins (the spec's FX
chain is T3.5).

Bounces (5 minutes from the current moment; same arguments → same bytes in the
browser and in Node):
```sh
node prototype/sky/bounce.mjs --day 2024-05-10 --from 2024-05-10T17:20 [--minutes 5] [--speed 24] \
     [--seed 1] [--s.voicesPerNode 2] [--s.mood nocturnal] [--out renders/x.wav]
```
It prints peak, RMS, max step, the real-time factor, and every onset and chord
change with its time in the file. **bounce 5 min** on the page does the same in a
Worker and offers the WAV for download. For a phone:
`afconvert -f m4af -d aac -b 256000 in.wav out.m4a` (macOS).

### Checks without the owner
- `node --test 'prototype/sky/test/*.test.mjs'` covers harmony, one clock,
  block-size independence and render safety (`test/sound.test.mjs`).
- `test/worklet.html` renders 20 s through the real AudioWorklet in an
  OfflineAudioContext, for 64 and 128 voices, and reports snapshots, sky time,
  peak and speed.
- Headless Chrome's `--screenshot --timeout` mode stalls AudioWorklets. Drive it
  over the DevTools protocol instead (`--remote-debugging-port`, Node's built-in
  `WebSocket`, `Runtime.evaluate`); `#…&autosound=1` with
  `--autoplay-policy=no-user-gesture-required --mute-audio` starts the sound without a click.

## Files
| File | What |
|---|---|
| `model.js` | the Sky Model (pure; ports 1:1 to C++) and `runSlow` |
| `data.js` | track loading, gap filling, quiet-level H baseline |
| `observer.js` | dipole frame, field lines, dome projection, MLT |
| `astro.js` | sun, moon (phase), sidereal time |
| `stars.js` | 111 bright stars (Yale BSC5, V ≤ 3.0) |
| `rng.js` | counter-based seeded noise |
| `dome.js` | WebGL renderer (Canvas 2D fallback) + instrument overlay |
| `charts.js`, `app.js`, `index.html`, `style.css` | the page |
| `export-goldens.mjs` | `node prototype/sky/export-goldens.mjs [day] [speed]` → `tests/golden/sky/` |
| `export-sound-golden.mjs` | `node prototype/sky/export-sound-golden.mjs [day] [from] [seconds]` → `tests/golden/sound/` (dry sound, chord requests, voicings; T3.1) |
| `sound/engine.js` | the sound engine (pure; owns the model clock; the `processBlock` analogue) |
| `sound/harmony.js` | chord vocabulary per Mood, progression on sky phases, voice-leading |
| `sound/bank.js` | complex one-pole resonators, double-banked |
| `sound/fx.js` | stand-in reverb, auto-gain, DC blocker, lookahead limiter |
| `sound/worklet.js`, `sound/panel.js` | the AudioWorklet and the page's SOUND block |
| `sound/render.js`, `sound/wav.js`, `sound/bounce-worker.js`, `bounce.mjs` | Journey bounces |
| `test/` | `node --test 'prototype/sky/test/*.test.mjs'`; `node prototype/sky/test/summary.mjs` prints phases and onsets per day; `worklet.html` |

Sources: sun after NOAA's solar calculator; moon after the Astronomical
Almanac's low-precision formulae; stars from the Yale Bright Star Catalogue
(5th ed., public domain) via github.com/brettonw/YaleBrightStarCatalog (MIT).
Sun and moon are checked against JPL Horizons (`test/horizons-cmo.json`).
