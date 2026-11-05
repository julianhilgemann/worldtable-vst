#include "SlowSky.h"

#include "Astronomy.h"
#include "SkyRandom.h"

#include <cmath>
#include <limits>
#include <numbers>

namespace worldtable::sky
{
namespace
{
    using data::WorldChannel;

    constexpr double kSlowSeconds = 1.0;
    constexpr double kMinute = 60.0;

    double clamp (double x, double a, double b) noexcept { return x < a ? a : x > b ? b : x; }
    double lerp (double a, double b, double f) noexcept  { return a + (b - a) * f; }
    double relax (double x, double target, double dt, double tau) noexcept
    {
        return x + (target - x) * (1.0 - std::exp (-dt / tau));
    }
} // namespace

SlowSkyState initialSlowState (const data::WorldSnapshot& track, const SkyParams& p, double startUtc) noexcept
{
    return initialSlowState (p, startUtc, track.sample (WorldChannel::kp, startUtc));
}

SlowSkyState initialSlowState (const SkyParams& p, double startUtc, double kp0) noexcept
{
    SlowSkyState s {};
    s.timeUtc = startUtc;
    s.windSpeedKmS = 400.0;
    s.bzNt = 0.0;
    s.densityCm3 = 5.0;
    s.kp = 2.0;
    s.dhNt = 0.0;
    s.dhdtNt = 0.0;
    s.couplingMvM = 0.0;
    s.couplingSmoothMvM = 0.0;
    s.kpSmooth = kp0;
    s.activity = 0.1;
    s.gain = 1.0;
    s.reservoir = 0.4 * p.threshold;
    s.cycleThreshold = p.threshold;
    s.onsetReservoir = 0.0;
    s.unloading = 0.0;
    s.onsetStrength = 1.0;
    s.phaseStartUtc = startUtc;
    s.phaseDurationS = 0.0;
    s.lastOnsetUtc = -std::numeric_limits<double>::infinity();
    s.lastOnsetEventStrength = 0.0;
    s.lastOnsetDhdtNt = 0.0;
    s.eqLatDeg = p.eqBase - p.eqPerKp * kp0;
    s.poleLatDeg = p.poleBase;
    s.dhModelNt = 0.0;
    s.dhMeasuredSmoothNt = 0.0;
    s.mltHours = 0.0;
    s.nightFactor = 1.0;
    s.sunElDeg = 0.0;
    s.cycle = 0;
    s.onsetCount = 0;
    s.onsetNode = p.nodes >> 1;
    s.phase = SkyPhase::quiet;
    s.lastOnsetKind = OnsetKind::loaded;
    s.forced = false;
    return s;
}

int slowStep (SlowSkyState& s,
              const data::WorldSnapshot& track,
              const SkyParams& p,
              const Observer& obs,
              uint32_t seed,
              std::span<SkyEvent, kMaxEventsPerSlowStep> events) noexcept
{
    const double dt = kSlowSeconds;
    const double t = s.timeUtc;
    int eventCount = 0;
    const bool hValid = track.validAt (WorldChannel::hDeviation, t);

    // data
    s.windSpeedKmS = track.sample (WorldChannel::windSpeed, t);
    s.bzNt = track.sample (WorldChannel::bz, t);
    s.densityCm3 = track.sample (WorldChannel::density, t);
    s.kp = track.sample (WorldChannel::kp, t);
    s.dhNt = track.sample (WorldChannel::hDeviation, t);
    s.dhdtNt = s.dhNt - track.sample (WorldChannel::hDeviation, t - 5.0 * kMinute);
    s.couplingMvM = (s.windSpeedKmS * std::fmax (0.0, -s.bzNt)) / 1000.0;
    s.couplingSmoothMvM = relax (s.couplingSmoothMvM, s.couplingMvM, dt, p.eTauMin * 60.0);
    s.kpSmooth = relax (s.kpSmooth, s.kp, dt, p.kpTauMin * 60.0);

    // sun → magnetic local time at the station
    const auto sun = astro::sun (t, obs.station().latDeg, obs.station().lonDeg);
    s.mltHours = obs.mlt (obs.mlonDeg(), sun.subsolarLatDeg, sun.subsolarLonDeg);
    s.nightFactor = nightFactor (s.mltHours);
    s.sunElDeg = sun.elDeg;

    // reservoir: load from coupling, leak, release
    const double sinceOnsetMin = (t - s.lastOnsetUtc) / kMinute;
    const double releaseRate = s.phase == SkyPhase::onset || s.phase == SkyPhase::recovery
        ? (p.releaseFraction * s.onsetReservoir / (p.releaseTauMin * 60.0)) * std::exp (-sinceOnsetMin / p.releaseTauMin)
        : 0.0;
    const double quietLoad = p.baseLoadPerHour
                           * (0.6 + 0.4 * (rng::rand01 (seed, 3, rng::indexOf (std::floor (t / 600.0))) * 2.0));
    s.reservoir += ((p.loadPerHour * s.couplingSmoothMvM + quietLoad) / 3600.0
                    - s.reservoir / (p.leakHours * 3600.0) - releaseRate) * dt;
    s.reservoir = std::fmax (0.0, s.reservoir);
    s.unloading = clamp ((s.onsetStrength * releaseRate * p.releaseTauMin * 60.0) / p.threshold, 0.0, 2.5);

    // phases
    const bool nightside = s.mltHours >= 18.0 || s.mltHours <= 6.0;
    const bool forcedOk = s.dhdtNt <= p.forcedBayNt && s.dhNt <= p.forcedDeviationNt && nightside && hValid
                       && s.phase != SkyPhase::onset && sinceOnsetMin >= p.forcedRefractoryMin;
    const bool spontaneousOk = s.reservoir >= s.cycleThreshold && sinceOnsetMin >= p.refractoryMin
                            && s.phase != SkyPhase::onset;
    // pseudo-breakup: evaluated once per model minute, seeded by the minute index
    const double minuteIndex = std::floor (t / kMinute);
    const bool pseudoOk = std::fmod (t, kMinute) == 0.0 && sinceOnsetMin >= p.pseudoAfterMin
                       && s.phase != SkyPhase::onset
                       && rng::rand01 (seed, 15, rng::indexOf (minuteIndex)) < p.pseudoPerHour / 60.0;
    const double elapsed = t - s.phaseStartUtc;

    const auto enter = [&] (SkyPhase phase, double durationS)
    {
        s.phase = phase;
        s.phaseStartUtc = t;
        s.phaseDurationS = durationS;
        events[static_cast<size_t> (eventCount++)] = { t, 0.0f, 0.0f, -1, SkyEvent::Type::phase, phase,
                                                       OnsetKind::loaded, false };
    };

    if (forcedOk || spontaneousOk || pseudoOk)
    {
        const auto kind = forcedOk ? OnsetKind::forced : spontaneousOk ? OnsetKind::loaded : OnsetKind::pseudo;
        s.cycle += 1;
        s.onsetCount += 1;
        s.forced = forcedOk;
        const double strength = forcedOk ? clamp (-s.dhdtNt / 200.0, 0.6, 2.5)
                              : kind == OnsetKind::pseudo ? p.pseudoStrength : 1.0;
        s.onsetReservoir = std::fmin (std::fmax (s.reservoir, p.threshold * strength * (kind == OnsetKind::loaded ? 0.0 : 0.8)),
                                      p.maxRelease * p.threshold);
        s.reservoir = std::fmax (s.reservoir, s.onsetReservoir);
        // weakly driven onsets are softer than storm-driven ones
        s.onsetStrength = kind == OnsetKind::forced ? 1.0
                        : kind == OnsetKind::pseudo ? p.pseudoStrength
                        : clamp (0.35 + s.couplingSmoothMvM / 2.0, 0.35, 1.2);
        s.lastOnsetUtc = t;
        s.cycleThreshold = p.threshold * (1.0 + p.thresholdJitter * rng::randS (seed, 7, s.cycle));
        // onset near 23 MLT, jittered, clamped to the visible chain
        const double nodeOf23 = (p.nodes - 1) / 2.0
                              + (std::fmod (23.0 - s.mltHours + 36.0, 24.0) - 12.0) * 15.0 / (2.0 * p.spanDeg / (p.nodes - 1));
        // Math.round: halves round up
        s.onsetNode = static_cast<int32_t> (clamp (std::floor (nodeOf23 + 6.0 * rng::randS (seed, 9, s.cycle) + 0.5),
                                                   4.0, p.nodes - 5.0));
        enter (SkyPhase::onset, lerp (p.expansionMin.lo, p.expansionMin.hi, rng::rand01 (seed, 11, s.cycle)) * kMinute);

        s.lastOnsetKind = kind;
        s.lastOnsetEventStrength = s.onsetStrength * s.onsetReservoir / p.threshold;
        s.lastOnsetDhdtNt = s.dhdtNt;
        events[static_cast<size_t> (eventCount++)] = { t,
                                                       static_cast<float> (s.lastOnsetEventStrength),
                                                       static_cast<float> (s.dhdtNt),
                                                       static_cast<int16_t> (s.onsetNode),
                                                       SkyEvent::Type::onset,
                                                       SkyPhase::onset,
                                                       kind,
                                                       false };
    }
    else if (s.phase == SkyPhase::onset && elapsed >= s.phaseDurationS)
    {
        enter (SkyPhase::recovery, lerp (p.recoveryMin.lo, p.recoveryMin.hi, rng::rand01 (seed, 13, s.cycle)) * kMinute);
    }
    else if (s.phase == SkyPhase::recovery && elapsed >= s.phaseDurationS)
    {
        enter (s.couplingSmoothMvM > p.growthE ? SkyPhase::growth : SkyPhase::quiet, 0.0);
    }
    else if (s.phase == SkyPhase::quiet && s.couplingSmoothMvM > p.growthE)
    {
        enter (SkyPhase::growth, 0.0);
    }
    else if (s.phase == SkyPhase::growth && s.couplingSmoothMvM < 0.5 * p.growthE)
    {
        enter (SkyPhase::quiet, 0.0);
    }

    // activity and oval
    const double actTarget = clamp (0.06 + 0.55 * (s.kpSmooth / 9.0) + 0.3 * std::tanh (s.couplingSmoothMvM / 8.0), 0.0, 1.0);
    s.activity = relax (s.activity, actTarget, dt, p.activityTauMin * 60.0);
    const double growth = s.phase == SkyPhase::growth ? clamp (s.reservoir / s.cycleThreshold, 0.0, 1.0) : 0.0;
    const double eqTarget = p.eqBase - p.eqPerKp * s.kpSmooth - p.growthDriftDeg * growth
                          + p.expansionPoleDeg * std::fmin (1.0, s.unloading);
    s.eqLatDeg = relax (s.eqLatDeg, eqTarget, dt, 6.0 * 60.0);
    s.poleLatDeg = relax (s.poleLatDeg, p.poleBase - p.polePerKp * s.kpSmooth + p.expansionPoleDeg * std::fmin (1.0, s.unloading),
                          dt, 6.0 * 60.0);

    // assimilation: pull the disturbance gain so |ΔH model| tracks |ΔH measured|
    const double arcLat = arcLatitudeDeg (s, p);
    const double proximity = std::exp (-std::pow ((obs.mlatDeg() - arcLat) / 5.0, 2.0));
    s.dhModelNt = -p.dhScaleNt * s.gain * (0.25 * s.activity + s.unloading) * proximity * s.nightFactor;
    s.dhMeasuredSmoothNt = relax (s.dhMeasuredSmoothNt, std::fabs (s.dhNt), dt, 5.0 * 60.0);
    if (hValid)
    {
        const double dz = p.gainDeadZoneNt;
        const double err = std::log (std::fmax (s.dhMeasuredSmoothNt, dz)) - std::log (std::fmax (std::fabs (s.dhModelNt), dz));
        // inside the dead zone the gain drifts back to 1 over about an hour
        const double pull = err != 0.0 ? err / (p.gainTauMin * 60.0) : -std::log (s.gain) / 3600.0;
        s.gain = clamp (s.gain * std::exp (pull * dt), p.gainRange.lo, p.gainRange.hi);
    }

    s.timeUtc = t + kSlowSeconds;
    return eventCount;
}

} // namespace worldtable::sky
