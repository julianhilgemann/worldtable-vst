#pragma once

#include "Drive.h"
#include "Harmony.h"
#include "SkySound.h"
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
    int key = 2;          ///< pitch class of the chord root
    int function = 0;     ///< ChordFunction
    int shape = 0;        ///< index within the function
    int voices = 0;       ///< voices per layer
    int bank = 0;
    bool latched = false; ///< the chord came from MIDI
    bool pending = false; ///< a new chord is waiting for the bank
    uint32_t changes = 0;
    uint32_t tick = 0;
};

/** The v1 instrument (T3.6): the Sky Model's sound (SkySound), the spec's FX chain, the chord hold
    and the MIDI latch, driven by parameters.

    ```
    SkyRuntime ─► SkySound ─► Drive ─► Phaser ─► Chorus ─► Delay ─► Reverb ─► AutoGain ─► DC
              ─► master ─► Limiter ─► out
    ```
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
    const Chord& chord() const noexcept                 { return skySound.chord(); }
    uint32_t chordChanges() const noexcept              { return skySound.chordChanges(); }
    int voices() const noexcept                         { return skySound.voices(); }

    /** The applied chord's name, for tests and logs (audio or message thread). */
    ChordName currentChordName() const noexcept { return nameOf (skySound.chord()); }

private:
    /** One block's parameters: the targets read from the APVTS, and the glided values sent on. */
    struct Controls
    {
        int key = 2, mood = 0, scale = 0, shape = 0, transpose = 0, registerSemis = 0;
        float progression = 1.0f, spread = 2.0f, detune = 3.0f, drift = 4.0f, width = 0.9f;
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
    };

    void readTarget (Controls&) const noexcept;
    void glideControls (const Controls& target, int numSamples) noexcept;
    SkySound::Settings settingsFrom (const Controls&) const noexcept;
    void applyControls() noexcept;
    void handleMidi (const juce::MidiBuffer&) noexcept;
    void publishChord() noexcept;
    void configureHarmony (const Controls&) noexcept;

    params::ParamPointers& p;
    sky::SkyRuntime& runtime;

    double sampleRate = 48000.0;
    int numNodes = 64;
    uint32_t seed = 1;

    SkySound skySound;
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

    Telemetry telemetry;
    ScopeFifo scope;
    sky::TripleBuffer<ChordState> chordBuffer;
    bool chordPublished = false;
    uint32_t publishedChanges = 0;
    bool publishedLatched = false, publishedPending = false;

    JUCE_DECLARE_NON_COPYABLE (Instrument)
};

} // namespace worldtable::dsp
