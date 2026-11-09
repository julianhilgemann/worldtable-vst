#include "SkyModel.h"

#include "Astronomy.h"
#include "SkyRandom.h"
#include "SlowSky.h"

#include <cmath>
#include <limits>
#include <numbers>

// prototype/sky/model.js `SkyModel`, line by line: constructor, seek, step (the curtain on real
// time), snapshot. The slow sky is SlowSky.cpp (T2.2); sun, moon and the dome frame are
// Astronomy.cpp and Observer.cpp.

namespace worldtable::sky
{
namespace
{
    constexpr double kPi = std::numbers::pi;
    constexpr double kTick = SkyModel::kTickSeconds;
    constexpr int64_t kMicrosPerSecond = 1'000'000;
    constexpr int64_t kMicrosPerSlowStep = static_cast<int64_t> (SkyModel::kSlowStepSeconds) * kMicrosPerSecond;

    double clamp (double x, double a, double b) noexcept { return x < a ? a : x > b ? b : x; }
    double lerp (double a, double b, double f) noexcept  { return a + (b - a) * f; }
    double relax (double x, double target, double dt, double tau) noexcept
    {
        return x + (target - x) * (1.0 - std::exp (-dt / tau));
    }
    bool same (double a, double b) noexcept { return ! (a < b) && ! (a > b); }

    /** Whole µs of a UTC time on the slow grid or a seek target (finite, integral seconds). */
    int64_t toMicros (double utc) noexcept { return std::llround (utc * 1e6); }

