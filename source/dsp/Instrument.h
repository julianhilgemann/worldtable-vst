#pragma once

#include "Drive.h"
#include "Harmony.h"
#include "SkySound.h"
#include "Sources.h"
#include "fx/AutoGain.h"
#include "fx/Chorus.h"
#include "fx/Limiter.h"
#include "fx/Phaser.h"
#include "fx/ShimmerReverb.h"
#include "fx/StereoDelay.h"
#include "params/Params.h"
#include "sky/SkyHandoff.h"
#include "sky/SkyRuntime.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace worldtable::dsp
{

/** What the audio thread publishes for the editor (read on the message thread). */
struct Telemetry
{
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    std::atomic<float> gainReduction { 1.0f };   ///< the limiter's minimum gain this block, linear
    std::atomic<float> autoGain { 1.0f };
    std::atomic<float> reverbLevel { 0.0f };
    std::atomic<float> breath { 0.0f };        ///< the Breath cycle 0 … 1 (SkySound::breathCycle)
    std::atomic<int>   lowNote { -1 };         ///< the low resonator's MIDI note (T3.7)
    std::atomic<float> lowLevel { 0.0f };      ///< its output gain
    std::atomic<float> filterHz { 20000.0f };  ///< the filter's cutoff now (after the sky)
    std::atomic<float> playMix { 0.0f };       ///< 0 drone … 1 Play (gliding)
};

/** The played notes for the editor (T3.7): every sounding voice, written by the audio thread each
    block, read by the editor (single reader). */
struct PlayState
{
    static constexpr int kMax = PlayVoices::kSlots;

    std::array<int16_t, kMax> notes {};
    std::array<float, kMax> levels {};
    std::array<uint8_t, kMax> held {};
    int count = 0;
};

/** Single-producer/single-consumer ring of the post-limiter mono mix for the analyzer. */
class ScopeFifo
{
public:
    static constexpr int kSize = 8192;

    void push (const float* l, const float* r, int n) noexcept
    {
        const auto scope = fifo.write (n);
        for (int i = 0; i < scope.blockSize1; ++i)
            data[static_cast<size_t> (scope.startIndex1 + i)] = 0.5f * (l[i] + r[i]);
        for (int i = 0; i < scope.blockSize2; ++i)
            data[static_cast<size_t> (scope.startIndex2 + i)] = 0.5f * (l[scope.blockSize1 + i] + r[scope.blockSize1 + i]);
    }

    /** Reads up to n samples; returns how many were read. */
    int pull (float* dest, int n) noexcept
    {
        const auto scope = fifo.read (std::min (n, fifo.getNumReady()));
        for (int i = 0; i < scope.blockSize1; ++i)
            dest[i] = data[static_cast<size_t> (scope.startIndex1 + i)];
        for (int i = 0; i < scope.blockSize2; ++i)
            dest[scope.blockSize1 + i] = data[static_cast<size_t> (scope.startIndex2 + i)];
        return scope.blockSize1 + scope.blockSize2;
    }

private:
    juce::AbstractFifo fifo { kSize };
    std::array<float, kSize> data {};
};

/** The chord readout hand-off (T3.6): the applied chord's name and the notes each layer plays in
    the current bank. Written by the audio thread when the chord changes, read by the editor. */
struct ChordState
{
    static constexpr int kMaxNotes = ResonatorBank::kMaxVoices;

    std::array<char, 24> label {};
    std::array<char, 16> roman {};
    std::array<std::array<int16_t, kMaxNotes>, kNumLayers> notes {};
    /** The band edges the notes were voiced with (T3.8): floor, low│red, red│green, green│violet,
        ceiling (MIDI notes), and the blend (semitones). In Play mode, the applied bands (the low
        note's floor). */
    std::array<int16_t, BandEdges::kCount> edges {};
    float blend = 0.0f;
    int key = 2;          ///< pitch class of the chord root
    int function = 0;     ///< ChordFunction
    int shape = 0;        ///< index within the function
    int voices = 0;       ///< voices per layer
    int bank = 0;
    bool latched = false; ///< the chord came from MIDI
    bool pending = false; ///< a new chord is waiting for the bank
    bool play = false;    ///< Play mode (T3.7): label and notes are the held notes'
    int held = 0;         ///< Play mode: notes held
    uint32_t changes = 0;
    uint32_t tick = 0;
};

/** The v1 instrument (T3.6): the Sky Model's sound (SkySound), the spec's FX chain, the chord hold
    and the MIDI latch, driven by parameters. T3.7 adds Play mode, the low resonator and the filter
    (dsp::Sources, SkySound's companion).

    ```
    SkyRuntime ─► SkySound ─┬─ PlayVoices, LowResonator ─► Filter ─► Drive ─► Phaser ─► Chorus
                            └─────────────────────────────────────► Delay ─► Reverb ─► AutoGain ─► DC
              ─► master ─► Limiter ─► out
    ```
    MIDI: off, notes latch the drone's chord (T3.6); in Play mode they play the voices, at their
    sample (the block is split at each event).
    Everything is allocated in prepare(); process() is real-time safe (no allocation, locks, I/O,
    logging or exceptions). Controls are read once per block from the APVTS atomics and glided
    (rule 4), so a parameter change never clicks. `process()` is called by the processor on the
    audio thread; `readChord()` is the editor's single reader. */
class Instrument
{
public:
    static constexpr double kGlideSeconds = 0.15;     ///< every control reaching the DSP

    Instrument (params::ParamPointers&, sky::SkyRuntime&);

    /** Message thread: allocates and configures from the current parameters. `runtime` must be
        prepared already. */
    void prepare (double sampleRate, int maximumBlockSize);

    /** Audio thread. */
    void process (juce::AudioBuffer<float>&, const juce::MidiBuffer&) noexcept;

    int getLatencySamples() const noexcept { return limiter.getLatency(); }

    //== State ==================================================================================
    Telemetry& getTelemetry() noexcept { return telemetry; }
    ScopeFifo& getScope() noexcept     { return scope; }

    /** The editor calls this; true when newer played notes arrived (single reader). */
    bool readPlay (PlayState& out) noexcept
    {
        if (! playBuffer.update())
            return false;
        out = playBuffer.front();
        return true;
    }

    /** The editor calls this; true when a newer chord arrived (single reader: the message thread). */
    bool readChord (ChordState& out) noexcept
    {
        if (! chordBuffer.update())
            return false;
        out = chordBuffer.front();
        return true;
    }

    SkySound& sound() noexcept                          { return skySound; }
    const SkySound& sound() const noexcept              { return skySound; }
    Harmony& harmony() noexcept                         { return skySound.harmony(); }
    Sources& sources() noexcept                         { return front; }
    const Sources& sources() const noexcept             { return front; }
    bool playMode() const noexcept                      { return controls.play; }
    const Chord& chord() const noexcept                 { return skySound.chord(); }
    uint32_t chordChanges() const noexcept              { return skySound.chordChanges(); }
    int voices() const noexcept                         { return skySound.voices(); }

    /** The applied chord's name, for tests and logs (audio or message thread). */
    ChordName currentChordName() const noexcept { return nameOf (skySound.chord()); }

private:
    /** One block's parameters: the targets read from the APVTS, and the glided values sent on. */
    struct Controls
    {
        int key = 2, mood = 0, scale = 0, shape = 0, transpose = 0;
        float progression = 1.0f, detune = 3.0f, drift = 4.0f, width = 0.9f;
        Bands bands;   ///< T3.8: snap (SkySound applies them once still for 300 ms)
        std::array<float, kNumLayers> level { 1.0f, 0.7f, 0.4f };
        std::array<float, kNumLayers> t60 { 20.0f, 6.0f, 2.5f };
        float storm = 0.5f, breath = 0.35f, glow = 0.5f, space = 0.5f, driftMacro = 0.5f;

        float drive = 0.15f;
        float phRate = 0.05f, phDepth = 0.6f, phFeedback = 0.3f, phMix = 0.4f;
        int phStages = 6;
        float chRate = 0.1f, chDepth = 3.0f, chMix = 0.25f;
        float dlyTime = 1.8f, dlyFb = 0.35f, dlyTone = 0.5f, dlyMix = 0.25f;
        bool dlyPingPong = true;
        float revSize = 0.85f, revDecay = 9.0f, revDamp = 0.35f, revPreDelay = 20.0f;
        float revShimmer = 0.2f, revMod = 0.4f, revMix = 0.35f;
        float ceiling = -1.0f, release = 200.0f, masterDb = 0.0f;

        bool play = false;
        float playAttack = 0.01f, playRelease = 2.5f, playVelocity = 0.6f, playDrone = 0.0f;
        float lowLevel = 0.5f, lowDecay = 8.0f;
        int fltType = 0;
        float fltCutoff = 20000.0f, fltReso = 0.1f, fltSky = 0.3f;
    };

    void readTarget (Controls&) const noexcept;
    void glideControls (const Controls& target, int numSamples) noexcept;
    SkySound::Settings settingsFrom (const Controls&) const noexcept;
    Sources::Settings sourcesFrom (const Controls&) const noexcept;
    void applyControls() noexcept;
    void handleMidi (const juce::MidiBuffer&) noexcept;
    void syncLatch() noexcept;
    void playEvent (const juce::MidiMessage&) noexcept;
    void renderPlay (float* left, float* right, int numSamples, const juce::MidiBuffer&) noexcept;
    void publishChord() noexcept;
    void publishPlay() noexcept;
    void configureHarmony (const Controls&) noexcept;

    params::ParamPointers& p;
    sky::SkyRuntime& runtime;

    double sampleRate = 48000.0;
    int numNodes = 64;
    uint32_t seed = 1;

    SkySound skySound;
    Sources front;   ///< SkySound's companion: the played notes, the low resonator, the filter
    Drive drive;
    Phaser phaser;
    Chorus chorus;
    StereoDelay delay;
    ShimmerReverb reverb;
    AutoGain autoGain;
    DcBlocker dcL, dcR;
    Limiter limiter;

    Controls controls, target;
    int lastKey = -1, lastMood = -1, lastShape = -1, lastTranspose = 0, lastScale = -1;
    float lastProgression = -1.0f;
    float masterGain = 1.0f;
    bool prepared = false;
    bool lastPlay = false, autoGainHeld = false;

    Telemetry telemetry;
    ScopeFifo scope;
    sky::TripleBuffer<ChordState> chordBuffer;
    bool chordPublished = false;
    uint32_t publishedChanges = 0;
    bool publishedLatched = false, publishedPending = false, publishedPlay = false;
    Bands publishedBands {};
    std::array<uint64_t, 2> publishedHeld {};
    sky::TripleBuffer<PlayState> playBuffer;
    std::array<PlayVoices::View, PlayVoices::kSlots> playView {};

    JUCE_DECLARE_NON_COPYABLE (Instrument)
};

} // namespace worldtable::dsp
