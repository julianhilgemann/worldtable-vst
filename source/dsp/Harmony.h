#pragma once

#include "Voicing.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <span>

namespace worldtable::dsp
{

/** Harmony (spec/05-sound-engine.md §3): prototype/sky/sound/harmony.js, the vocabulary, the naming
    and the voicing helpers (Voicing.h, T3.1), plus the chord hold, the scales and the MIDI latch the
    sketch left out (spec/01-functional-spec.md §4).

    Pure, fixed-size and allocation-free: no std::string, no heap, no locks. The setters are atomic
    and the MIDI state is two bit-masks, so T3.3 can own a Harmony on the audio thread; `chord()`,
    `poll()` and the MIDI calls are audio-thread only. The progression engine itself is T3.3. */

/** The three Moods of the sketch's VOCABULARY. */
enum class Mood : int { bright = 0, bittersweet = 1, nocturnal = 2 };
inline constexpr int kNumMoods = 3;

/** The four chord functions: the sky's phases write these (quiet, growth, onset, recovery). */
enum class ChordFunction : int { home = 0, tension = 1, awe = 2, borrowed = 3 };
inline constexpr int kNumFunctions = 4;

/** One chord of the vocabulary, as the sketch stores it: `degree` semitones above the key,
    `intervals` above the chord root, and the shape name the sketch prints ("maj7", "add9",
    "m add9", …). */
struct ChordShape
{
    std::array<int8_t, Chord::kMaxTones> intervals {};
    int count = 0;
    int degree = 0;
    const char* name = "";
};

/** The chords of one Mood/function: two or three, as the sketch lists them. */
struct Vocabulary
{
    std::array<ChordShape, 3> chords {};
    int count = 0;
};

/** The sketch's VOCABULARY[mood][function]. */
const Vocabulary& vocabulary (Mood, ChordFunction) noexcept;

/** Shape `index` of the function, clamped to the list (a progression cannot ask for a chord that
    doesn't exist). */
const ChordShape& vocabularyChord (Mood, ChordFunction, int index) noexcept;

/** The Chord for that shape in `key` (pitch class, wrapped into 0–11). */
Chord chordOf (Mood, ChordFunction, int index, int key) noexcept;

/** Scales (spec/01 §4): the six the spec names. They don't tune the bank — the layers play chord
    tones (§1) — but the sparkles (T3.4) and the progression's scale-tone step (T3.3) share this. */
enum class Scale : int { major = 0, lydian, majorPentatonic, minor, dorian, minorPentatonic };
inline constexpr int kNumScales = 6;

std::span<const int8_t> scaleIntervals (Scale) noexcept;
bool inScale (Scale, int key, int pc) noexcept;

/** The pitch class nearest `pc` that is in the scale; ties go up. */
int snapToScale (Scale, int key, int pc) noexcept;

/** The sketch's chordName(): a spelled label ("B♭maj7") and a roman numeral ("♭VI"). Fixed buffers,
    so the audio thread can fill one. The shape comes from the intervals, so a latched chord that
    isn't in the vocabulary still gets a name (or "alt"). */
struct ChordName
{
    std::array<char, 24> label {};
    std::array<char, 16> roman {};
    bool minor = false;
};
ChordName nameOf (const Chord&) noexcept;

/** Held MIDI notes (bit-masks of notes 0–63 and 64–127) → a chord: the lowest distinct pitch classes
    (at most Chord::kMaxTones); the lowest note that spells a known shape is the root, else the
    lowest note. With `requireKnownShape`, an unknown shape gives false. The MIDI latch's rule,
    shared with Play mode's readout (T3.7). */
bool chordFromNotes (uint64_t lo, uint64_t hi, bool requireKnownShape, Chord& out) noexcept;

/** The substorm phase, in the sky's order (SkySound.cpp asserts the two agree). */
enum class Phase : int { quiet = 0, growth = 1, onset = 2, recovery = 3 };

/** The chord the instrument holds (spec/01 §4): root key, Mood, chord shape and Transpose from the
    UI, or the last chord latched from MIDI. A note-on latches the held notes; the latch follows the
    notes as they shrink. The chord controls clear the latch, so the next note-on takes over again;
    Transpose applies to both. `chord()` is what SkySound::requestChord() wants.

    MIDI vs progression (T3.6, owner decision 2026-11-05): while notes are latched the held chord
    is authoritative. The sky keeps sensing — `onPhase`/`onOnset`/`update` still advance the target
    function and shape — but `request()` does not raise a pending chord. On the last note-off the
    latch clears and `chord()` is the sky's current chord, which the host (Instrument) polls and
    requests, so the progression resumes from where the sky is then. With Progression 0 that is
    the UI's held chord.

    It also runs the progression (spec/05 §3): the sky's phase writes the chord function and the
    seeded choice; T3.3. `start`, `setSeed` and the setters may be called from the host before
    playback; `onPhase`, `onOnset`, `update`, `take` and the `pending*` readers are audio-thread.
    The progression's default is 0 (hold); the sketch's 1 is the plug-in's parameter default (T3.6). */
class Harmony
{
public:
    void setKey (int pitchClass) noexcept;        ///< 0–11, wraps
    void setMood (Mood) noexcept;
    void setFunction (ChordFunction) noexcept;
    void setChordIndex (int) noexcept;            ///< 0–2; chordOf clamps to the function's size
    void setTranspose (int semitones) noexcept;
    void setScale (Scale) noexcept;

