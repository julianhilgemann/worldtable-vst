#pragma once

#include "Envelope.h"
#include "SlowLfo.h"
#include "dsp/SlotExchange.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <type_traits>

/** The modulation grid (T4.2, spec/tasks/T4.2-modulation-grid.md). It replaces "16 slots, curve,
    via" of spec/05-sound-engine.md §6.

    PROTECTED INTERFACE (CLAUDE.md rule 6): the owner reviews changes to this header first. Reviewed
    on 2026-11-13: the product law, the levels' modulated range, the `add` law and the Strike
    destination are his decisions (the task file has them).

    ## What it is
    A dense table of depths, sources × destinations, each −1 … +1, 0 = no route. There are no
    curves, no via and no modulator modulating another. Once per sky tick (100 Hz) the engine
    collects the sources' values and the knobs' values (the bases) and calls ModMatrix::tick().
    tick() returns the modulated targets, and the engine glides each one to the next tick
    (CLAUDE.md rule 4). The macros stop being hard-wired: today's wiring is defaultDepths().

    ```
      sources (10)                 depths (10 × 21)          destinations (21 columns)
      Storm Breath Glow Space Drift ─┐                       ┌─► targets (24: 23 parameters and the
      Activity Onset                 ├─► the column's law ───┤   exciter's Strike; Drive moves the
      LFO 1  LFO 2  Envelope        ─┘   over its routes     └─► four band drives) clamped
    ```

    ## The three laws (per column, DestinationInfo::law; `mod_law` in parameters.yaml)
      mul:  target = base · Π over routes (1 + depth · source)     levels, mixes, amounts, Strike,
                                                                    Detune, Drift, Width, Progression
      add:  target = base + amount · ½ (max − min)                  the EQ gains (±12 dB at amount ±1)
      oct:  target = base · 2^(3 · amount)                          and the drives (+0.5); the cut
    with amount = Σ over routes depth · source (add, oct; depth 1 at source 1 = 3 octaves). Each
    factor 1 + depth·source is at least 0, so routes pushing down never flip a target's sign and two
    of them never silence it between them. Every law clamps to the target's modulated range
    (TargetInfo::modMin … modMax). If the amount is exactly 0 (no route, or `add`/`oct` routes that
    cancel; a mul column's factors all exactly 1), the base passes through untouched, bit for bit
    and without the clamp. A non-finite source value counts as 0 (the DSP never sees NaN).

    ## Today's wiring as the default grid (defaultDepths(); main at 3fd1745, Instrument.cpp)
    Source values (see Source): a macro is x = 2·(m − 0.5); Breath is c·k (c = the breath cycle
    0 … 1, k = the Breath knob); Activity is the slewed activity, at most 1.

      route                         depth   today                            match
      Storm → Strike (the exciter)  +0.6    bed, rise, strike × (0.4+1.2·m)  exact: 1 + 0.6·x ≡ 0.4 + 1.2·m
      Storm → Progression           +0.6    × (0.4 + 1.2·m), clamped 0 … 1   exact, clamp included
      Glow  → Violet level          +0.5    × (0.5 + m), not clamped         exact (modulated range 0 … 2)
      Glow  → Shimmer               +0.5    × (0.5 + m), clamped 0 … 1       exact
      Space → Delay mix, Reverb mix +0.5    × (0.5 + m), clamped 0 … 1       exact
      Drift → Detune, Drift         +0.5    × (0.5 + m), not clamped         exact (modulated 0 … 18, 0 … 30 ct)
      Breath → Red, Green levels    −0.35   × (1 − 0.35·k·c)                 exact
      Breath → Violet level         −0.5    × (1 − 0.35·k·c)(1 − 0.15·k·c)   first order only (below)
      Activity → Cut                +0.3    × 2^(eq_sky·3·min(act, 1))       exact (eq_sky = this depth)

    Routes on one mul column multiply, as today's macros did, so the grid plays today's sound at
    every macro setting, not only at the default patch, up to float rounding. The one exception is
    violet's breath: today it is quadratic, (1 − 0.35·y)(1 − 0.15·y) = 1 − 0.5·y + 0.0525·y² with
    y = k·c, and one route is linear. At depth −0.5 the slope matches; the grid is lower by
    0.0525·y²: 0.07 dB at k 0.35, 0.87 dB at k 1, at the top of the breath. The SkySound goldens
    run with Breath 0, so they hold.

    ## Targets without a knob
    Strike (`excite`) is the exciter's level: a target with no parameter (TargetInfo::paramId is
    ""), whose base is fixed at 1 (min = max = 1) and whose modulated range is 0 … 2. The engine
    scales the exciter's bed, rise and strike, and the bow and strike of the played notes and the
    low resonator, by it, exactly where Storm's factor sat before. It appears in the grid and in the
    telemetry like any target; there is no knob to draw a ring on.

    ## The modulated range
    A target's modulated range may reach past its knob: the four levels go up to 2 (+6 dB) while
    their knobs stay 0 … 1, Detune up to 18 ct and Drift up to 30 ct (today's × 1.5 at Drift 1;
    the orchestrator's call, the owner to confirm). Everything else is clamped to its parameter's
    range.

    ## Where things live
    - The depths are plug-in state, not parameters (not automatable). They are saved as the
      `modulation` child: one `route` per non-zero cell, with `source` (SourceInfo::key),
      `destination` (DestinationInfo::key) and `depth`. Unknown keys are skipped and missing cells
      are 0. The enum order is never saved, so these lists may grow without breaking sessions.
    - The message thread builds a Depths from that state and publishes it through a
      DepthsExchange. The audio thread take()s it at block start, hands it to setDepths() and
      release()s the slot at once.
    - The engine computes the source values. The macros are the glided knobs. Breath is
      SkySound's breath cycle × the knob. Activity comes from the activity slew in dsp::Sources.
      Onset is a swell. The LFOs and the Envelope are SlowLfo and Envelope.
    - The LFOs' and the Envelope's settings are parameters (lfo1_rate … env_trigger).

    ## Real-time contract
    Every function here is noexcept, allocates nothing, takes no lock, does no I/O and runs in
    bounded time: fixed-size arrays only, at most kNumSources × kNumDestinations multiply-adds per
    tick. Deterministic: the result depends only on the depths, the source values and the bases,
    never on the host's block size, because tick() runs on the sky's tick grid. */
