#pragma once

#include "Harmony.h"
#include "SkySound.h"
#include "Sources.h"
#include "fx/AutoGain.h"
#include "fx/Chorus.h"
#include "fx/Limiter.h"
#include "fx/Phaser.h"
#include "fx/ShimmerReverb.h"
#include "fx/StereoDelay.h"
#include "mod/ModMatrix.h"
#include "params/Params.h"
#include "sky/SkyHandoff.h"
#include "sky/SkyRuntime.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <span>

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
    std::atomic<float> eqCutHz { 20000.0f };   ///< the EQ's high cut now (after the sky), 20000 = open (T3.9)
    std::atomic<float> playMix { 0.0f };       ///< 0 drone … 1 Play (gliding)
    std::atomic<float> phaserPhase { 0.0f };   ///< the phaser's left LFO phase 0 … 1, per block (T6.12)
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

/** Single-producer/single-consumer ring of a mono mix for the editor's analyzer: the post-limiter
    output (`getScope`) and, since T6.12, the sources before the tone stage (`getInputScope`). When
    nobody reads, it fills and push() drops what does not fit. */
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
    and the MIDI latch, driven by parameters. T3.7 adds Play mode and the low resonator, T3.9 the EQ
    with drive per band in place of the filter and the drive (dsp::Sources, SkySound's companion).

    ```
    SkyRuntime ─► SkySound ─┬─ PlayVoices, LowResonator ─► Eq (4 bands, drive each) ─► Phaser ─► Chorus
                            └───────────────────────────────────────────────────► Delay ─► Reverb ─► AutoGain ─► DC
              ─► master ─► Limiter ─► out
    ```
    MIDI: off, notes latch the drone's chord (T3.6); in Play mode they play the voices, at their
    sample (the block is split at each event). Every note also reaches the envelope at its sample.

    The modulation grid (T4.2, dsp/mod/ModMatrix.h): the Instrument is the sky's tick listener and
    wraps SkySound. Before each tick it glides the controls, advances the sources (the macros, the
    breath cycle, the slewed activity, the onset swell, the two LFOs and the envelope), evaluates
    the grid and hands the modulated targets on: levels, detune, drift, width and Strike to the
    curtain and its companion, the EQ's gains, cut and drives to the companion, Progression to the
    harmony. The effects run after each tick segment, their modulated values gliding linearly
    from one tick's to the next's on a 32-sample grid anchored to the tick. So the output is the
    same at any block size (rule 5) and a moving value never steps (rule 4).

    Everything is allocated in prepare(); process() is real-time safe (no allocation, locks, I/O,
    logging or exceptions). Controls are read once per block from the APVTS atomics and glided per
    tick (rule 4), so a parameter change never clicks. `process()` is called by the processor on the
    audio thread; `readChord()` and `readModulation()` are the editor's single reader. */
class Instrument : private sky::SkyRuntime::TickListener
{
public:
    static constexpr double kGlideSeconds = 0.15;     ///< every control reaching the DSP
    static constexpr int kFxStepSamples = 32;         ///< the effects' modulated values glide on this grid
    static constexpr double kOnsetAttack = 0.08, kOnsetTau = 6.0;   ///< the Onset source's swell (s)

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
    ScopeFifo& getInputScope() noexcept { return inputScope; }   ///< before the tone stage (T6.12)

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

    //== Modulation (T4.2) ======================================================================
    /** The grid's hand-off: the processor publishes the depths here (one producer at a time). */
    mod::DepthsExchange& depthsExchange() noexcept { return depthsIn; }

    /** Any thread: the Envelope panel's Fire button. Taken at the next block, fired on the next tick. */
    void fireEnvelope() noexcept { fireRequests.fetch_add (1, std::memory_order_release); }

    /** The editor calls this; true when a newer modulation frame arrived (single reader). */
    bool readModulation (mod::ModTelemetry& out) noexcept
    {
        if (! modBuffer.update())
            return false;
        out = modBuffer.front();
        return true;
    }

    // tests: the grid as the audio thread last evaluated it
    const mod::ModMatrix& modMatrix() const noexcept     { return matrix; }
    const mod::Frame& modFrame() const noexcept          { return frame; }
    const mod::Envelope& envelope() const noexcept       { return env; }
    const mod::SlowLfo& lfo (int i) const noexcept       { return lfos[static_cast<size_t> (i & 1)]; }

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
        Eq::Settings eq;       ///< T3.9: snap (the EQ glides them per sample)

        // T4.2: the LFOs and the envelope (snap: the LFO keeps its phase, the envelope its value)
        std::array<float, 2> lfoRate { 0.12f, 0.03f };
        std::array<int, 2> lfoShape { 0, 2 };
        float envAttack = 0.1f, envDecay = 0.6f, envSustain = 0.35f, envRelease = 1.5f;
        int envTrigger = 0;
    };

    /** The effects' modulated values: they glide from one tick's to the next's. */
    struct FxValues
    {
        std::array<float, 7> v {};   ///< ph_mix, ch_mix, dly_fb, dly_mix, rev_decay, rev_shimmer, rev_mix
        bool differs (const FxValues& o) const noexcept
        {
            for (size_t i = 0; i < v.size(); ++i)
                if (std::abs (v[i] - o.v[i]) > 0.0f)
                    return true;
            return false;
        }
    };

    /** The Onset source: each substorm onset swells it to 1 in kOnsetAttack, linearly from where it
        is, then it decays as e^(−t / kOnsetTau). On the tick grid. */
    struct OnsetSwell
    {
        double value = 0.0, from = 0.0, t = -1.0;   ///< t < 0: no swell running
        void trigger() noexcept { from = value; t = 0.0; }
        void tick() noexcept;
    };

    void readTarget (Controls&) const noexcept;
    void snapControls (const Controls& target) noexcept;
    void glideControls (const Controls& target, int numSamples) noexcept;
    SkySound::Settings settingsFrom (const Controls&, const mod::Frame&) const noexcept;
    Sources::Settings sourcesFrom (const Controls&, const mod::Frame&) const noexcept;
    void applyControls() noexcept;
    void syncLatch() noexcept;
    void playEvent (const juce::MidiMessage&) noexcept;
    void render (float* left, float* right, int numSamples, const juce::MidiBuffer&, bool play) noexcept;
    void renderSky (float* left, float* right, int numSamples) noexcept;

    // T4.2: the tick listener (wrapping SkySound's) and the grid
    void skyTicked (const sky::SkyModel&, std::span<const sky::SkyEvent>, int tickSamples) noexcept override;
    void renderSamples (int offset, int numSamples) noexcept override;
    void configureMotion (const Controls&) noexcept;
    mod::SourceValues sourceValues (uint64_t tick) const noexcept;
    mod::Bases basesFrom (const Controls&) const noexcept;
    void applyFrame (bool atOnce) noexcept;
    FxValues fxOf (const mod::Frame&) const noexcept;
    void setEffects (const FxValues&) noexcept;
    void renderEffects (float* left, float* right, int numSamples) noexcept;
    void publishModulation() noexcept;
    void publishChord() noexcept;
    void publishPlay() noexcept;
    void configureHarmony (const Controls&) noexcept;

    params::ParamPointers& p;
    sky::SkyRuntime& runtime;

    double sampleRate = 48000.0;
    int numNodes = 64;
    uint32_t seed = 1;

    SkySound skySound;
    Sources front;   ///< SkySound's companion: the played notes, the low resonator, the EQ
    Phaser phaser;
    Chorus chorus;
    StereoDelay delay;
    ShimmerReverb reverb;
    AutoGain autoGain;
    DcBlocker dcL, dcR;
    Limiter limiter;

    Controls controls, target;

    // T4.2
    mod::ModMatrix matrix;
    mod::Frame frame;
    std::array<mod::SlowLfo, 2> lfos;
    mod::Envelope env;
    OnsetSwell onsetSwell;
    mod::DepthsExchange depthsIn;
    std::atomic<uint32_t> fireRequests { 0 };
    uint32_t firesTaken = 0;
    sky::TripleBuffer<mod::ModTelemetry> modBuffer;
    bool modTicked = false;          ///< a tick ran in this block (publish the frame)
    uint64_t modTicks = 0;
    FxValues fxFrom, fxTo;
    bool fxMoving = false;
    int tickLength = 0, intoTick = 0;   ///< the current tick's samples, and how many are rendered
    float* outLeft = nullptr;           ///< the part of the block being rendered (renderSky)
    float* outRight = nullptr;

    int lastKey = -1, lastMood = -1, lastShape = -1, lastTranspose = 0, lastScale = -1;
    float lastProgression = -1.0f;
    float masterGain = 1.0f;
    bool prepared = false;
    bool lastPlay = false, autoGainHeld = false;

    Telemetry telemetry;
    ScopeFifo scope, inputScope;
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
