# Sky Model sketch (T1.2)

Browser sketch of `spec/04-sky-model.md`: a generative aurora, nudged by the
recorded days in `resources/sky/`, seen from CMO (College, Alaska) on an
all-sky dome. Plain ES modules, WebGL 1, no dependencies, no build step.

## Run
```sh
python3 -m http.server 8765 --bind 127.0.0.1        # from the repo root
open "http://127.0.0.1:8765/prototype/sky/#day=2025-01-19&t=2025-01-20T13:08&speed=6"
```
The URL hash keeps the review state (`day`, `t` UTC, `speed`, `seed`, and any
tuned constant as `p.<name>=<value>`), so a moment can be shared as a link.

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
| `test/` | `node --test 'prototype/sky/test/*.test.mjs'`; `node prototype/sky/test/summary.mjs` prints phases and onsets per day |

Sources: sun after NOAA's solar calculator; moon after the Astronomical
Almanac's low-precision formulae; stars from the Yale Bright Star Catalogue
(5th ed., public domain) via github.com/brettonw/YaleBrightStarCatalog (MIT).
Sun and moon are checked against JPL Horizons (`test/horizons-cmo.json`).
