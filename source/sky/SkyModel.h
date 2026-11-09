#pragma once

#include "Observer.h"
#include "SkyParams.h"
#include "SkySnapshot.h"
#include "data/WorldSnapshot.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace worldtable::sky
{

//==================================================================================================
/** Something that happened in one tick (spec/04 §2.4). Phase changes and onsets come from the slow
    sky; whistlers from the fast one. */
struct SkyEvent
{
    enum class Type : uint8_t { phase, onset, whistler };

    double    modelTimeUtc;
    float     strength;    ///< onset: as SkyOnset::strength; whistler: 0.3 … 1; phase: 0
    float     dhdtNt;      ///< onset: H change over the 5 min before it; else 0
    int16_t   node;        ///< onset: the onset node; else −1
    Type      type;
    SkyPhase  phase;       ///< phase: the phase entered; else the phase at the time
    OnsetKind onsetKind;   ///< onset only
    bool      falling;     ///< whistler only
};

//==================================================================================================
/** The slow sky (model time, 1 s grid): the sketch's `initialSlow` state, field for field.

    Public so a checkpoint is a plain copy and a frozen sky can be saved with the session. Readers
    use SkySnapshot, not this. Sketch names in brackets. */
struct SlowSkyState
{
    double timeUtc;              ///< start of the next slow step [t]

    // data sampled at the last step
    double windSpeedKmS;         ///< [v]
    double bzNt;                 ///< [bz]
    double densityCm3;           ///< [n]
    double kp;                   ///< [kp]
    double dhNt;                 ///< H deviation [dh]
    double dhdtNt;               ///< H change over 5 min [dhdt]
    double couplingMvM;          ///< v·max(0, −Bz)/1000 [e]

    // smoothed drivers
    double couplingSmoothMvM;    ///< [eSm]
    double kpSmooth;             ///< [kpSm]
    double activity;             ///< 0 … 1 [act]
    double gain;                 ///< assimilation gain [gain]

    // reservoir and substorm cycle
    double reservoir;            ///< energy W, same unit as SkyParams::threshold [w]
    double cycleThreshold;       ///< this cycle's jittered threshold [thr]
    double onsetReservoir;       ///< W₀, energy at the last onset [w0]
    double unloading;            ///< 0 … 2.5 [u]
    double onsetStrength;        ///< 0.35 … 1.2, softer for weakly driven onsets [ostr]
    double phaseStartUtc;        ///< [phaseStart]
    double phaseDurationS;       ///< onset and recovery only; 0 otherwise [phaseDur]
    double lastOnsetUtc;         ///< −∞ before the first onset [lastOnset]
    double lastOnsetEventStrength;   ///< SkyOnset::strength of the last onset (event only in the sketch)
    double lastOnsetDhdtNt;          ///< (event only in the sketch)

    // oval and assimilation
    double eqLatDeg;             ///< [eqLat]
    double poleLatDeg;           ///< [poleLat]
    double dhModelNt;            ///< [dhModel]
    double dhMeasuredSmoothNt;   ///< |ΔH| measured, τ 5 min [dhMeasSm]
    double mltHours;             ///< station MLT [mlt0]
    double nightFactor;          ///< 0.25 noon … 1 midnight [night]
    double sunElDeg;             ///< [sunEl]

    uint32_t  cycle;             ///< substorm cycles so far: seeds the jitter [cycle]
    uint32_t  onsetCount;        ///< [onsetCount]
    int32_t   onsetNode;         ///< [onsetNode]
    SkyPhase  phase;             ///< [phase]
    OnsetKind lastOnsetKind;     ///< (event only in the sketch)
    bool      forced;            ///< last onset was data-forced [forced]
};

static_assert (std::is_trivially_copyable_v<SlowSkyState>);
static_assert (std::is_standard_layout_v<SlowSkyState>);

//==================================================================================================
/** The slow sky over a whole track, for one seed and one set of params. Built off the audio thread
    and published with (or after) its track.

    It gives seek() a start state at every model minute, so a seek replays at most 59 slow steps
    instead of the whole track. It also gives Live its state at "now", the goldens their per-minute
    trace, and the UI its day chart (phases and onsets). The slow sky is the same at every speed
    (spec/04 §1), so one timeline serves 1×, 24× and 1440×. */
struct SkyTimeline
{
    const data::WorldSnapshot* track = nullptr;   ///< not owned; must outlive the timeline
    uint32_t  seed = 1;
    SkyParams params;

    /** State at track->startUtc + 60 s × k, before that step runs. One more entry than the track
        has minutes, so the last one is the state at the end. */
    std::vector<SlowSkyState> minutes;

    /** Phase changes and onsets over the whole track, oldest first. */
    std::vector<SkyEvent> events;

    /** Runs the slow sky from the track start (60 slow steps per minute of track; ≈ 0.2 s for 48 h,
        estimated). Allocates: data or message thread only. */
    void build (const data::WorldSnapshot& track, uint32_t seed, const SkyParams& params);

    /** The checkpoint at or before `utc`, clamped to the track; nullptr if empty. Real-time safe. */
    const SlowSkyState* checkpointAtOrBefore (double utc) const noexcept;
};

//==================================================================================================
/** The Sky Model (spec/04-sky-model.md): one generative aurora, nudged by a data track.

    Clocked by audio samples (ADR-0007): processBlock calls step() every sampleRate / 100 samples
    (SkyClock). Everything here except construction runs on the audio thread and is real-time safe:
    no allocation, no locks, no I/O, bounded work (at most kMaxSlowStepsPerTick slow steps and
    kMaxNodes nodes per step; at most 59 slow steps per seek).

    Two clocks (spec/04 §1): real time (ticks) moves the curtain; model time moves the data, the
    reservoir and the phases. One tick advances model time by 10 ms × speed. The slow sky steps on a
    fixed 1 s grid of model time, so it is identical at every speed.

    Deterministic (CLAUDE.md rule 5): same timeline (track, seed, params), same seek and speed calls
    at the same ticks → identical snapshots and events. The sample rate only decides how many
    samples a tick lasts; the block size doesn't matter. */
class SkyModel
{
public:
    static constexpr int    kTicksPerSecond      = 100;
    static constexpr double kTickSeconds         = 1.0 / kTicksPerSecond;
    static constexpr double kSlowStepSeconds     = 1.0;
    static constexpr double kMaxSpeed            = 1440.0;   ///< 24 h of sky per real minute
    static constexpr int    kMaxSlowStepsPerTick = static_cast<int> (kMaxSpeed * kTickSeconds / kSlowStepSeconds) + 1;
    /** Worst case per tick: every slow step enters a phase and fires an onset, plus one whistler. */
    static constexpr int    kMaxEventsPerStep    = 2 * kMaxSlowStepsPerTick + 1;
    static constexpr int    kMaxSeekSlowSteps    = 59;

    /** Message thread. Fixed-size state, so nothing allocates after this. Starts quiet (climatology),
        with no timeline, at speed 0. */
    SkyModel() noexcept;

    //== Audio thread =================================================================================

    /** Adopts a published timeline: its track, seed and params. nullptr = none: model time holds and
        the curtain keeps moving on the last slow state. The timeline must stay alive until a later
        setTimeline() has returned (the hand-off pool owns it, ADR-0003).
        If the seed or params differ from the previous timeline's, the slow state is re-seeked to the
        current model time. The curtain is kept, so nothing jumps. */
    void setTimeline (const SkyTimeline* timeline) noexcept;

    /** Moves model time, clamped to the track: copies the checkpoint at or before it and replays at
        most kMaxSeekSlowSteps slow steps. The curtain keeps moving. Emits no events. Without a
        timeline it does nothing. */
    void seek (double modelTimeUtc) noexcept;

    /** Model seconds per real second, clamped to [0, kMaxSpeed]. 1 = Live, 24 = Journey 24 h → 1 h,
        0 = Freeze (data and phases hold; the curtain keeps breathing). At the track's end model time
        holds as if frozen. */
    void setSpeed (double modelSecondsPerSecond) noexcept;

    /** One tick: 10 ms of real time, 10 ms × speed of model time. Returns the tick's events
        (at most kMaxEventsPerStep, oldest first), valid until the next step(). */
    std::span<const SkyEvent> step() noexcept;

    /** The sky after the last step() or seek(). */
    const SkySnapshot& current() const noexcept { return snapshot; }

    double modelTimeUtc() const noexcept { return static_cast<double> (modelMicros) / 1e6; }
    double speed() const noexcept        { return speedFactor; }

    /** The slow sky as of the last step() or seek(): for tests, and for saving a frozen sky with the
        session (M6). Readers use current(). */
    const SlowSkyState& slowState() const noexcept { return slow; }

    /** Slow steps the last seek() replayed (≤ kMaxSeekSlowSteps). Diagnostic. */
    int lastSeekSlowSteps() const noexcept { return seekSlowSteps; }

    /** How many seeks have been applied (T6.6): a listener that sees it change knows the sky jumped
        rather than moved (SkySound re-bases its exciter and tells the harmony). */
    uint32_t seeks() const noexcept { return seekCount; }

    /** The station the sky is seen from (the dome projection): the sound pans the curtain with it. */
    const Observer& observer() const noexcept { return obs; }

private:
    // T2.3: private members may change without review (T2.1). The public surface above is reviewed.
    void layOutCurtain (bool keepMotion) noexcept;
    void stepCurtain() noexcept;
    void writeSnapshot (bool refreshSun) noexcept;
    bool pushEvent (const SkyEvent& event) noexcept;

    const SkyTimeline* timeline = nullptr;
    SkyParams params {};
    uint32_t  seed = 1;
    Observer  obs;
    SlowSkyState slow {};

    int64_t  modelMicros   = 0;   ///< model time in whole µs since the epoch: exact, like the sketch's ms
    int64_t  microsPerTick = 0;   ///< round(10 000 µs × speed)
    double   speedFactor   = 0.0;
    uint64_t ticks         = 0;
    int      seekSlowSteps = 0;
    uint32_t seekCount     = 0;

    struct Curtain   // fast state, double like the sketch's Float64Arrays
    {
        static constexpr int kMaxPatches = (SkySnapshot::kMaxNodes + 5) / 6;
        std::array<double, SkySnapshot::kMaxNodes> y {}, vy {}, brightness {}, mlonDeg {}, pulse {};
        std::array<double, kMaxPatches> pulsePeriodS {}, pulsePhase {};
        uint32_t    whistlers = 0;
        SkyWhistler lastWhistler {};
        double      recoveryPulse = 0.0;
        int         nodes = 0;   ///< laid out for this many nodes …
        double      spanDeg = 0.0, stationMlonDeg = 0.0;   ///< … this span and station
        uint32_t    seed = 0;    ///< … and these pulse patches
    } curtain;

    std::array<SkyEvent, kMaxEventsPerStep> events {};
    int eventCount = 0;

    SkySnapshot snapshot {};
};

//==================================================================================================
/** Counts audio samples into 100 Hz ticks (ADR-0007). Tick k falls on sample
    round(k × sampleRate / 100) counted from prepare() — the T1.3 sketch engine's grid — so tick
    positions don't depend on the block size. Real-time safe.

        for (int pos = 0; pos < numSamples;)
        {
            if (clock.tickDue()) { handle (model.step()); clock.ticked(); }
            const int n = std::min (numSamples - pos, clock.samplesToNextTick());
            render (pos, n);
            clock.advance (n);
            pos += n;
        }
*/
class SkyClock
{
public:
    void prepare (double sampleRate) noexcept
    {
        samplesPerTick = sampleRate / SkyModel::kTicksPerSecond;
        sample = 0;
        nextTick = 0;
        tickCount = 0;
    }

    bool tickDue() const noexcept { return sample == nextTick; }

    void ticked() noexcept
    {
        ++tickCount;
        nextTick = std::llround (static_cast<double> (tickCount) * samplesPerTick);
    }

    int samplesToNextTick() const noexcept { return static_cast<int> (nextTick - sample); }

    void advance (int numSamples) noexcept { sample += numSamples; }

    int64_t  samplePosition() const noexcept { return sample; }
    uint64_t ticks() const noexcept          { return tickCount; }

private:
    double   samplesPerTick = 480.0;
    int64_t  sample    = 0;
    int64_t  nextTick  = 0;
    uint64_t tickCount = 0;
};

} // namespace worldtable::sky
