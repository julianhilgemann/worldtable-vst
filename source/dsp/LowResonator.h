#pragma once

#include <array>
#include <cstdint>

namespace worldtable::dsp
{

/** The low resonator (T3.7, spec/05 §1): a resonator layer under red, tuned to the bass note and
    placed in the sub octave (C1–B1) or the bass octave (C2–B2). Mono, in the centre.

    Two banks, each a pair of resonators ± kDetuneCents around the note (slow beating) and one an
    octave up (the body, so the note still speaks on small speakers). A new note goes to the other
    bank and the excitation crossfades over to it (equal power); the old bank rings out, damped to a
    T60 of at most twice the crossfade. Complex one-pole resonators in double, like ResonatorBank:
    the input is normalised by √(1−r²), so the level doesn't depend on the decay.

    The caller (dsp::Sources) drives it once per sky tick: the note's pitch class, the continuous
    excitation (the sky's bed in drone mode, the bow while a key is down in Play mode), the output
    gain (ramped over the tick) and, in Play mode, a strike at note-on. Real-time safe and
    allocation-free; deterministic for a given seed. */
class LowResonator
{
public:
    static constexpr int kBanks = 2;
    static constexpr int kPerBank = 3;                  ///< note − detune, note + detune, octave up
    static constexpr double kDetuneCents = 1.5;
    static constexpr double kBodyGain = 0.35;           ///< the octave up, relative to the pair
    static constexpr double kOutputScale = 0.2;         ///< at level 0.5 the sub band sits level with 63–125 Hz (T3.7 renders)

    struct Settings
    {
        double t60 = 8.0;     ///< s
        int octave = 1;       ///< 1: C1–B1 (sub), 2: C2–B2 (bass)
    };

    /** MIDI note of pitch class `pc` (0–11) in `octave` (1: C1 = 24). */
    static int noteFor (int pitchClass, int octave) noexcept;

    void prepare (double sampleRate, uint32_t seed) noexcept;

    void setSettings (const Settings&) noexcept;

    /** The bass note's pitch class (< 0: keep the current one; an octave change still applies). A
        new note goes to the other bank; the excitation crossfades to it over `crossfadeSeconds`.
        Immediate, so a strike right after lands on the new note. */
    void setNote (int pitchClass, double crossfadeSeconds) noexcept;

    /** One control tick: `excitation` is the continuous input level, `gain` the output gain reached
        at the end of the tick (`tickSamples` long). */
    void tick (double excitation, double gain, int tickSamples) noexcept;

    /** An impulse into the current bank (Play mode note-on): rings at once, click-free (the
        output is the phasor's imaginary part, which starts at zero). */
    void strike (double amount) noexcept;

    /** Adds the resonator's output to both channels (mono). */
    void process (float* left, float* right, int numSamples) noexcept;

    int note() const noexcept { return currentNote; }      ///< MIDI note sounding, −1 before the first
    double outputGain() const noexcept { return outGain; }
    double crossfade() const noexcept { return xf; }

    /** NaN/inf guard (spec/05 §8): resets a resonator rather than let it spread. */
    int guard() noexcept;

private:
    void tune (int bank, int note) noexcept;
    double radius (double t60Seconds) const noexcept;

    Settings settings;
    double sampleRate = 48000.0;
    int cur = 0;
    int currentNote = -1, currentPc = -1, currentOctave = 1;
    double xf = 1.0;                 ///< 0 … 1 toward the current bank
    double xfSeconds = 4.0;          ///< of the running crossfade

    std::array<double, kBanks * kPerBank> re {}, im {}, c {}, s {};
    std::array<double, kBanks * kPerBank> hz {};
    std::array<double, kBanks * kPerBank> inA {};       ///< input amplitude (incl. √(1−r²))
    std::array<double, kBanks> bankT60 {};
    std::array<uint32_t, kPerBank> noise {};            ///< one stream per resonator, shared by the banks
    double outGain = 0.0, outStep = 0.0;
    int rampLeft = 0;
};

} // namespace worldtable::dsp
