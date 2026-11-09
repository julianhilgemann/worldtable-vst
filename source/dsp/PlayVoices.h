#pragma once

#include "Voicing.h"

#include <array>
#include <cstdint>

namespace worldtable::dsp
{

/** What the sky gives the played notes on each tick (filled by dsp::Sources): per curtain node the
    pan (equal-power gains), presence, detune in cents, and how strongly its emission favours each
    layer. Nodes are SkySound's voices (one per node). */
struct SkyNodes
{
    static constexpr int kMaxNodes = 128;

    int count = 0;
    std::array<double, kMaxNodes> panL {}, panR {}, presence {}, cents {};
    std::array<std::array<double, kNumLayers>, kMaxNodes> colour {};   ///< layer weights, 0.5 … 1.5
};

/** Play mode (T3.7, ADR-0008, spec/05 §13): MIDI notes play the resonators, as a polyphonic
    instrument, in the curtain's colours.

    A note sounds from kStrands points of the curtain. Each strand has kPartials resonators, the
    three layers in small: red an octave down, green the note and its octave, violet the twelfth.
    They are the bank's complex one-pole resonators (T60 = the layer decays), excited two ways: a
    strike at note-on (velocity; it fades out as the attack lengthens, so a slow attack bows in
    instead) and a continuous bow while the key is down (the sky's bed, × (0.5 + activity)). An
    envelope (linear attack, exponential release to −60 dB) shapes what comes out. Each strand
    takes its node's pan, presence, detune and drift, and the node's emission balance weights the
    partials; an onset strikes held notes softly.

    kSlots voices, kMaxHeld of them held at most (the oldest held note is stolen). A stolen voice
    fades out in kStealSeconds and then starts the new note, so nothing clicks. The same note
    again retriggers its voice. Sustain pedal, all notes off and all sound off are handled.

    Real-time safe, allocation-free; deterministic: the same events at the same samples give the
    same output. prepare() runs on the message thread; everything else on the audio thread. */
class PlayVoices
{
public:
    static constexpr int kSlots = 24;
    static constexpr int kMaxHeld = 16;
    static constexpr int kStrands = 3;
    static constexpr int kPartials = 4;
    static constexpr int kResonators = kStrands * kPartials;
    static constexpr std::array<int, kPartials> kInterval { -12, 0, 12, 19 };
    static constexpr std::array<int, kPartials> kLayerOf { 0, 1, 1, 2 };            ///< red, green, green, violet
    static constexpr std::array<double, kPartials> kPartialGain { 0.7, 1.0, 0.45, 0.35 };
    static constexpr double kStealSeconds = 0.01;
    static constexpr int kNoNote = -1000;          ///< lowestHeld() with nothing held
    static constexpr double kOutputScale = 0.33;   ///< a 4-note chord at velocity 100 sustains near the drone's −18 dBFS (T3.7 renders)
    static constexpr double kStrikeScale = 0.7;    ///< the strike against the bow: ~6 dB above the sustain

    struct Settings
    {
        double attack = 0.01;      ///< s, linear
        double release = 2.5;      ///< s to −60 dB
        double velocity = 0.6;     ///< 0: every note full level … 1: velocity sets the level (30 dB)
        std::array<double, kNumLayers> level { 1.0, 0.7, 0.4 };
        std::array<double, kNumLayers> t60 { 20.0, 6.0, 2.5 };
        double bow = 0.3;          ///< continuous excitation while held (× (0.5 + activity))
        double strike = 1.0;       ///< note-on strike
        int transpose = 0;         ///< semitones, applied to every played note
    };

    /** One voice, for the UI (the keyboard strip and the voicing view). */
    struct View
    {
        int note = -1;         ///< sounding MIDI note (transpose applied)
        float level = 0.0f;    ///< envelope × velocity gain
        bool held = false;     ///< key down or sustained
    };

    void prepare (double sampleRate, uint32_t seed) noexcept;
    void setSettings (const Settings&) noexcept;

    //== Events (audio thread, between samples) ======================================================
    void noteOn (int note, float velocity) noexcept;   ///< velocity 0 … 1; 0 is a note-off
    void noteOff (int note) noexcept;
    void sustain (bool down) noexcept;
    void allNotesOff() noexcept;                       ///< releases everything, lifts the pedal
    void allSoundOff() noexcept;                       ///< fades everything out in kStealSeconds

    /** Once per sky tick: the curtain under the strands, the activity (bow strength), an onset
        strength (0: none) and the real time for the drift; ramps the output gains over the tick. */
    void tick (const SkyNodes&, double activity, double onsetStrength, int tickSamples) noexcept;

    /** Adds the voices to left and right. */
    void process (float* left, float* right, int numSamples) noexcept;

    //== State =========================================================================================
    int heldCount() const noexcept;
    int activeCount() const noexcept;
    /** The lowest note with its key down or sustained (transpose applied, so it may be below 0),
        kNoNote if none. */
    int lowestHeld() const noexcept;
    /** Fills `out` with the active voices; returns how many. */
    int view (std::array<View, kSlots>& out) const noexcept;
    int guard() noexcept;

private:
    enum class Stage : uint8_t { idle, attack, sustain, release, steal };

    struct Voice
    {
        Stage stage = Stage::idle;
        int note = -1;                  ///< as played (before transpose)
        float velocity = 0.0f;
        bool gate = false, sustained = false;
        double env = 0.0, velGain = 0.0;
        int home = 0;                   ///< curtain node of the first strand
        uint64_t age = 0;               ///< note-on order
        int pendingNote = -1;           ///< starts when the steal fade ends
        float pendingVelocity = 0.0f;

        std::array<double, kResonators> re {}, im {}, c {}, s {}, inA {};
        std::array<double, kResonators> gL {}, gR {}, dgL {}, dgR {};
        std::array<uint32_t, kStrands> noise {};
        int rampLeft = 0;
    };

    void start (Voice&, int note, float velocity) noexcept;
    void release (Voice&) noexcept;
    void steal (Voice&, int note, float velocity) noexcept;
    void retune (Voice&, bool snapGains, int rampSamples) noexcept;
    double strikeShape() const noexcept;
    double velocityGain (float velocity) const noexcept;
    int nodeOf (const Voice&, int strand) const noexcept;
    bool held (const Voice& v) const noexcept { return v.gate || v.sustained; }

    Settings settings;
    double sampleRate = 48000.0;
    uint32_t seed = 1;
    uint64_t starts = 0;
    bool pedal = false;
    double bowNow = 0.0;                ///< bow input this tick
    double attackStep = 1.0, releaseCoef = 0.0, stealStep = 1.0;
    std::array<double, kNumLayers> radius {}, norm {};   ///< per layer, from the T60s
    std::array<Voice, kSlots> voices {};
    const SkyNodes* curtain = nullptr;  ///< the latest tick's curtain (owned by dsp::Sources)
};

} // namespace worldtable::dsp
