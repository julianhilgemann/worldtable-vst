#include "Harmony.h"

#include <algorithm>
#include <bit>
#include <cstddef>

// prototype/sky/sound/harmony.js, the vocabulary, chordName() and the voicing section, plus the
// chord hold, the scales and the MIDI latch (spec/01-functional-spec.md §4).

namespace worldtable::dsp
{

namespace
{
constexpr int wrap12 (int pc) noexcept { return ((pc % 12) + 12) % 12; }

constexpr ChordShape shape (std::initializer_list<int> intervals, int degree, const char* name)
{
    ChordShape s;
    s.count = static_cast<int> (intervals.size());
    int i = 0;
    for (const int iv : intervals)
        s.intervals[static_cast<size_t> (i++)] = static_cast<int8_t> (iv);
    s.degree = degree;
    s.name = name;
    return s;
}

constexpr Vocabulary voc (std::initializer_list<ChordShape> chords)
{
    Vocabulary v;
    v.count = static_cast<int> (chords.size());
    int i = 0;
    for (const auto& c : chords)
        v.chords[static_cast<size_t> (i++)] = c;
    return v;
}

/** harmony.js VOCABULARY. Order: bright, bittersweet, nocturnal × home, tension, awe, borrowed. */
constexpr std::array<std::array<Vocabulary, kNumFunctions>, kNumMoods> kVocabulary { {
    { {   // bright
        voc ({ shape ({ 0, 4, 7, 11 }, 0, "maj7"), shape ({ 0, 4, 7, 14 }, 0, "add9"),
                shape ({ 0, 4, 7, 9, 14 }, 0, "6/9") }),
        voc ({ shape ({ 0, 2, 7, 11 }, 0, "maj7sus2"), shape ({ 0, 4, 7, 14 }, 5, "add9"),
                shape ({ 0, 5, 7, 14 }, 7, "sus4 add9") }),
        voc ({ shape ({ 0, 4, 7, 11 }, 8, "maj7"), shape ({ 0, 4, 7, 14 }, 4, "add9"),
                shape ({ 0, 4, 7, 14 }, 9, "add9") }),
        voc ({ shape ({ 0, 3, 7, 14 }, 5, "m add9"), shape ({ 0, 4, 7, 14 }, 10, "add9"),
                shape ({ 0, 4, 7, 14 }, 8, "add9") }),
    } },
    { {   // bittersweet
        voc ({ shape ({ 0, 4, 7, 14 }, 0, "add9"), shape ({ 0, 4, 7, 11, 14 }, 0, "maj9"),
                shape ({ 0, 3, 7, 10, 14 }, 9, "m9") }),
        voc ({ shape ({ 0, 4, 7, 11 }, 5, "maj7"), shape ({ 0, 3, 7, 10, 14 }, 2, "m9"),
                shape ({ 0, 5, 7, 10 }, 0, "7sus4") }),
        voc ({ shape ({ 0, 4, 7, 11 }, 8, "maj7"), shape ({ 0, 4, 7, 11 }, 3, "maj7") }),
        voc ({ shape ({ 0, 3, 7, 14 }, 5, "m add9"), shape ({ 0, 3, 7, 9 }, 5, "m6"),
                shape ({ 0, 4, 7, 14 }, 8, "add9") }),
    } },
    { {   // nocturnal
        voc ({ shape ({ 0, 3, 7, 10, 14 }, 0, "m9"), shape ({ 0, 3, 7, 14 }, 0, "m add9"),
                shape ({ 0, 3, 7, 10, 17 }, 0, "m11") }),
        voc ({ shape ({ 0, 5, 7, 10 }, 0, "7sus4"), shape ({ 0, 2, 7, 14 }, 10, "sus2"),
                shape ({ 0, 5, 7, 10 }, 7, "7sus4") }),
        voc ({ shape ({ 0, 4, 7, 11 }, 8, "maj7"), shape ({ 0, 3, 7, 10 }, 9, "m7"),
                shape ({ 0, 4, 7, 14 }, 4, "add9") }),
        voc ({ shape ({ 0, 3, 7, 14 }, 5, "m add9"), shape ({ 0, 4, 7, 14 }, 10, "add9"),
                shape ({ 0, 4, 7, 11, 14 }, 3, "maj9") }),
    } },
} };

/** The shape names the sketch's chords use, plus the plain triads/power chord a MIDI latch can
    spell. Intervals are above the chord root; pitch-class versions (no 14/17) are the aliases a
    latched chord matches. */
struct NamedShape
{
    std::array<int8_t, Chord::kMaxTones> intervals {};
    int count = 0;
    const char* name = "";
    bool minor = false;
};

constexpr NamedShape named (std::initializer_list<int> intervals, const char* name, bool minor)
{
    NamedShape s;
    s.count = static_cast<int> (intervals.size());
    int i = 0;
    for (const int iv : intervals)
        s.intervals[static_cast<size_t> (i++)] = static_cast<int8_t> (iv);
    s.name = name;
    s.minor = minor;
    return s;
}

constexpr std::array kShapes {
    named ({ 0, 7 }, "5", false),
    named ({ 0, 4, 7 }, "maj", false),
    named ({ 0, 3, 7 }, "m", true),
    named ({ 0, 3, 6 }, "dim", true),
    named ({ 0, 4, 8 }, "aug", false),
    named ({ 0, 2, 7 }, "sus2", false),
    named ({ 0, 2, 7, 14 }, "sus2", false),                // the sketch's sus2
    named ({ 0, 5, 7 }, "sus4", false),
    named ({ 0, 4, 7, 9 }, "6", false),
    named ({ 0, 3, 7, 9 }, "m6", true),
    named ({ 0, 4, 7, 10 }, "7", false),
    named ({ 0, 4, 7, 11 }, "maj7", false),
    named ({ 0, 3, 7, 10 }, "m7", true),
    named ({ 0, 3, 6, 10 }, "m7\xE2\x99\xAD" "5", true),   // m7♭5
    named ({ 0, 3, 6, 9 }, "dim7", true),
    named ({ 0, 2, 4, 7 }, "add9", false),                 // latched / pitch-class form
    named ({ 0, 4, 7, 14 }, "add9", false),                // the sketch's form
    named ({ 0, 2, 3, 7 }, "m add9", true),
    named ({ 0, 3, 7, 14 }, "m add9", true),
    named ({ 0, 2, 4, 7, 10 }, "9", false),
    named ({ 0, 2, 4, 7, 11 }, "maj9", false),
    named ({ 0, 4, 7, 11, 14 }, "maj9", false),
    named ({ 0, 2, 3, 7, 10 }, "m9", true),
    named ({ 0, 3, 7, 10, 14 }, "m9", true),
    named ({ 0, 2, 4, 7, 9 }, "6/9", false),
    named ({ 0, 4, 7, 9, 14 }, "6/9", false),
    named ({ 0, 3, 7, 10, 17 }, "m11", true),
    named ({ 0, 2, 5, 7 }, "sus4 add9", false),
    named ({ 0, 5, 7, 14 }, "sus4 add9", false),
    named ({ 0, 5, 7, 10 }, "7sus4", false),
    named ({ 0, 2, 7, 11 }, "maj7sus2", false),
};

constexpr std::array<std::array<int8_t, 7>, kNumScales> kScaleIntervals { {
    { { 0, 2, 4, 5, 7, 9, 11 } },   // major
    { { 0, 2, 4, 6, 7, 9, 11 } },   // Lydian
    { { 0, 2, 4, 7, 9, 0, 0 } },    // major pentatonic
    { { 0, 2, 3, 5, 7, 8, 10 } },   // minor
    { { 0, 2, 3, 5, 7, 9, 10 } },   // Dorian
    { { 0, 3, 5, 7, 10, 0, 0 } },   // minor pentatonic
} };

constexpr std::array<int, kNumScales> kScaleSizes { 7, 7, 5, 7, 7, 5 };

constexpr std::array<const char*, 7> kLetterNames { "C", "D", "E", "F", "G", "A", "B" };
constexpr std::array<int, 12> kKeyLetterIndex { 0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6 };
constexpr std::array<int, 7> kLetterPc { 0, 2, 4, 5, 7, 9, 11 };
constexpr std::array<int, 12> kDegreeLetter { 0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6 };
constexpr std::array<const char*, 12> kRoman { "I", "\xE2\x99\xAD" "II", "II", "\xE2\x99\xAD" "III",
                                                "III", "IV", "\xE2\x99\xAF" "IV", "V",
                                                "\xE2\x99\xAD" "VI", "VI", "\xE2\x99\xAD" "VII", "VII" };
constexpr const char* kFlat = "\xE2\x99\xAD";    // ♭
constexpr const char* kSharp = "\xE2\x99\xAF";   // ♯

const NamedShape* findShape (const std::array<int8_t, Chord::kMaxTones>& intervals, int count) noexcept
{
    for (const auto& s : kShapes)
        if (s.count == count)
        {
            bool same = true;
            for (int k = 0; k < count && same; ++k)
                same = s.intervals[static_cast<size_t> (k)] == intervals[static_cast<size_t> (k)];
            if (same)
                return &s;
        }
    return nullptr;
}

void append (char*& p, const char* s, char* end) noexcept
{
    while (*s != '\0' && p < end)
        *p++ = *s++;
}

bool sameChord (const Chord& a, const Chord& b) noexcept
{
    if (a.key != b.key || a.degree != b.degree || a.count != b.count)
        return false;
    for (int i = 0; i < a.count; ++i)
        if (a.intervals[static_cast<size_t> (i)] != b.intervals[static_cast<size_t> (i)])
            return false;
    return true;
}

template <typename Enum>
int indexOf (Enum e, int count) noexcept
{
    return std::clamp (static_cast<int> (e), 0, count - 1);
}
} // namespace

//==================================================================================================
const Vocabulary& vocabulary (Mood mood, ChordFunction fn) noexcept
{
    return kVocabulary[static_cast<size_t> (indexOf (mood, kNumMoods))]
                      [static_cast<size_t> (indexOf (fn, kNumFunctions))];
}

const ChordShape& vocabularyChord (Mood mood, ChordFunction fn, int index) noexcept
{
    const auto& v = vocabulary (mood, fn);
    return v.chords[static_cast<size_t> (std::clamp (index, 0, v.count - 1))];
}

Chord chordOf (Mood mood, ChordFunction fn, int index, int key) noexcept
{
    const auto& s = vocabularyChord (mood, fn, index);
    Chord c;
    c.key = wrap12 (key);
    c.degree = s.degree;
    c.count = s.count;
    c.intervals = s.intervals;
    return c;
}

//==================================================================================================
std::span<const int8_t> scaleIntervals (Scale scale) noexcept
{
    const auto i = static_cast<size_t> (indexOf (scale, kNumScales));
    return { kScaleIntervals[i].data(), static_cast<size_t> (kScaleSizes[i]) };
}

bool inScale (Scale scale, int key, int pc) noexcept
{
    const int offset = wrap12 (pc - key);
    for (const int8_t iv : scaleIntervals (scale))
        if (iv == offset)
            return true;
    return false;
}

int snapToScale (Scale scale, int key, int pc) noexcept
{
    // Walk out from `pc`; up wins a tie (the sketch's progression steps up on a tie).
    for (int d = 0; d <= 6; ++d)
    {
        const int up = wrap12 (pc + d);
        if (inScale (scale, key, up))
            return up;
        if (d > 0)
        {
            const int down = wrap12 (pc - d);
            if (inScale (scale, key, down))
                return down;
        }
    }
    return wrap12 (pc);
}

//==================================================================================================
ChordName nameOf (const Chord& chord) noexcept
{
    ChordName n;
    const auto* shape = findShape (chord.intervals, chord.count);
    const bool hasThird = chord.has (3) || chord.has (15);
    const bool hasMajorThird = chord.has (4) || chord.has (16);
    n.minor = shape != nullptr ? shape->minor : (hasThird && ! hasMajorThird);

    const int degree = wrap12 (chord.degree);
    const int keyLetter = kKeyLetterIndex[static_cast<size_t> (wrap12 (chord.key))];
    const int letter = (keyLetter + kDegreeLetter[static_cast<size_t> (degree)]) % 7;
    const int pc = wrap12 (chord.key + chord.degree);
    const int acc = ((pc - kLetterPc[static_cast<size_t> (letter)] + 18) % 12) - 6;

    char* p = n.label.data();
    char* end = n.label.data() + n.label.size() - 1;
    append (p, kLetterNames[static_cast<size_t> (letter)], end);
    for (int i = 0; i < acc; ++i)
        append (p, kSharp, end);
    for (int i = 0; i < -acc; ++i)
        append (p, kFlat, end);
    append (p, shape != nullptr ? shape->name : "alt", end);
    *p = '\0';

    p = n.roman.data();
    end = n.roman.data() + n.roman.size() - 1;
    append (p, kRoman[static_cast<size_t> (degree)], end);
    *p = '\0';
    if (n.minor)
        for (char* q = n.roman.data(); *q != '\0'; ++q)
            if (*q >= 'A' && *q <= 'Z')
                *q = static_cast<char> (*q - 'A' + 'a');

    return n;
}

//==================================================================================================
void Harmony::setKey (int pitchClass) noexcept
{
    keyPc.store (wrap12 (pitchClass), std::memory_order_relaxed);
    latched.store (false, std::memory_order_release);
}

void Harmony::setMood (Mood mood) noexcept
{
    moodIndex.store (indexOf (mood, kNumMoods), std::memory_order_relaxed);
    latched.store (false, std::memory_order_release);
}

void Harmony::setFunction (ChordFunction fn) noexcept
{
    functionIndex.store (indexOf (fn, kNumFunctions), std::memory_order_relaxed);
    latched.store (false, std::memory_order_release);
}

void Harmony::setChordIndex (int index) noexcept
{
    shapeIndex.store (std::clamp (index, 0, 2), std::memory_order_relaxed);
    latched.store (false, std::memory_order_release);
}

void Harmony::setTranspose (int semitones) noexcept
{
    transposeSemis.store (semitones, std::memory_order_relaxed);
}

void Harmony::setScale (Scale scale) noexcept
{
    scaleIndex.store (indexOf (scale, kNumScales), std::memory_order_relaxed);
}

int Harmony::key() const noexcept             { return keyPc.load (std::memory_order_relaxed); }
Mood Harmony::mood() const noexcept           { return static_cast<Mood> (moodIndex.load (std::memory_order_relaxed)); }
ChordFunction Harmony::function() const noexcept { return static_cast<ChordFunction> (functionIndex.load (std::memory_order_relaxed)); }
int Harmony::chordIndex() const noexcept      { return shapeIndex.load (std::memory_order_relaxed); }
int Harmony::transpose() const noexcept       { return transposeSemis.load (std::memory_order_relaxed); }
Scale Harmony::scale() const noexcept         { return static_cast<Scale> (scaleIndex.load (std::memory_order_relaxed)); }

void Harmony::noteOn (int midiNote) noexcept
{
    if (midiNote < 0 || midiNote > 127)
        return;
    const auto word = static_cast<size_t> (midiNote >> 6);
    const uint64_t bit = uint64_t (1) << (midiNote & 63);
    if ((held[word].fetch_or (bit, std::memory_order_relaxed) & bit) != 0)
        return;   // already held
    relatch (false);
}

void Harmony::noteOff (int midiNote) noexcept
{
    if (midiNote < 0 || midiNote > 127)
        return;
    const auto word = static_cast<size_t> (midiNote >> 6);
    const uint64_t bit = uint64_t (1) << (midiNote & 63);
    if ((held[word].fetch_and (~bit, std::memory_order_relaxed) & bit) == 0)
        return;   // wasn't held
    if ((held[0].load (std::memory_order_relaxed) | held[1].load (std::memory_order_relaxed)) != 0)
        relatch (true);   // all notes off: the last chord stays
}

bool Harmony::midiHeld() const noexcept
{
    return (held[0].load (std::memory_order_relaxed) | held[1].load (std::memory_order_relaxed)) != 0;
}

int Harmony::heldCount() const noexcept
{
    return std::popcount (held[0].load (std::memory_order_relaxed))
         + std::popcount (held[1].load (std::memory_order_relaxed));
}

bool Harmony::relatch (bool requireKnownShape) noexcept
{
    const uint64_t lo = held[0].load (std::memory_order_relaxed);
    const uint64_t hi = held[1].load (std::memory_order_relaxed);
    if ((lo | hi) == 0)
        return false;

    // The lowest distinct pitch classes, at most Chord::kMaxTones.
    std::array<int8_t, Chord::kMaxTones> pcs {};
    int n = 0;
    for (int note = 0; note < 128 && n < Chord::kMaxTones; ++note)
    {
        const bool on = note < 64 ? ((lo >> note) & 1) != 0 : ((hi >> (note - 64)) & 1) != 0;
        if (! on)
            continue;
        const int pc = note % 12;
        bool seen = false;
        for (int k = 0; k < n && ! seen; ++k)
            seen = pcs[static_cast<size_t> (k)] == pc;
        if (! seen)
            pcs[static_cast<size_t> (n++)] = static_cast<int8_t> (pc);
    }
    if (n == 0)
        return false;

    // The lowest held note that spells a known shape is the root (so an inversion reads right);
    // otherwise the lowest note is.
    std::array<int8_t, Chord::kMaxTones> intervals {};
    int count = 0;
    int root = pcs[0];
    for (int r = 0; r < n && count == 0; ++r)
    {
        std::array<int8_t, Chord::kMaxTones> iv {};
        for (int k = 0; k < n; ++k)
            iv[static_cast<size_t> (k)] = static_cast<int8_t> (wrap12 (pcs[static_cast<size_t> (k)] - pcs[static_cast<size_t> (r)]));
        std::sort (iv.begin(), iv.begin() + n);
        if (findShape (iv, n) != nullptr)
        {
            root = pcs[static_cast<size_t> (r)];
            intervals = iv;
            count = n;
        }
    }
    if (count == 0)
    {
        if (requireKnownShape)
            return false;
        for (int k = 0; k < n; ++k)
            intervals[static_cast<size_t> (k)] = static_cast<int8_t> (wrap12 (pcs[static_cast<size_t> (k)] - root));
        std::sort (intervals.begin(), intervals.begin() + n);
        count = n;
    }

    latchedData = {};
    latchedData.key = root;
    latchedData.degree = 0;
    latchedData.count = count;
    latchedData.intervals = intervals;
    latched.store (true, std::memory_order_release);
    return true;
}

Chord Harmony::chord() const noexcept
{
    const int transpose = transposeSemis.load (std::memory_order_relaxed);
    if (latched.load (std::memory_order_acquire))
    {
        Chord c = latchedData;
        c.key = wrap12 (c.key + transpose);
        return c;
    }
    return chordOf (mood(), function(), chordIndex(), wrap12 (key() + transpose));
}

bool Harmony::poll (Chord& out) noexcept
{
    const Chord c = chord();
    if (hasPolled && sameChord (c, lastPolled))
        return false;
    lastPolled = c;
    hasPolled = true;
    out = c;
    return true;
}

} // namespace worldtable::dsp
