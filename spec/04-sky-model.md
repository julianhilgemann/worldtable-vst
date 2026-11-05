# 04 — Sky Model

One simplified, generative aurora model, *nudged* by real data, is the single
source of truth (ADR-0002). The data sets the mean level; the model adds
plausible, seeded detail around it. Sound, dome and OSC are renderings of the
same `SkySnapshot` (`03-architecture.md`).

Status: **draft**. Equations and constants are starting points to tune in the
browser prototype (T1.2) against the 10 May 2024 storm, then freeze as
goldens for the C++ port. Physics here is deliberately simplified: plausible
and beautiful beats correct.

## 1. Clock
- Control rate **100 Hz** of audio sample time (ADR-0007). `dt = 0.01 s` of
  model time × time compression (Journey) or × 0 for data in Freeze.
- Model time (UTC) selects data from the `WorldSnapshot` track and drives
  astronomy and magnetic local time (MLT).

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

## 8. Questions for the prototype (T1.2)
- N = 64 or 128 for sound vs image (could differ: image interpolates).
- Does `v·Bs` give a convincing growth phase on 10 May 2024, or is Newell's
  coupling needed?
- How often should onsets happen on a quiet day so it never feels dead?
- What H-bay threshold catches CMO's real onsets without false ones?
