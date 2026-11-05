# ADR-0006 — The observer: an all-sky dome above a magnetometer station

**Status:** Accepted (owner, sessions 5–6, 2026-11-05)
**Supersedes:** the Curtain vs Oval choice (Hero Concepts canvas, boards A and B).

## Decision
- **A person on the ground, looking up.** The hero view is an all-sky dome:
  zenith in the centre, horizon at the rim, like the all-sky cameras used to
  observe aurora. The curtain is modelled in space (100–250 km altitude,
  geomagnetic coordinates) and projected fisheye onto the dome. When it passes
  overhead the rays converge at magnetic zenith (corona): the visual climax of
  an onset.
- **Observer = the station.** Each magnetometer station is one point of view;
  the hypothetical observer stands there, so the local nT really is "your" sky.
  No attempt to be correct everywhere. Stations are picked for visual appeal
  (arcs crossing the sky at a good angle). v1 starts with **CMO (College,
  Alaska)**, already in the POC; each added station adds a point of view
  (candidate: Tromsø, licence to check).
- **Real sky, simplified astronomy.** Sun position, moon (position and phase)
  and a small set of bright stars for the site, computed from time and
  location with lightweight formulas. Everything moves together, in real time
  and in Journey time-lapses.
- **Darkness is the default look.** Daytime, when shown, is stylistic only: a
  faint light-blue shimmer at the rim, aurora still visible. Toggles: day/night
  tracking, stars and moon, gradients only.
- **Sound follows the dome:** azimuth → stereo position; elevation and
  brightness → presence. Ambisonic or binaural output is a later option.
- **TouchDesigner:** the dome maps directly to fulldome / planetarium projection.

## Consequences
- The Sky Model needs an observer projection (station → geomagnetic frame →
  dome) and an astronomy module (`spec/04-sky-model.md`).
- The TP.2 ambient background is replaced by the dome renderer; the ambient
  switcher question becomes "dome detail level", if anything.