namespace worldtable::dsp::mod
{

inline constexpr double kTickSeconds = 0.01;   ///< the Sky Model's tick (sky::SkyModel::kTickSeconds)

//==================================================================================================
// Sources: the grid's rows

enum class Source : uint8_t
{
    // Macros
    storm,      ///< 2·(m − 0.5), −1 … 1 (m = the Storm knob, glided like any control)
    breath,     ///< c · k, 0 … k: c = the breath cycle ½ − ½·cos(2π·t·5/60) on the tick grid
                ///< (SkySound::breathCycle()), k = the Breath knob
    glow,       ///< 2·(m − 0.5)
    space,      ///< 2·(m − 0.5)
    drift,      ///< 2·(m − 0.5) of the Drift *macro* (not the drift parameter)
    // Sky
    activity,   ///< the sky's activity, slewed τ 2 s on the tick grid, clamped to 0 … 1
                ///< (min(dsp::Sources::skyActivity(), 1), what the EQ's cut follows today)
    onset,      ///< 0 … 1: each substorm onset swells it to 1 in 80 ms (linear, from where it is:
                ///< never down to 0 first), then it decays as e^(−t / 6 s). Peak 1 whatever the
                ///< onset's strength (the low resonator's swell, spec/05 §12, normalised)
    // Motion
    lfo1,       ///< SlowLfo::value(), −1 … 1
    lfo2,       ///< SlowLfo::value(), −1 … 1
    envelope,   ///< Envelope::value(), 0 … 1
};
inline constexpr int kNumSources = 10;

enum class Group : uint8_t { macros, sky, motion };

/** A source's fixed description. `key` is the stable name used in the saved state. The range is
    [lo, hi], except that Breath's hi is its knob (SourceValues::breathKnob). */
struct SourceInfo
{
    const char* key;
    const char* label;
    Group group;
    float lo, hi;
};

inline constexpr std::array<SourceInfo, kNumSources> kSources { {
    { "macro_storm",  "Storm",    Group::macros, -1.0f, 1.0f },
    { "macro_breath", "Breath",   Group::macros,  0.0f, 1.0f },   // hi = the knob
    { "macro_glow",   "Glow",     Group::macros, -1.0f, 1.0f },
    { "macro_space",  "Space",    Group::macros, -1.0f, 1.0f },
    { "macro_drift",  "Drift",    Group::macros, -1.0f, 1.0f },
    { "sky_activity", "Activity", Group::sky,     0.0f, 1.0f },
    { "sky_onset",    "Onset",    Group::sky,     0.0f, 1.0f },
    { "lfo1",         "LFO 1",    Group::motion, -1.0f, 1.0f },
    { "lfo2",         "LFO 2",    Group::motion, -1.0f, 1.0f },
    { "env",          "Envelope", Group::motion,  0.0f, 1.0f },
} };

//==================================================================================================
// Destinations: the grid's columns. Targets: what they move.

enum class Destination : uint8_t
{
    // Resonators
    lowLevel, redLevel, greenLevel, violetLevel, detune, drift, width,
    excite,                                             ///< Strike: the exciter's level (no knob)
    // EQ
    eqLowGain, eqMidGain, eqHighGain, eqCut, eqDrive,   ///< eqDrive: all four bands' drive together
    // Phaser, Chorus
    phaserMix, chorusMix,
    // Delay
    delayFeedback, delayMix,
    // Reverb
    reverbDecay, reverbShimmer, reverbMix,
    // Harmony
    progression,
};
inline constexpr int kNumDestinations = 21;

enum class Law : uint8_t
{
    mul,   ///< base · Π (1 + depth · source)
    add,   ///< base + Σ depth · source · ½ (max − min)
    oct,   ///< base · 2^(kOctavesPerUnit · Σ depth · source)
};
inline constexpr float kOctavesPerUnit = 3.0f;

/** A column's fixed description. `key` is the stable name used in the saved state: the parameter
    ID where the column moves one parameter, "eq_drive" for the four drives, "excite" for Strike.
    `label` and `module` are the grid's column name and heading (the mockup's DST). Every target of
    a column follows its law. */
struct DestinationInfo
{
    const char* key;
    const char* label;
    const char* module;
    Law law;
};

inline constexpr std::array<DestinationInfo, kNumDestinations> kDestinations { {
    { "low_level",    "Low",         "Resonators", Law::mul },
    { "red_level",    "Red",         "Resonators", Law::mul },
    { "green_level",  "Green",       "Resonators", Law::mul },
    { "violet_level", "Violet",      "Resonators", Law::mul },
    { "detune",       "Detune",      "Resonators", Law::mul },
    { "drift",        "Drift",       "Resonators", Law::mul },
    { "width",        "Width",       "Resonators", Law::mul },
    { "excite",       "Strike",      "Resonators", Law::mul },
    { "eq1_gain",     "Low",         "EQ",         Law::add },
    { "eq2_gain",     "Mid",         "EQ",         Law::add },
    { "eq3_gain",     "High",        "EQ",         Law::add },
    { "eq4_freq",     "Cut",         "EQ",         Law::oct },
    { "eq_drive",     "Drive",       "EQ",         Law::add },
    { "ph_mix",       "Phaser mix",  "Phaser",     Law::mul },
    { "ch_mix",       "Chorus mix",  "Chorus",     Law::mul },
    { "dly_fb",       "Feed",        "Delay",      Law::mul },
    { "dly_mix",      "Mix",         "Delay",      Law::mul },
    { "rev_decay",    "Decay",       "Reverb",     Law::mul },
    { "rev_shimmer",  "Shimmer",     "Reverb",     Law::mul },
    { "rev_mix",      "Mix",         "Reverb",     Law::mul },
    { "progression",  "Progression", "Harmony",    Law::mul },
} };

/** What the grid writes: one per knob, and Strike. Each column moves one, except Drive (four). */
enum class Target : uint8_t
{
    lowLevel, redLevel, greenLevel, violetLevel, detune, drift, width, excite,
    eq1Gain, eq2Gain, eq3Gain, eq4Freq, eq1Drive, eq2Drive, eq3Drive, eq4Drive,
    phMix, chMix, dlyFb, dlyMix, revDecay, revShimmer, revMix, progression,
};
inline constexpr int kNumTargets = 24;

/** A target's parameter, column and ranges.
    - `paramId`: the parameter it moves; "" for a target without a knob (Strike). Such a target's
      base is fixed: min = max = that base (1 for Strike), and the engine passes it as the base.
    - `min`, `max`: the knob's range, equal to the parameter's range in parameters.yaml (a test
      checks it against the APVTS). The `add` law moves by half of it per unit of amount.
    - `modMin`, `modMax`: the modulated range, the clamp of every law. It contains [min, max]; it
      reaches past it for the four levels (0 … 2), Detune (0 … 18 ct), Drift (0 … 30 ct) and Strike
      (0 … 2), and equals it everywhere else. */
struct TargetInfo
{
    const char* paramId;
    Destination column;
    float min, max;
    float modMin, modMax;
};

inline constexpr std::array<TargetInfo, kNumTargets> kTargets { {
    { "low_level",    Destination::lowLevel,      0.0f,   1.0f,     0.0f,   2.0f },
    { "red_level",    Destination::redLevel,      0.0f,   1.0f,     0.0f,   2.0f },
    { "green_level",  Destination::greenLevel,    0.0f,   1.0f,     0.0f,   2.0f },
    { "violet_level", Destination::violetLevel,   0.0f,   1.0f,     0.0f,   2.0f },
    { "detune",       Destination::detune,        0.0f,   12.0f,    0.0f,   18.0f },
    { "drift",        Destination::drift,         0.0f,   20.0f,    0.0f,   30.0f },
    { "width",        Destination::width,         0.0f,   1.0f,     0.0f,   1.0f },
    { "",             Destination::excite,        1.0f,   1.0f,     0.0f,   2.0f },
    { "eq1_gain",     Destination::eqLowGain,     -12.0f, 12.0f,    -12.0f, 12.0f },
    { "eq2_gain",     Destination::eqMidGain,     -12.0f, 12.0f,    -12.0f, 12.0f },
    { "eq3_gain",     Destination::eqHighGain,    -12.0f, 12.0f,    -12.0f, 12.0f },
    { "eq4_freq",     Destination::eqCut,         200.0f, 20000.0f, 200.0f, 20000.0f },
    { "eq1_drive",    Destination::eqDrive,       0.0f,   1.0f,     0.0f,   1.0f },
    { "eq2_drive",    Destination::eqDrive,       0.0f,   1.0f,     0.0f,   1.0f },
    { "eq3_drive",    Destination::eqDrive,       0.0f,   1.0f,     0.0f,   1.0f },
    { "eq4_drive",    Destination::eqDrive,       0.0f,   1.0f,     0.0f,   1.0f },
    { "ph_mix",       Destination::phaserMix,     0.0f,   1.0f,     0.0f,   1.0f },
    { "ch_mix",       Destination::chorusMix,     0.0f,   1.0f,     0.0f,   1.0f },
    { "dly_fb",       Destination::delayFeedback, 0.0f,   0.95f,    0.0f,   0.95f },
    { "dly_mix",      Destination::delayMix,      0.0f,   1.0f,     0.0f,   1.0f },
    { "rev_decay",    Destination::reverbDecay,   0.3f,   60.0f,    0.3f,   60.0f },
    { "rev_shimmer",  Destination::reverbShimmer, 0.0f,   1.0f,     0.0f,   1.0f },
    { "rev_mix",      Destination::reverbMix,     0.0f,   1.0f,     0.0f,   1.0f },
    { "progression",  Destination::progression,   0.0f,   1.0f,     0.0f,   1.0f },
} };

constexpr size_t indexOf (Source s) noexcept      { return static_cast<size_t> (s); }
constexpr size_t indexOf (Destination d) noexcept { return static_cast<size_t> (d); }
constexpr size_t indexOf (Target t) noexcept      { return static_cast<size_t> (t); }

constexpr const TargetInfo& infoOf (Target t) noexcept { return kTargets[indexOf (t)]; }
constexpr Law lawOf (Destination d) noexcept           { return kDestinations[indexOf (d)].law; }
constexpr Law lawOf (Target t) noexcept                { return lawOf (infoOf (t).column); }

/** False for a target without a knob (Strike): its base is fixed at infoOf (t).min. */
constexpr bool hasParameter (Target t) noexcept        { return infoOf (t).paramId[0] != '\0'; }

//==================================================================================================
/** The grid: one depth per source and column, −1 … +1, 0 = no route. Trivially copyable (≈ 800
    bytes), so it is built off the audio thread and swapped in whole (DepthsExchange). */
struct Depths
{
    std::array<std::array<float, kNumDestinations>, kNumSources> cells {};

