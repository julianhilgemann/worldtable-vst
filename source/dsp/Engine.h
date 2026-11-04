#pragma once

#include "Lfo.h"
#include "TableExchange.h"
#include "Voice.h"
#include "fx/Limiter.h"
#include "fx/ShimmerReverb.h"
#include "fx/StereoDelay.h"
#include "params/Params.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>

namespace worldtable::dsp
{

/** Lock-free values the audio thread publishes for the UI (read on the message thread). */
struct Telemetry
{
    std::atomic<float> position { 0.0f };       // effective table position of the focus voice
    std::atomic<float> ampEnv { 0.0f };
    std::atomic<float> filterEnv { 0.0f };
    std::atomic<float> cutoffHz { 1000.0f };
    std::atomic<float> lfo1 { 0.0f }, lfo2 { 0.0f };
    std::atomic<float> lfo1Phase { 0.0f }, lfo2Phase { 0.0f };
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    std::atomic<float> gainReduction { 1.0f };  // linear, ≤ 1
    std::atomic<float> reverbLevel { 0.0f };
    std::atomic<float> tableFade { 1.0f };
    std::atomic<int> euclidStep { -1 };
    std::atomic<uint32_t> euclidHits { 0 };      // increments on every euclid hit
    std::atomic<int> activeVoices { 0 };
    std::atomic<double> bpm { 90.0 };
    std::array<std::atomic<uint32_t>, 4> noteMask {}; // 128 bits: notes currently sounding
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

/** Host transport snapshot for one block. */
struct HostTime
{
    double bpm = 0.0;        // 0 → unknown
    bool isPlaying = false;
    bool hasPpq = false;
    double ppq = 0.0;
};

/** The synth: note sources (drone / euclid / MIDI) → voices → delay → reverb → limiter.
    Real-time safe: all memory is allocated in prepare(). */
class Engine
{
public:
    static constexpr int kNumVoices = 16;
    static constexpr float kTableFadeSeconds = 1.5f;

    Engine (params::ParamPointers&, TableExchange&);

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void process (juce::AudioBuffer<float>&, const juce::MidiBuffer&, const HostTime&) noexcept;

    Telemetry& getTelemetry() noexcept { return telemetry; }
    ScopeFifo& getScope() noexcept     { return scope; }
    int getLatencySamples() const noexcept { return limiter.getLatency(); }

private:
    struct Controls;  // per-chunk parameter snapshot

    void readControls (Controls&) noexcept;
    void pickUpTables() noexcept;
    void prepareChunk (const Controls&, int numSamples) noexcept;
    void updateDrone (const Controls&) noexcept;
    void runEuclid (const Controls&, int numSamples, double ppqPerSample) noexcept;
    void handleMidi (const juce::MidiMessage&) noexcept;
    void startNote (int note, float velocity, Voice::Owner, int64_t gateSamples) noexcept;
    void releaseOwner (Voice::Owner) noexcept;
    int noteFromPool (const Controls&, float u) const noexcept;
    void publishTelemetry (const Controls&) noexcept;

    params::ParamPointers& p;
    TableExchange& exchange;

    double sampleRate = 48000.0;
    std::array<Voice, kNumVoices> voices;
    uint64_t voiceAge = 0;
    int focusVoice = -1;

    // tables
    int currentSlot = -1, previousSlot = -1;
    float tableFade = 1.0f, tableFadeInc = 0.0f;

    // modulation
    Lfo lfo1, lfo2;
    Smoother posSmooth, cutoffSmooth, resoSmooth, driveSmooth, detuneSmooth, levelSmooth, widthSmooth;
    Smoother delayMixSmooth, revMixSmooth, masterSmooth;
    ChunkContext ctx;

    // note sources
    std::array<int, 8> droneNotes {};
    int numDroneNotes = 0;
    int lastMode = -1;
    double clockPpq = 0.0;
    int64_t lastStepIndex = -1;
    std::array<float, 32> stepNoteU {};
    FastRandom seqRng;

    // fx
    StereoDelay delay;
    ShimmerReverb reverb;
    Limiter limiter;
    std::array<float, kChunk> mixL {}, mixR {};

    Telemetry telemetry;
    ScopeFifo scope;
    float peakL = 0.0f, peakR = 0.0f, minGain = 1.0f;
    double currentBpm = 90.0;
};

} // namespace worldtable::dsp
