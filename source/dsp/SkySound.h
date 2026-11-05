#pragma once

#include "Exciter.h"
#include "Harmony.h"
#include "ResonatorBank.h"
#include "Voicing.h"
#include "sky/SkyRuntime.h"

#include <array>
#include <cstdint>

namespace worldtable::dsp
{

/** The curtain, heard (spec/05-sound-engine.md §1–2, §12): prototype/sky/sound/engine.js `tick()`
    and `process()`, without the model (SkyRuntime owns it), the harmony (T3.2, T3.3) and the
    effects (T3.5).

    Each voice follows a point of the curtain (one per node, or two with voicesPerNode = 2) and has
    three resonators, one per layer. Per tick: the node's red/green/violet → layer gain (√colour ×
    level × presence, slewed 2 s); displacement → ± detune cents; a seeded drift; dome azimuth →
    equal-power pan (east left, like the dome); elevation → presence.

    Chord changes are double-banked: the new chord is voice-led into the silent bank and the bed
    moves over to it in 12 s (equal power) while the old bank rings out. A request waits while the
    old bank still rings, unless it is urgent (a sky move or a user change): then the old bank is
    damped to a T60 of 3 s. A newer request replaces a pending one.

    Deterministic (CLAUDE.md rule 5): the sound changes only on ticks and samples, so the output
    doesn't depend on the block size. prepare() runs on the message thread; everything else is
    real-time safe and allocation-free. */
class SkySound : private sky::SkyRuntime::TickListener
{
public:
    static constexpr int kMaxVoices = ResonatorBank::kMaxVoices;

    /** The sketch's SOUND_DEFAULTS that this part of the sound uses. Internal constants until the
        parameters arrive (parameters.yaml v1, T3.6). */
    struct Settings
    {
        int    voicesPerNode = 1;                         ///< 1: V = nodes; 2: a voice between neighbours too
        double registerSemitones = 0.0;
        double spread = 2.0;                              ///< voicing spread, octaves per layer at 2
        std::array<double, kNumLayers> level { 1.0, 0.7, 0.4 };     ///< red, green, violet
        std::array<double, kNumLayers> t60 { 20.0, 6.0, 2.5 };      ///< s
        Exciter::Settings exciter;
        double detuneCents = 3.0;                         ///< at ± 160 km displacement
        double driftCents = 4.0;                          ///< slow per-voice wander
        double gainSlewSeconds = 2.0;                     ///< colour → gain (rule 4: ≥ 2 s)
        double crossfadeSeconds = 12.0;                   ///< the bed moves to a new chord's bank
        double width = 0.9;                               ///< stereo width of the dome azimuth
    };

    static constexpr double kDampT60 = 3.0;               ///< s: an urgent change damps the old bank
    static constexpr double kPanAltitudeKm = 110.0;       ///< the green line's altitude, for the pan

    /** Message thread. `nodes`: the sky's node count (SkyParams::nodes); nodes × voicesPerNode ≤
        kMaxVoices. Voices the start chord into bank 0 and silences everything. */
    void prepare (double sampleRate, int nodes, uint32_t seed, const Settings&, const Chord& start) noexcept;

    /** Audio thread: steps the sky through `runtime` and renders its sound into left and right
        (overwrites). */
    void process (sky::SkyRuntime& runtime, float* left, float* right, int numSamples) noexcept;

    /** Audio thread, between blocks: asks for a chord. Taken on a tick when the banks allow (see the
        class comment). The progression asks by itself once `harmony()` is configured. */
    void requestChord (const Chord&, bool urgent) noexcept;

    /** The chord hold + progression (T3.2/T3.3). Configure it and call start() before playback; the
        sound feeds it the tick's phase and onset events and applies its requests. */
    Harmony& harmony() noexcept { return harmonyEngine; }
    uint64_t lastChordTick() const noexcept { return chordTick; }

    //== State (tests, later the UI) ==================================================================
    const Chord& chord() const noexcept       { return applied; }   ///< newest applied chord
    bool hasPendingChord() const noexcept     { return pendingValid; }
    int  currentBank() const noexcept         { return cur; }
    bool bankActive (int bank) const noexcept { return resonators.isActive (bank); }
    double crossfade() const noexcept         { return xf; }        ///< 0 … 1 toward the current bank
    bool damping() const noexcept             { return dampStart >= 0.0; }
    uint64_t ticks() const noexcept           { return tickCount; }
    uint32_t chordChanges() const noexcept    { return changes; }
    int  resets() const noexcept              { return guardResets; }
    int  voices() const noexcept              { return numVoices; }
    int  note (int bank, Layer, int voice) const noexcept;

private:
    void skyTicked (const sky::SkyModel&, std::span<const sky::SkyEvent>, int tickSamples) noexcept override;
    void renderSamples (int offset, int numSamples) noexcept override;

    void geometry (const sky::SkyModel&) noexcept;
    void voiceBank (int bank, const Chord&, int fromBank) noexcept;
    void applyChord (double realSeconds) noexcept;
    double realSeconds() const noexcept;

    Settings settings;
    double sampleRate = 48000.0;
    uint32_t seed = 1;
    int numNodes = 0, numVoices = 0;

    ResonatorBank resonators;
    Exciter exciter;
    Harmony harmonyEngine;

    // voices: position along the chain, slot, notes per bank and layer
    std::array<double, kMaxVoices> pos {};
    std::array<int8_t, kMaxVoices> slotOf {};
    std::array<std::array<std::array<int16_t, kMaxVoices>, kNumLayers>, ResonatorBank::kBanks> notes {};
    std::array<std::array<Slots, kNumLayers>, ResonatorBank::kBanks> slotPcs {};

    // per tick
    std::array<double, kNumLayers * kMaxVoices> gain {};
    std::array<double, kMaxVoices> brightness {};
    std::array<double, kMaxVoices> panL {}, panR {}, presence {};
    std::array<double, kMaxVoices> panNode {}, presenceNode {};   ///< per node, 10 Hz
    bool panInit = false;
    uint64_t tickCount = 0;

    // chords: bank `cur` takes the excitation; the other rings out, then retires
    int cur = 0;
    double xf = 1.0, xfStart = 0.0, xfEnd = 0.0;
    double dampStart = -1.0;              ///< real s when damping began; < 0: not damping
    Chord applied {}, pending {};
    bool pendingValid = false, pendingUrgent = false, pendingFromHarmony = false;
    uint32_t changes = 0;
    uint64_t chordTick = 0;
    int guardResets = 0;

    // output of the block being rendered
    float* outLeft = nullptr;
    float* outRight = nullptr;
};

} // namespace worldtable::dsp
