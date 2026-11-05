#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

// PROTECTED INTERFACE (CLAUDE.md rule 6): ask the owner before changing it.

namespace worldtable::sky
{

/** Substorm phase (spec/04-sky-model.md §2.1). The values are stable: OSC sends them as ints. */
enum class SkyPhase : uint8_t
{
    quiet    = 0,
    growth   = 1,
    onset    = 2,   ///< onset and expansion
    recovery = 3,
};

/** Why an onset fired (spec/04 §2.1, §3, §8). */
enum class OnsetKind : uint8_t
{
    loaded = 0,   ///< the reservoir crossed its threshold
    forced = 1,   ///< a negative H bay at the station, nightside (data-forced)
    pseudo = 2,   ///< a soft, seeded pseudo-breakup after a long quiet spell
};

/** One point of the curtain chain (spec/04 §2.2). Node 0 is the westmost; nodes are evenly spaced
    in geomagnetic longitude across the station's view. */
struct CurtainNode
{
    float displacementKm;   ///< fold, + poleward, within ± SkyParams::displacementLimitKm
    float velocityKmS;      ///< rate of change of the displacement
    float brightness;       ///< ≥ 0, no upper limit: ≈ 1 is a bright arc, ≈ 7 at a great-storm onset
                            ///< (only green/red/violet stop at 2.5); rises with τ 0.5 s, falls with τ 3 s
    float green;            ///< 557.7 nm emission 0 … 2.5, already × brightness: the arc's body
    float red;              ///< 630.0 nm, 0 … 2.5: high, soft glow (quiet, recovery, great storms)
    float violet;           ///< 427.8 nm, 0 … 2.5: energetic lower border, strongest at onset
    float mlatDeg;          ///< geomagnetic latitude of the footprint: the arc + the displacement
    float mlonDeg;          ///< geomagnetic longitude, fixed per node
    float pulse;            ///< pulsating-patch level 0 … 1 (a sine; patches of 6 nodes, period 3–14 s)
};

/** The most recent onset. Valid when SkySnapshot::onsetCount > 0. */
struct SkyOnset
{
    double    timeUtc;      ///< model time of the onset, s
    float     strength;     ///< released energy in thresholds × onset strength (pseudo ≈ 0.1, great storm ≈ 3)
    float     dhdtNt;       ///< H change over the 5 min before it, nT (the forced trigger reads this)
    int16_t   node;         ///< onset node: the strike and the surge are centred here
    OnsetKind kind;
};

/** The most recent whistler or chorus event (spec/04 §2.4). Valid when whistlerCount > 0. */
struct SkyWhistler
{
    uint64_t step;          ///< tick it fired on
    float    strength;      ///< 0.3 … 1
    bool     falling;       ///< falling tone (80 %) or rising
};

/** Sun and moon above the station (spec/04 §5). Refreshed about once a second. */
struct SkyAstronomy
{
    float sunAzDeg;         ///< azimuth from north through east
    float sunElDeg;
    float moonAzDeg;
    float moonElDeg;        ///< topocentric
    float moonIllumination; ///< 0 new … 1 full
    bool  moonWaxing;
};

/** Where the sky is seen from (ADR-0006). Constant for a session. */
struct SkyObserver
{
    float latDeg, lonDeg, elevKm;          ///< station, geographic
    float mlatDeg, mlonDeg;                ///< station, geomagnetic (centred dipole)
    float poleLatDeg, poleLonDeg;          ///< dipole north pole used for the frame
    float magZenithAzDeg, magZenithElDeg;  ///< where rays converge (CMO: 77° elevation, SSW)
};

/** The sky after one model tick: spec/03-architecture.md, hand-off B.

    Fixed-size POD, written by the audio thread after every SkyModel::step(). The sound reads the
    model's own copy on the audio thread; the UI and the OSC sender read published copies and never
    touch model state. Readers detect events by watching the counters change. */
struct SkySnapshot
{
    static constexpr int kMaxNodes = 128;

    //== Clock =========================================================================================
    double   modelTimeUtc;          ///< s since the Unix epoch
    uint64_t step;                  ///< 100 Hz ticks since the model started; real time = step / 100 s
    float    speed;                 ///< model seconds per real second: 1 Live, 24 Journey, 0 Freeze

    //== Substorm ======================================================================================
    SkyPhase phase;
    float    phaseProgress;         ///< 0 … 1. Growth: reservoir / threshold; onset and recovery:
                                    ///< elapsed / duration; quiet: 0
    float    energy;                ///< reservoir in thresholds
    float    activity;              ///< 0 … 1, from Kp and coupling (τ 10 min)
    float    unloading;             ///< 0 … 2.5: energy being released now (drives the onset)
    float    gain;                  ///< assimilation gain 0.4 … 2.5 (model ΔH pulled toward measured)
    float    couplingMvM;           ///< solar-wind coupling v·max(0, −Bz) / 1000, mV/m

    //== Data at model time ============================================================================
    float    kp;
    float    bzNt;
    float    windSpeedKmS;
    float    densityCm3;
    float    dhMeasuredNt;          ///< H deviation at the station, measured
    float    dhModelNt;             ///< … and as the model predicts it

    //== Oval and slow modulation ======================================================================
    float    mltHours;              ///< magnetic local time at the station
    float    ovalLatitudeDeg;       ///< arc's geomagnetic latitude at magnetic midnight
    float    eqLatitudeDeg;         ///< oval's equatorward edge at midnight
    float    poleLatitudeDeg;       ///< oval's poleward edge at midnight
    float    dayShiftDeg;           ///< the oval sits this much further poleward at noon
    float    stormRed;              ///< 0 … 1: great-storm red glow (Kp 5 → 9)
    float    pcPhase;               ///< field-line ringing (Pc) phase 0 … 1
    float    recoveryPulse;         ///< 0 … 1: depth of pulsating aurora, peaks mid-recovery

    //== Events ========================================================================================
    uint32_t    onsetCount;
    uint32_t    whistlerCount;
    SkyOnset    lastOnset;
    SkyWhistler lastWhistler;

    //== Context =======================================================================================
    SkyAstronomy astronomy;
    SkyObserver  observer;

    //== Curtain =======================================================================================
    int32_t nodeCount;              ///< 64 or 128 (SkyParams::nodes)
    std::array<CurtainNode, kMaxNodes> nodes;   ///< [0, nodeCount) valid
};

static_assert (std::is_trivially_copyable_v<SkySnapshot>);
static_assert (std::is_standard_layout_v<SkySnapshot>);
static_assert (sizeof (SkySnapshot) <= 8192, "copied at 100 Hz into the hand-off buffer: keep it small");

} // namespace worldtable::sky
