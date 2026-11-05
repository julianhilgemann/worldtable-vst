# 04 — Sky Model

One simplified, generative aurora model, *nudged* by real data, is the single
source of truth (ADR-0002). The data sets the mean level; the model adds
plausible, seeded detail around it. Sound, dome and OSC are renderings of the
same `SkySnapshot` (`03-architecture.md`).

Status: **sketched** (T1.2, `prototype/sky/`, 2026-11-05). The browser sketch
implements this spec; its tuned constants are in §9 and its goldens in
`tests/golden/sky/`. The owner's look at it is still pending. Physics here is
deliberately simplified: plausible and beautiful beats correct.

## 1. Clocks
Two clocks, so a Journey at any speed shows the same substorms with natural
motion:
- **Model time** (UTC) drives data, the reservoir, substorm phases,
  assimilation and the oval. It advances on a **fixed 1 s grid**, independent
  of the Journey speed, so the slow sky is bit-identical at 1×, 24× or 1440×
  (tested in the sketch). Model time selects data from the `WorldSnapshot`
  track and drives astronomy and magnetic local time (MLT).
- **Real time** drives the curtain waves, brightness, pulsations and
  whistlers: ticks at **100 Hz** of audio sample time (ADR-0007). One tick
  advances model time by 10 ms × speed (Journey, up to 1440×) or 0 (Freeze:
  the curtain keeps breathing in place).
Rule: anything with a period under about a minute lives on real time.

## 2. State

### 2.1 Energy reservoir and substorm phases
Loading from solar-wind coupling, release in substorms:

```
coupling  C(t) = v · Bs,   Bs = max(0, −Bz)          (km/s · nT; Newell's function is the alternative to try)
reservoir dW/dt = α·C − W/τ_leak − R(t)
```

| Phase | Enters when | Typical length | Look | Sound cue |
|---|---|---|---|---|
| **Quiet** | W low, C ≈ 0 | hours | faint, still arc, poleward | stable home chord |
| **Growth** | C > 0 sustained | 30–60 min | arc brightens slowly, drifts equatorward | suspensions, rising tension |
| **Onset / expansion** | W > W_c (seeded jitter), or a data-forced onset (§3) | 10–30 min | sudden brightening, rays, folds, westward surge, corona if overhead | broadband strike, chromatic-mediant shift |
| **Recovery** | release R(t) has drained W | 1–2 h | patchy, pulsating aurora | bittersweet borrowed chord resolving home |

R(t) is a release pulse shaped by the onset (fast rise, ~10 min decay).
Substorms typically recur every 2–4 h while coupling stays on.

### 2.2 Curtain
A chain of **N nodes (64–128)** along magnetic longitude, spanning the sector
visible from the station (≈ ±1,200 km along the oval), so every node can be
seen and heard. Per node *i*:

```
ÿᵢ = c²·(yᵢ₊₁ − 2yᵢ + yᵢ₋₁) − γ·ẏᵢ − k·yᵢ + Fᵢ(t)        damped, driven wave equation
```
- `yᵢ` displacement (folds), `ẏᵢ` velocity: folds and ripples travel along the chain.
- `Fᵢ(t)`: smooth seeded noise scaled by activity, plus an onset impulse at the
  onset longitude that spreads westward.
- Brightness `bᵢ` relaxes toward a target set by energy, phase, pulsation and
  `|ẏᵢ|` (moving folds brighten), time constant ~1–5 s.
- Emission mix per node (`red`, `green`, `violet`, each 0..1):
  green (557.7 nm, ~110 km) = body of the arc; red (630.0 nm, ~250 km) = soft,
  high, slow glow, strongest in quiet/recovery and in great storms;
  violet (427.8 nm N₂⁺, ~100 km) = energetic lower border, strongest at onset.
- Oval latitude: the arc's geomagnetic latitude drifts equatorward in growth
  and with Kp (rough start: ≈ 66° − 2°·Kp at magnetic midnight; tune).

