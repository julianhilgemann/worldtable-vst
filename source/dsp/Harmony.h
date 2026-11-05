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

/** The chord the instrument holds (spec/01 §4): root key, Mood, chord shape and Transpose from the
    UI, or the last chord latched from MIDI. A note-on latches the held notes; all notes off keeps
    the last chord. The chord controls clear the latch, so the next note-on takes over again;
    Transpose applies to both. `chord()` is what SkySound::requestChord() wants. */
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
    bool midiHeld() const noexcept;
    int  heldCount() const noexcept;              ///< notes held, not distinct pitch classes

    /** The sounding chord: latched if MIDI is holding, else the held vocabulary chord, transposed. */
    Chord chord() const noexcept;

    /** Audio thread: true (and copies `out`) when chord() differs from the last poll. */
    bool poll (Chord& out) noexcept;

private:
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
};

} // namespace worldtable::dsp