    constexpr float get (Source s, Destination d) const noexcept { return cells[indexOf (s)][indexOf (d)]; }

    /** Clamps to −1 … +1; a non-finite depth becomes 0. */
    constexpr void set (Source s, Destination d, float depth) noexcept
    {
        cells[indexOf (s)][indexOf (d)] = depth >= -1.0f ? (depth <= 1.0f ? depth : 1.0f)
                                                         : (depth < -1.0f ? -1.0f : 0.0f);
    }
};
static_assert (std::is_trivially_copyable_v<Depths>);

/** Lock-free hand-off of the grid, message thread → audio thread (ADR-0003, as the wavetables and
    the tracks travel). Three slots: the one the audio thread is copying, a pending one and one
    being written. */
using DepthsExchange = SlotExchange<Depths, 3>;

/** Today's hard-wired macros as routes (see the table in the file comment). The LFOs, the Envelope
    and Onset start with no routes. */
constexpr Depths defaultDepths() noexcept
{
    Depths d;
    d.set (Source::storm, Destination::excite, 0.6f);
    d.set (Source::storm, Destination::progression, 0.6f);
    d.set (Source::breath, Destination::redLevel, -0.35f);
    d.set (Source::breath, Destination::greenLevel, -0.35f);
    d.set (Source::breath, Destination::violetLevel, -0.5f);
    d.set (Source::glow, Destination::violetLevel, 0.5f);
    d.set (Source::glow, Destination::reverbShimmer, 0.5f);
    d.set (Source::space, Destination::delayMix, 0.5f);
    d.set (Source::space, Destination::reverbMix, 0.5f);
    d.set (Source::drift, Destination::detune, 0.5f);
    d.set (Source::drift, Destination::drift, 0.5f);
    d.set (Source::activity, Destination::eqCut, 0.3f);   // the old eq_sky's default (the parameter went)
    return d;
}

//==================================================================================================
/** One tick's source values (see Source for what each is). */
struct SourceValues
{
    std::array<float, kNumSources> value {};
    float breathKnob = 0.35f;   ///< Breath's range is 0 … this (the rings)

