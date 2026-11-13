#pragma once

#include <array>
#include <cstdint>

namespace worldtable::dsp::mod
{

/** The free envelope, a source of the modulation grid (T4.2).

    Attack, decay, sustain, release. Each time is 10 ms … 10 s, the output is 0 … 1. The shapes are
    the mockup's (spec/mockups/ui-v2.html `envShape`), with f the progress through the stage
    (0 … 1):
      attack   linear, from where the envelope is up to 1, at the slope of a full attack
               (from 0 it takes `attack`; from v it takes (1 − v)·attack)
      decay    1 − (1 − S)·(1 − (1 − f)²)          1 → S, easing in
      sustain  S, held (see below)
      release  v₀·(1 − f)²                         from where it was, v₀, down to 0
    Times are counted in sky ticks: a stage of T seconds advances f by kTickSeconds / T per tick,
    so a time change mid-stage keeps the value continuous. When a stage ends on a tick, the value
    is the stage's end value, stage() still names that stage, and the next stage begins with the
    next tick.

    **Triggers** (env_trigger): Onset (a substorm onset), Note (a MIDI note-on) or Fire only. fire()
    is the panel's button. It fires in every mode; "Fire only" means nothing else fires it.
    - A note holds the envelope at sustain while any key is down; the last note-off releases it.
      Every note-on fires again, even while other keys are down.
    - An onset or Fire holds it at sustain for kHoldSeconds (counted once the decay is done), then
      releases it.
    - A key let go before the sustain releases at once, from wherever the envelope is.
    - A trigger while the envelope moves (retrigger) starts the attack from the current value. It
      never drops to 0 first, so it never clicks. The hold starts again.
    - A note shorter than a tick still fires: one tick of attack, then the release.
    - Leaving Note mode while a key holds the envelope releases it.

    Events (onset, noteOn, noteOff, fire) are latched and take effect on the next tick(). The
    output changes only in tick(). The engine calls the events at their sample, splitting the block
    there as Play mode does (Instrument::renderPlay). So the envelope fires on the same tick for the
    same onset or note at any block size (rule 5).

    Real-time safe: no allocation, no locks, bounded time. */
class Envelope
{
public:
    enum class Trigger : uint8_t { onset, note, fire };                 ///< env_trigger: Onset, Note, Fire only
    enum class Stage : uint8_t { idle, attack, decay, sustain, release };

    static constexpr double kTickSeconds = 0.01;                        ///< the sky's tick
    static constexpr double kMinSeconds = 0.01, kMaxSeconds = 10.0;     ///< every time's range
    static constexpr double kHoldSeconds = 1.2;                         ///< an onset or Fire holds sustain this long

    /** The env_* parameters. The defaults are the mockup's (attack 0.1 s, decay 0.6 s, sustain
        0.35, release 1.5 s, on onset). */
    struct Settings
    {
        double attack = 0.1, decay = 0.6, sustain = 0.35, release = 1.5;   ///< s, s, 0 … 1, s
        Trigger trigger = Trigger::onset;
    };

    /** What the Envelope panel draws: the dot running along the shape. */
    struct View
    {
        Stage stage = Stage::idle;
        float progress = 0.0f;   ///< see progress()
        float value = 0.0f;
        bool held = false;       ///< a key holds it at sustain (Note mode)
    };

    /** Idle at 0, no key down, nothing latched. */
    void reset() noexcept;

    /** Times are clamped to kMinSeconds … kMaxSeconds, the sustain to 0 … 1. */
    void setSettings (const Settings&) noexcept;

    //== Events: latched, taken by the next tick() =================================================
    void onset() noexcept;              ///< a substorm onset: fires in Onset mode
    void noteOn (int note) noexcept;    ///< 0 … 127: fires in Note mode; keys are counted in every mode
    void noteOff (int note) noexcept;
    void allNotesOff() noexcept;
    void fire() noexcept;               ///< the Fire button: fires in every mode

    /** One sky tick: takes the latched events, advances the stage, returns value(). */
    float tick() noexcept;

    float value() const noexcept { return out; }   ///< 0 … 1, as of the last tick
    Stage stage() const noexcept { return now; }

    /** Progress through the stage, 0 … 1. Attack: the value (the attack is the line 0 → 1). Decay
        and release: f. Sustain: the fraction of the kHoldSeconds hold done after an onset or Fire,
        0 while a key holds it. Idle: 0. */
    float progress() const noexcept;

    View view() const noexcept;
    const Settings& getSettings() const noexcept { return settings; }

private:
    Settings settings;
    Stage now = Stage::idle;
    float out = 0.0f;
    double f = 0.0;                     ///< progress through decay or release
    double releaseFrom = 0.0;           ///< v₀
    int holdTicks = 0;                  ///< ticks of the timed hold done
    bool stageDone = false;             ///< the stage ended on the last tick: the next one starts
    bool heldByNote = false;            ///< the gate is a key (Note mode), not the timed hold
    bool firePending = false;           ///< a trigger arrived since the last tick
    bool releasePending = false;        ///< the last key went up since the last tick
    std::array<uint64_t, 2> keys {};    ///< the keys down, one bit per note
};

} // namespace worldtable::dsp::mod