    int  key() const noexcept;
    Mood mood() const noexcept;
    ChordFunction function() const noexcept;
    int  chordIndex() const noexcept;
    int  transpose() const noexcept;
    Scale scale() const noexcept;

    /** MIDI latch (audio thread): the held notes → a chord. */
    void noteOn (int midiNote) noexcept;
    void noteOff (int midiNote) noexcept;
    void allNotesOff() noexcept;
    bool midiHeld() const noexcept;
    int  heldCount() const noexcept;              ///< notes held, not distinct pitch classes
    bool isLatched() const noexcept;              ///< a MIDI chord is sounding

    /** The sounding chord: latched if MIDI is holding, else the held vocabulary chord, transposed. */
    Chord chord() const noexcept;

    /** Audio thread: true (and copies `out`) when chord() differs from the last poll. */
    bool poll (Chord& out) noexcept;

    //== Progression (spec/05 §3; T3.3) ==========================================================
    void setProgression (double amount) noexcept;      ///< 0 hold … 1 follow the sky
    double progression() const noexcept;
    void setSeed (uint32_t seed) noexcept;             ///< the sound seed; before playback
    uint32_t seed() const noexcept;

    /** Seeds the target from the phase at the start (no request); call before playback. */
    void start (Phase) noexcept;

    /** A phase change (SkySound skips the onset phase: onOnset handles it). */
    void onPhase (Phase, int modelMinute, const char* reason = nullptr) noexcept;

    /** A substorm onset; a weak one (< 0.3, a quiet-day pseudo-breakup) only adds tension. */
    void onOnset (float strength, int modelMinute) noexcept;

    /** Per tick: consumes a control change, then wanders if the current function is stale. */
    void update (double realSeconds) noexcept;

    bool hasPending() const noexcept;
    Chord pendingChord() const noexcept;
    bool pendingUrgent() const noexcept;
    int pendingFunction() const noexcept;
    int pendingIndex() const noexcept;
    const char* pendingReason() const noexcept;

    /** The sound applied the pending chord: clear it and restart the wander clock. */
    void take (double realSeconds) noexcept;

    /** Drops a pending request without touching the clock (prepare). */
    void discardPending() noexcept { pendingValid = false; }

private:
    void request (int function, int index, const char* reason) noexcept;
    double wanderDelay() const noexcept;
    static int functionOf (Phase) noexcept;
    bool relatch (bool requireKnownShape) noexcept;

    std::atomic<int> keyPc { 2 };                 ///< D
    std::atomic<int> moodIndex { 0 };
    std::atomic<int> functionIndex { 0 };
    std::atomic<int> shapeIndex { 0 };
    std::atomic<int> transposeSemis { 0 };
    std::atomic<int> scaleIndex { 0 };
    std::atomic<uint64_t> held[2] {};             ///< MIDI 0–63, 64–127
    std::atomic<bool> latched { false };

    Chord latchedData {};
    Chord lastPolled {};
    bool hasPolled = false;

    // progression (audio-thread state; setters above only flag it)
    std::atomic<double> progressionAmount { 0.0 };
    std::atomic<uint32_t> soundSeed { 1 };
    std::atomic<bool> revoice { false };
    std::atomic<const char*> revoiceReason { nullptr };
    int targetFunction = 0, targetIndex = 0;
    std::array<int, kNumFunctions> lastIndex { -1, -1, -1, -1 };
    int moves = 0, wanders = 0;
    double lastChangeS = 0.0, wanderAfterS = 0.0;
    bool pendingValid = false, pendingUrgentFlag = false;
    int pendingFn = 0, pendingIdx = 0;
    const char* pendingWhy = "";
};

} // namespace worldtable::dsp