    constexpr float& operator[] (Source s) noexcept             { return value[indexOf (s)]; }
    constexpr float operator[] (Source s) const noexcept        { return value[indexOf (s)]; }
};

/** One tick's bases: every target's knob value (as glided for the DSP), in parameter units; a
    target without a knob takes its fixed base (infoOf (t).min). */
struct Bases
{
    std::array<float, kNumTargets> value {};

    constexpr float& operator[] (Target t) noexcept             { return value[indexOf (t)]; }
    constexpr float operator[] (Target t) const noexcept        { return value[indexOf (t)]; }
};

/** One tick's result: what the engine glides to and what the telemetry reads. */
struct Frame
{
    std::array<float, kNumSources> source {};          ///< the source values used (non-finite → 0)
    /** Per column: the amount now, and at the sources' extremes (each source over its own range,
        independently: the "range" of the rings, spec/tasks/T4.2 "The rings"). For an `add` or `oct`
        column the amount is Σ depth·source; for a `mul` column it is Π (1 + depth·source) − 1, so
        that the target is base · (1 + amount) there too, and its range is the product of each
        route's extremes. */
    std::array<float, kNumDestinations> amount {}, amountLo {}, amountHi {};
    /** Per target: the law applied to its base (value), and the reachable range [lo, hi]: the law
        at amountLo and amountHi, ordered (a negative base turns them around), clamped to the
        modulated range. A target
        whose column has no route has value = lo = hi = its base. */
    std::array<float, kNumTargets> value {}, lo {}, hi {};

