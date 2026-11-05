#pragma once

namespace worldtable::sky
{

struct Range
{
    double lo, hi;
};

/** Sky Model tuning constants (spec/04-sky-model.md §7, §9). Internal: not plug-in parameters.

    Names and defaults are the T1.2 sketch's `DEFAULTS` (prototype/sky/model.js), stored with the
    goldens (`params` in tests/golden/sky/<day>.json); a unit test keeps the two equal. Units are in the
    names. Slow constants act on model time, fast ones on real time (spec/04 §1). */
struct SkyParams
{
    int    nodes   = 64;              ///< curtain nodes, 64 or 128 (≤ SkySnapshot::kMaxNodes)
    double spanDeg = 25;              ///< the chain covers the station's magnetic longitude ± this

    //== Slow: reservoir and phases (model time) ======================================================
    double eTauMin          = 5;      ///< smoothing of the coupling E = v·Bs
    double kpTauMin         = 20;
    double loadPerHour      = 0.8;    ///< reservoir loading per mV/m per hour
    double baseLoadPerHour  = 0.2;    ///< quiet-time loading (seeded ±40 % per 10 min)
    double leakHours        = 2.0;
    double threshold        = 2.5;    ///< reservoir level that fires an onset
    double thresholdJitter  = 0.3;    ///< ± fraction, seeded per substorm cycle
    double maxRelease       = 3.0;    ///< one onset releases at most this many thresholds
    double releaseFraction  = 0.7;
    double releaseTauMin    = 10;
    Range  expansionMin     { 14, 26 };
    Range  recoveryMin      { 50, 100 };
    double refractoryMin    = 90;     ///< substorms recur every 1.5 h at most
    double growthE          = 0.5;    ///< mV/m: above this the reservoir counts as growing
    double forcedBayNt      = -80;    ///< H change over 5 min that forces an onset (nightside) …
    double forcedDeviationNt = -150;  ///< … while H is this far below its quiet level
    double forcedRefractoryMin = 60;
    double pseudoPerHour    = 0.25;   ///< soft pseudo-breakups after a long quiet spell
    double pseudoAfterMin   = 150;
    double pseudoStrength   = 0.4;
    double gainTauMin       = 10;     ///< assimilation time constant
    double gainDeadZoneNt   = 60;     ///< |ΔH| below this is quiet-time variation, not disturbance
    Range  gainRange        { 0.4, 2.5 };
    double dhScaleNt        = 700;    ///< model ΔH per unit of disturbance
    double activityTauMin   = 10;

    //== Oval: geomagnetic latitude at magnetic midnight ==============================================
    double eqBase           = 66.5;   ///< equatorward edge at Kp 0 …
    double eqPerKp          = 2.0;    ///< … moving this far equatorward per Kp
    double poleBase         = 75.0;   ///< poleward edge
    double polePerKp        = 0.45;
    double dayShiftDeg      = 7;      ///< the oval sits this much further poleward at noon
    double arcPosition      = 0.5;    ///< 0 equatorward edge … 1 poleward edge
    double growthDriftDeg   = 1.5;
    double expansionPoleDeg = 2.5;

    //== Fast: curtain (real time) ====================================================================
    double waveC            = 2.2;    ///< nodes per second
    double damping          = 0.35;   ///< 1/s
    double stiffness        = 0.03;   ///< 1/s²
    double forceKm          = 900;    ///< km/s² forcing at activity 1
    double surgeKm          = 4000;
    double displacementLimitKm = 160;
    double riseTauS         = 0.5;
    double fallTauS         = 3.0;
    Range  pulsePeriodS     { 3, 14 };
    double pcPeriodS        = 40;
    double whistlerPerS     = 0.08;   ///< at activity 1

    //== Observer frame (not in the sketch's DEFAULTS) =================================================
    double poleLatDeg       = 80.8;   ///< centred-dipole north pole, IGRF epoch 2025 (approximate)
    double poleLonDeg       = -72.7;
};

} // namespace worldtable::sky