    int nodesFor (const SkyParams& p) noexcept
    {
        return p.nodes < 2 ? 2 : p.nodes > SkySnapshot::kMaxNodes ? SkySnapshot::kMaxNodes : p.nodes;
    }
} // namespace

SkyModel::SkyModel() noexcept
    : obs (data::stationInfo (data::StationId::CMO), SkyParams {}.poleLatDeg, SkyParams {}.poleLonDeg)
{
    slow = initialSlowState (params, 0.0, 2.0);   // quiet: climatology Kp, no track
    layOutCurtain (false);
    writeSnapshot (true);
}

//==================================================================================================
void SkyModel::setTimeline (const SkyTimeline* next) noexcept
{
    const SkyTimeline* previous = timeline;
    timeline = next != nullptr && next->track != nullptr && ! next->minutes.empty() ? next : nullptr;

    if (timeline == nullptr)
    {
        writeSnapshot (false);   // model time holds; the curtain keeps moving on the last slow state
        return;
    }

    const bool reseek = previous == nullptr || timeline->seed != seed || ! (timeline->params == params);
    params = timeline->params;
    seed = timeline->seed;
    obs = Observer (data::stationInfo (timeline->track->station), params.poleLatDeg, params.poleLonDeg);

    if (curtain.nodes != nodesFor (params) || ! same (curtain.spanDeg, params.spanDeg)
        || ! same (curtain.stationMlonDeg, obs.mlonDeg()) || curtain.seed != seed)
        layOutCurtain (true);

    if (reseek)
        seek (modelTimeUtc());
    else
        writeSnapshot (true);
}

void SkyModel::seek (double modelTimeUtc) noexcept
{
    if (timeline == nullptr)
        return;

    const auto& track = *timeline->track;
    // The sketch's seek: floor to the 1 s grid, clamp to [start, end − 1 s] (NaN → start).
    double target = std::floor (modelTimeUtc);
    if (! (target >= track.startUtc))
        target = track.startUtc;
    if (target > track.endUtc() - kSlowStepSeconds)
        target = track.endUtc() - kSlowStepSeconds;

    // The checkpoint at or before target + 1 s: the steps up to and including `target` are what is
    // left, at most 59 of them.
    const auto* checkpoint = timeline->checkpointAtOrBefore (target + kSlowStepSeconds);
    if (checkpoint == nullptr)
        return;

    slow = *checkpoint;
    std::array<SkyEvent, kMaxEventsPerSlowStep> discarded {};   // seek emits no events (sketch)
    seekSlowSteps = 0;
    while (slow.timeUtc <= target && seekSlowSteps <= kMaxSeekSlowSteps)
    {
        slowStep (slow, track, params, obs, seed, discarded);
        ++seekSlowSteps;
    }

    modelMicros = toMicros (target);
    ++seekCount;
    writeSnapshot (true);
}

void SkyModel::setSpeed (double modelSecondsPerSecond) noexcept
{
    speedFactor = std::isnan (modelSecondsPerSecond) ? 0.0 : clamp (modelSecondsPerSecond, 0.0, kMaxSpeed);
    microsPerTick = std::llround (speedFactor * kTickSeconds * static_cast<double> (kMicrosPerSecond));
    snapshot.speed = static_cast<float> (speedFactor);
}

//==================================================================================================
std::span<const SkyEvent> SkyModel::step() noexcept
{
    eventCount = 0;
    ++ticks;
    bool slowStepped = false;

    // model time: the slow sky catches up on its 1 s grid
    if (timeline != nullptr && microsPerTick > 0)
    {
        const auto& track = *timeline->track;
        const int64_t endMicros = toMicros (track.endUtc());

        if (modelMicros < endMicros - kMicrosPerSlowStep)
        {
            modelMicros += microsPerTick;
            std::array<SkyEvent, kMaxEventsPerSlowStep> stepEvents {};

            for (int k = 0; k < kMaxSlowStepsPerTick; ++k)
            {
                const int64_t t = toMicros (slow.timeUtc);
                if (t > modelMicros || t >= endMicros)
                    break;

                const int n = slowStep (slow, track, params, obs, seed, stepEvents);
                for (int e = 0; e < n; ++e)
                    pushEvent (stepEvents[static_cast<size_t> (e)]);
                slowStepped = true;
            }
        }
    }

    stepCurtain();
    writeSnapshot (slowStepped);
    return { events.data(), static_cast<size_t> (eventCount) };
}

//==================================================================================================
void SkyModel::layOutCurtain (bool keepMotion) noexcept
{
    const int n = nodesFor (params);

    if (! keepMotion || curtain.nodes != n)
    {
        for (size_t i = 0; i < static_cast<size_t> (SkySnapshot::kMaxNodes); ++i)
        {
            curtain.y[i] = 0.0;
            curtain.vy[i] = 0.0;
            curtain.brightness[i] = i < static_cast<size_t> (n) ? 0.05 : 0.0;
            curtain.pulse[i] = 0.0;
            snapshot.nodes[i] = {};
        }
    }

    for (int i = 0; i < n; ++i)
        curtain.mlonDeg[static_cast<size_t> (i)] = obs.mlonDeg() + lerp (-params.spanDeg, params.spanDeg, i / static_cast<double> (n - 1));

    // pulsating patches of 6 nodes: seeded period and phase, constant per seed
    const int patches = (n + 5) / 6;
    for (int k = 0; k < patches; ++k)
    {
        const auto kk = static_cast<uint32_t> (k);
        curtain.pulsePeriodS[static_cast<size_t> (k)] = lerp (params.pulsePeriodS.lo, params.pulsePeriodS.hi, rng::rand01 (seed, 41, kk));
        curtain.pulsePhase[static_cast<size_t> (k)] = 2.0 * kPi * rng::rand01 (seed, 43, kk);
    }

    curtain.nodes = n;
    curtain.spanDeg = params.spanDeg;
    curtain.stationMlonDeg = obs.mlonDeg();
    curtain.seed = seed;
}

void SkyModel::stepCurtain() noexcept
{
    // The curtain: a damped, driven wave equation along the chain, on real time.
    const auto& p = params;
    const auto& s = slow;
    const int n = curtain.nodes;
    const double tr = static_cast<double> (ticks) * kTick;

    // Differences in whole µs, as exact as the sketch's integer milliseconds.
    const double sinceOnsetMin = std::isfinite (s.lastOnsetUtc)
        ? static_cast<double> (modelMicros - toMicros (s.lastOnsetUtc)) / (60.0 * kMicrosPerSecond)
        : std::numeric_limits<double>::infinity();
    const double phaseElapsedS = static_cast<double> (modelMicros - toMicros (s.phaseStartUtc)) / kMicrosPerSecond;

    const bool inExpansion = s.phase == SkyPhase::onset;
    const double surgeCenter = s.onsetNode - 1.2 * std::fmax (0.0, sinceOnsetMin);       // westward travelling surge
    const double front = 3.0 + 40.0 * (1.0 - std::exp (-std::fmax (0.0, sinceOnsetMin) / 5.0));   // expansion width (nodes)
    const double drive = p.forceKm * (0.15 + s.activity) * std::sqrt (s.gain);
    const double c2 = p.waveC * p.waveC;
    const double recoveryPulse = s.phase == SkyPhase::recovery
        ? std::sin (kPi * clamp (phaseElapsedS / std::fmax (0.001, s.phaseDurationS), 0.0, 1.0))
        : 0.0;
    const double pc = 1.0 + 0.08 * s.activity * std::sin ((2.0 * kPi * tr) / p.pcPeriodS);
    const double stormRed = clamp ((s.kpSmooth - 5.0) / 4.0, 0.0, 1.0);
    const double quietRed = s.phase == SkyPhase::quiet || s.phase == SkyPhase::recovery ? 1.0 : 0.4;
    const double arcBase = arcLatitudeDeg (s, p);

    auto& y = curtain.y;
    auto& vy = curtain.vy;
    auto& b = curtain.brightness;

    for (int i = 0; i < n; ++i)
    {
        const auto u = static_cast<size_t> (i);
        const double di = static_cast<double> (i);
        // in place, like the sketch: the left neighbour has already moved this tick
        const double yl = y[i > 0 ? u - 1 : 0], yr = y[i < n - 1 ? u + 1 : static_cast<size_t> (n - 1)];
        double f = drive * rng::fbm2 (seed, 21, di / 7.0, tr / 3.0);
        if (inExpansion)
        {
            const double x = (di - surgeCenter) / 3.0;
            f += p.surgeKm * s.unloading * std::exp (-(x * x)) * std::sin (tr * 1.7 + di * 0.4);
        }
        const double acc = c2 * (yl - 2.0 * y[u] + yr) - p.damping * vy[u] - p.stiffness * y[u] + f;
        vy[u] += acc * kTick;
        y[u] += vy[u] * kTick;
        if (y[u] > p.displacementLimitKm)  { y[u] = p.displacementLimitKm;  vy[u] *= -0.3; }
        if (y[u] < -p.displacementLimitKm) { y[u] = -p.displacementLimitKm; vy[u] *= -0.3; }

        const double mltI = s.mltHours + (curtain.mlonDeg[u] - obs.mlonDeg()) / 15.0;
        const double night = nightFactor (mltI);
        const double patch = 0.5 + 0.5 * rng::fbm2 (seed, 31, di / 10.0, tr / 25.0);
        double envelope = 0.0;
        if (inExpansion || s.unloading > 0.02)
        {
            const double x = (di - s.onsetNode) / front;
            envelope = std::exp (-(x * x));
        }
        // pulsating aurora in recovery: patches of 6 nodes, each with its own seeded period
        const auto k = static_cast<size_t> (i / 6);
        const double pulse = 0.5 + 0.5 * std::sin ((2.0 * kPi * tr) / curtain.pulsePeriodS[k] + curtain.pulsePhase[k]);
        curtain.pulse[u] = pulse;
        const double pulseGain = 1.0 + 0.8 * recoveryPulse * (pulse - 0.5) * (patch > 0.45 ? 1.0 : 0.3);
        const double motion = 1.0 + std::fmin (1.5, std::fabs (vy[u]) / 120.0);
        const double target = night * s.gain * (0.09 + 0.45 * s.activity * (0.55 + 0.45 * patch) + 1.1 * s.unloading * envelope)
                            * motion * pulseGain * pc;
        b[u] = relax (b[u], target, kTick, target > b[u] ? p.riseTauS : p.fallTauS);

        auto& node = snapshot.nodes[u];
        node.displacementKm = static_cast<float> (y[u]);
        node.velocityKmS = static_cast<float> (vy[u]);
        node.brightness = static_cast<float> (b[u]);
        node.green = static_cast<float> (clamp (b[u], 0.0, 2.5));
        node.red = static_cast<float> (clamp (b[u] * (0.15 + 0.4 * quietRed) + 0.7 * stormRed * night, 0.0, 2.5));
        node.violet = static_cast<float> (clamp (b[u] * (0.1 + 0.9 * s.unloading * envelope), 0.0, 2.5));
        const double arc = arcBase + (p.dayShiftDeg * (1.0 - std::cos ((2.0 * kPi * mltI) / 24.0))) / 2.0;
        node.mlatDeg = static_cast<float> (arc + y[u] / 111.2);
        node.mlonDeg = static_cast<float> (curtain.mlonDeg[u]);
        node.pulse = static_cast<float> (pulse);
    }
    curtain.recoveryPulse = recoveryPulse;

    // whistlers / chorus: seeded Poisson events, the rate grows with activity
    if (rng::rand01 (seed, 51, static_cast<uint32_t> (ticks))
        < p.whistlerPerS * s.activity * s.activity * kTick * (1.0 + 2.0 * s.unloading))
    {
        curtain.whistlers += 1;
        const bool falling = rng::rand01 (seed, 53, curtain.whistlers) < 0.8;
        const double strength = 0.3 + 0.7 * rng::rand01 (seed, 55, curtain.whistlers);
        curtain.lastWhistler = { ticks, static_cast<float> (strength), falling };
        pushEvent ({ modelTimeUtc(), static_cast<float> (strength), 0.0f, -1, SkyEvent::Type::whistler, s.phase,
                     OnsetKind::loaded, falling });
    }
}

void SkyModel::writeSnapshot (bool refreshSun) noexcept
{
    const auto& s = slow;
    const auto& p = params;
    auto& o = snapshot;
    const double phaseElapsedS = static_cast<double> (modelMicros - toMicros (s.phaseStartUtc)) / kMicrosPerSecond;

    o.modelTimeUtc = modelTimeUtc();
    o.step = ticks;
    o.speed = static_cast<float> (speedFactor);

    o.phase = s.phase;
    o.phaseProgress = static_cast<float> (s.phase == SkyPhase::growth ? clamp (s.reservoir / s.cycleThreshold, 0.0, 1.0)
                                        : s.phase == SkyPhase::quiet  ? 0.0
                                        : clamp (phaseElapsedS / std::fmax (0.001, s.phaseDurationS), 0.0, 1.0));
    o.energy = static_cast<float> (s.reservoir / p.threshold);
    o.activity = static_cast<float> (s.activity);
    o.unloading = static_cast<float> (s.unloading);
    o.gain = static_cast<float> (s.gain);
    o.couplingMvM = static_cast<float> (s.couplingMvM);

    o.kp = static_cast<float> (s.kp);
    o.bzNt = static_cast<float> (s.bzNt);
    o.windSpeedKmS = static_cast<float> (s.windSpeedKmS);
    o.densityCm3 = static_cast<float> (s.densityCm3);
    o.dhMeasuredNt = static_cast<float> (s.dhNt);
    o.dhModelNt = static_cast<float> (s.dhModelNt);

    o.mltHours = static_cast<float> (s.mltHours);
    o.ovalLatitudeDeg = static_cast<float> (arcLatitudeDeg (s, p));
    o.eqLatitudeDeg = static_cast<float> (s.eqLatDeg);
    o.poleLatitudeDeg = static_cast<float> (s.poleLatDeg);
    o.dayShiftDeg = static_cast<float> (p.dayShiftDeg);
    o.stormRed = static_cast<float> (clamp ((s.kpSmooth - 5.0) / 4.0, 0.0, 1.0));
    o.pcPhase = static_cast<float> (std::fmod ((static_cast<double> (ticks) * kTick) / p.pcPeriodS, 1.0));
    o.recoveryPulse = static_cast<float> (curtain.recoveryPulse);

    o.onsetCount = s.onsetCount;
    o.whistlerCount = curtain.whistlers;
    o.lastOnset = { s.onsetCount > 0 ? s.lastOnsetUtc : 0.0,
                    static_cast<float> (s.lastOnsetEventStrength),
                    static_cast<float> (s.lastOnsetDhdtNt),
                    static_cast<int16_t> (s.onsetNode),
                    s.lastOnsetKind };
    o.lastWhistler = curtain.lastWhistler;

    const auto& station = obs.station();
    if (refreshSun)
    {
        const auto sun = astro::sun (o.modelTimeUtc, station.latDeg, station.lonDeg);
        const auto moon = astro::moon (o.modelTimeUtc, station.latDeg, station.lonDeg);
        o.astronomy.sunAzDeg = static_cast<float> (sun.azDeg);
        o.astronomy.sunElDeg = static_cast<float> (sun.elDeg);
        o.astronomy.moonAzDeg = static_cast<float> (moon.azDeg);
        o.astronomy.moonElDeg = static_cast<float> (moon.elDeg);
        o.astronomy.moonIllumination = static_cast<float> (moon.illumination);
        o.astronomy.moonWaxing = moon.waxing;
    }

    o.observer.latDeg = static_cast<float> (station.latDeg);
    o.observer.lonDeg = static_cast<float> (station.lonDeg);
    o.observer.elevKm = static_cast<float> (station.elevKm);
    o.observer.mlatDeg = static_cast<float> (obs.mlatDeg());
    o.observer.mlonDeg = static_cast<float> (obs.mlonDeg());
    o.observer.poleLatDeg = static_cast<float> (p.poleLatDeg);
    o.observer.poleLonDeg = static_cast<float> (p.poleLonDeg);
    o.observer.magZenithAzDeg = static_cast<float> (obs.magneticZenith().azDeg);
    o.observer.magZenithElDeg = static_cast<float> (obs.magneticZenith().elDeg);

    o.nodeCount = curtain.nodes;
}

bool SkyModel::pushEvent (const SkyEvent& event) noexcept
{
    if (eventCount >= kMaxEventsPerStep)
        return false;
    events[static_cast<size_t> (eventCount++)] = event;
    return true;
}

} // namespace worldtable::sky