    constexpr float operator[] (Target t) const noexcept { return value[indexOf (t)]; }
};

//==================================================================================================
/** The grid in the engine: the depths as heard (gliding toward the newest table) and the evaluation.
    Audio thread, except where noted. */
class ModMatrix
{
public:
    /** A depth moves at most this far per second toward a new table, linearly: a new route is fully
        there within 1 s, −1 → +1 takes 2 s, and the step is never heard as a click (rule 4). */
    static constexpr float kDepthPerSecond = 1.0f;
    static constexpr float kDepthPerTick = kDepthPerSecond * static_cast<float> (kTickSeconds);

    /** Before playback (prepare, a session load): takes `depths` at once, no glide. */
    void reset (const Depths& depths) noexcept;

    /** A newer table (from the DepthsExchange): the depths glide to it from where they are. */
    void setDepths (const Depths& depths) noexcept;

    /** Once per sky tick: moves the depths one tick toward the newest table, then evaluates them
        (evaluate()) into `out`. */
    void tick (const SourceValues&, const Bases&, Frame& out) noexcept;

    const Depths& depths() const noexcept { return heard; }    ///< as heard now (gliding)
    const Depths& target() const noexcept { return wanted; }   ///< the newest table
    bool gliding() const noexcept;

    /** The whole evaluation, without the glide: amounts per column, the law per target, the ranges. */
    static void evaluate (const Depths&, const SourceValues&, const Bases&, Frame& out) noexcept;

    /** `target`'s law and clamp (kTargets, kDestinations) applied to `base`, with the column's
        amount (see Frame::amount: for `mul`, the product minus 1). An amount of exactly 0 returns
        `base` untouched; a non-finite amount counts as 0. */
    static float apply (Target target, float base, float amount) noexcept;

private:
    Depths heard {}, wanted {};
};

//==================================================================================================
/** What the editor reads about modulation (telemetry, 30 Hz): one tick's Frame, the LFOs' cycle
    positions and the Envelope's stage and progress. The audio thread publishes the newest tick's
    after each block that ticked (sky::TripleBuffer), the editor takes the newest. */
struct ModTelemetry
{
    Frame frame;
    std::array<SlowLfo::View, 2> lfo {};
    Envelope::View envelope {};
    uint64_t tick = 0;   ///< the sky tick it belongs to
};
static_assert (std::is_trivially_copyable_v<ModTelemetry>);

} // namespace worldtable::dsp::mod