### 2.3 Pulsations
- Slow brightness pulsing (pulsating aurora, periods ~2–20 s), mainly in recovery.
- Field-line ringing (Pc3–Pc5, ~10 s to ~10 min), amplitude follows activity.
Both are phases in the snapshot so sound and image pulse together.

### 2.4 Events
- **Onset:** counter + strength; fires triggers (resonator strike, chord step).
- **Whistler / chorus:** sparse events when activity is high (seeded Poisson,
  rate ∝ activity); each has a direction (falling or rising) and strength.

## 3. Data assimilation
Real data nudges the model; it never replaces it.
- **Kp → activity level** and oval latitude (slow, τ ≈ 10 min).
- **v, Bz → coupling** C(t) (§2.1), straight from the track.
- **H deviation at the station → disturbance gain.** The model predicts a
  ground disturbance ΔH_model (proportional to release rate × brightness near
  the station's longitude). A slow nudging term pulls the activity gain so
  that |ΔH_model| tracks |ΔH_measured| (τ ≈ 10 min).
- **Data-forced onset:** a negative H bay steeper than a threshold (start:
  −50 nT in 5 min, tune) near the station's magnetic midnight forces an onset,
  so real substorms appear in the sky when they happened.
- Missing data: hold the last valid value and decay toward climatology
  (Kp 2, v 400 km/s, Bz 0) over ~1 h.
- Seeded randomness keeps it reproducible (freeze, offline render).

## 4. Observer and dome projection (ADR-0006)
- Station: geographic lat/lon (CMO 64.87° N, 147.86° W) → geomagnetic frame
  (centred dipole; IGRF pole for the epoch) → magnetic local time from model time.
- Curtain points at altitude (100 / 110 / 250 km by emission line) and
  geomagnetic lat/lon → local east-north-up from the observer → elevation and
  azimuth → fisheye onto the dome (equidistant: radius ∝ 90° − elevation).
- Rays follow field lines and converge at **magnetic zenith** (inclination at
  CMO ≈ 77°); a curtain overhead becomes a corona.
- Sound follows the dome: azimuth → stereo position, elevation × brightness → presence.

## 5. Astronomy (lightweight)
- Sun: NOAA solar-position algorithm (simplified), alt/az.
- Moon: low-precision position (~0.3°) and phase.
- Stars: ~100 brightest (V ≤ 2.5, Yale Bright Star Catalogue, public domain),
  rotated by local sidereal time.
- Computed at ~1 Hz into the snapshot; daytime only changes the look (a faint
  light-blue shimmer at the rim), never the sound.

## 6. Outputs
`SkySnapshot` (`03-architecture.md` §B): model time, phase + progress, energy,
activity, Kp/Bz/v/n, node array (displacement, velocity, brightness, r/g/v),
oval latitude, pulsation phases, event counters, astronomy. Mod-matrix sky
channels (energy, onset, brightness, fold motion, pulsation, Kp, Bz, wind) are
derived from it, normalised 0..1 and slewed.

## 7. Tuning constants (internal, not parameters)
N, c, γ, k, α, τ_leak, W_c and its jitter, release shape, brightness time
constants, pulsation ranges, assimilation gains and thresholds. Macros expose
only musical meaning (Storm scales sensitivity to the data; see `01` §6).

## 8. Questions for the prototype (T1.2) — answers from the sketch
- **N = 64 is enough for the image:** the renderer interpolates 4 columns per
  node. For sound, decide in T1.3.
- **`v·Bs` is convincing on 10 May 2024,** but on its own it fired an onset
  every ~40 min. A 90 min refractory time and a release cap (3 × threshold)
  shape it into a substorm every ~1.5 h through the storm. Newell's coupling
  isn't needed yet.
- **Quiet days:** loading alone never reaches the threshold at Kp ≤ 1, so
  seeded **pseudo-breakups** (0.25 per hour after 2.5 h without an onset,
  strength 0.4) give about one soft event per 8 h. Continuous curtain motion
  does the rest.
- **H-bay threshold:** −45 nT per 5 min gave 50 onsets in 48 h on the moderate
  day. **−80 nT per 5 min while ΔH ≤ −150 nT** (nightside, 60 min refractory)
  catches CMO's ~11 UT bays on both nights (onsets 11:30 and 11:01 UT) with few
  extras.
- **H baseline:** the median of the calmest 6 h of the track (the lowest
  standard deviation). It's robust on all three days; Live needs a rolling
  version.

## 9. Tuned constants (sketch defaults, 2026-11-05)
Source of truth: `prototype/sky/model.js` `DEFAULTS` (also stored in the
goldens).

| Group | Constants |
|---|---|
| Coupling | E = v·max(0, −Bz)/1000 mV/m, smoothed τ 5 min; Kp smoothed τ 20 min |
| Reservoir | load 0.8 /h per mV/m + quiet 0.2 /h (seeded ±40 % per 10 min); leak τ 2 h; threshold 2.5 (±30 % per cycle) |
| Release | 70 % of W₀ over τ 10 min, W₀ ≤ 3 thresholds; strength × (0.35 + E/2), ≤ 1.2 for loaded onsets |
| Phases | expansion 14–26 min, recovery 50–100 min (seeded); refractory 90 min; growth when E > 0.5 mV/m |
| Data-forced onset | ΔH over 5 min ≤ −80 nT and ΔH ≤ −150 nT, MLT 18–06, refractory 60 min |
| Assimilation | gain τ 10 min, dead zone 60 nT (inside it the gain drifts back to 1 over ~1 h), range 0.4–2.5; model ΔH = −700 nT × gain × (0.25·activity + unloading) × proximity × night |
| Oval (midnight) | equatorward edge 66.5° − 2.0°·Kp, poleward edge 75° − 0.45°·Kp, +7° at noon; arc halfway between; growth −1.5°, expansion +2.5° poleward |
| Curtain | N 64 over station maglon ± 25°; c 2.2 nodes/s, γ 0.35 /s, k 0.03 /s²; forcing 900 km/s² × (0.15 + activity) × √gain; surge 4000 km/s², moving west 1.2 nodes/min; folds ≤ ±160 km |
| Brightness | base 0.09 + 0.45·activity·patch + 1.1·unloading·envelope; × night factor × gain × motion × pulsation; rise τ 0.5 s, fall τ 3 s |
| Events | pulsating patches of 6 nodes, 3–14 s; Pc ringing 40 s; whistlers 0.08 /s × activity² × (1 + 2·unloading) |

## 10. Open after the sketch
- **One arc is too few for great storms.** On 10 May 2024 the arc stays at
  59–68° geomagnetic latitude, low in the southern sky from CMO, while the
  real storm put aurora overhead as well. Options: a second, poleward arc
  during storms, or an arc position that follows activity.
- **A full corona needs strong folds overhead.** The geometry is right (an
  overhead arc collapses edge-on to a band through magnetic zenith, and rays
  converge there), but the folds are too small for a radiating corona.
- **Snapshot fields the renderer needs** beyond `03-architecture.md` §B:
  per-node geomagnetic latitude and longitude, the oval edges (`eqLat`,
  `poleLat`), gain, unloading, coupling, MLT, the last whistler. Folded
  into `source/sky/SkySnapshot.h` (T2.1), with the onset node.
- **What the sound reads** (T1.3): per-node brightness, displacement, r/g/v and
  geomagnetic position (for the dome azimuth → pan); activity; onset events
  with strength and the **onset node** (the strike is centred there; the sketch
  reads it from the model's slow state); phase changes with the model time.
  The sound runs in the same audio callback, so in C++ it can read the model
  directly; the UI and OSC still get only the published snapshot.
