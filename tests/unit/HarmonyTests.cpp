#include "AllocationCounter.h"
#include "dsp/Harmony.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace worldtable;

namespace
{
juce::String textOf (const char* s) { return juce::String::fromUTF8 (s); }
juce::String textOf (const std::array<char, 24>& s) { return textOf (s.data()); }
juce::String textOf (const std::array<char, 16>& s) { return textOf (s.data()); }

juce::String labelOf (const dsp::Chord& chord) { return textOf (dsp::nameOf (chord).label); }
juce::String romanOf (const dsp::Chord& chord) { return textOf (dsp::nameOf (chord).roman); }

juce::String labelOf (dsp::Mood mood, dsp::ChordFunction fn, int index, int key)
{
    return labelOf (dsp::chordOf (mood, fn, index, key));
}

/** "0,4,7,11": the intervals, for a readable failure message. */
std::string intervalsOf (const dsp::Chord& chord)
{
    std::string s;
    for (int i = 0; i < chord.count; ++i)
        s += (i ? "," : "") + std::to_string (static_cast<int> (chord.intervals[static_cast<size_t> (i)]));
    return s;
}

bool sameChord (const dsp::Chord& a, const dsp::Chord& b)
{
    if (a.key != b.key || a.degree != b.degree || a.count != b.count)
        return false;
    for (int i = 0; i < a.count; ++i)
        if (a.intervals[static_cast<size_t> (i)] != b.intervals[static_cast<size_t> (i)])
            return false;
    return true;
}

void releaseAll (dsp::Harmony& h)
{
    for (int n = 0; n < 128; ++n)
        h.noteOff (n);
}
} // namespace

//==================================================================================================
TEST_CASE ("vocabulary is the sketch's tables", "[harmony]")
{
    using dsp::ChordFunction;
    using dsp::Mood;

    // harmony.js: bright 3/3/3/3, bittersweet 3/3/2/3, nocturnal 3/3/3/3 → 35 chords.
    const int expected[3][4] = { { 3, 3, 3, 3 }, { 3, 3, 2, 3 }, { 3, 3, 3, 3 } };
    int total = 0;
    for (int m = 0; m < 3; ++m)
        for (int f = 0; f < 4; ++f)
        {
            const auto& v = dsp::vocabulary (static_cast<Mood> (m), static_cast<ChordFunction> (f));
            REQUIRE (v.count == expected[m][f]);
            total += v.count;
        }
    REQUIRE (total == 35);

    for (int m = 0; m < 3; ++m)
        for (int f = 0; f < 4; ++f)
            for (int i = 0; i < expected[m][f]; ++i)
            {
                const auto& s = dsp::vocabularyChord (static_cast<Mood> (m), static_cast<ChordFunction> (f), i);
                INFO ("mood " << m << " function " << f << " chord " << i);
                REQUIRE (s.count >= 2);
                REQUIRE (s.count <= dsp::Chord::kMaxTones);
                REQUIRE (s.intervals[0] == 0);
                REQUIRE (s.degree >= 0);
                REQUIRE (s.degree < 12);
                REQUIRE (s.name != nullptr);
                REQUIRE (s.name[0] != '\0');
                for (int k = 1; k < s.count; ++k)
                    REQUIRE (s.intervals[static_cast<size_t> (k)] > s.intervals[static_cast<size_t> (k - 1)]);
            }
}

TEST_CASE ("chordOf clamps and wraps", "[harmony]")
{
    using dsp::ChordFunction;
    using dsp::Mood;

    REQUIRE (sameChord (dsp::chordOf (Mood::bright, ChordFunction::awe, 99, 2),
                        dsp::chordOf (Mood::bright, ChordFunction::awe, 2, 2)));
    REQUIRE (sameChord (dsp::chordOf (Mood::bright, ChordFunction::home, -3, 2),
                        dsp::chordOf (Mood::bright, ChordFunction::home, 0, 2)));

    const auto a = dsp::chordOf (Mood::bright, ChordFunction::home, 0, -1);
    const auto b = dsp::chordOf (Mood::bright, ChordFunction::home, 0, 11);
    REQUIRE (sameChord (a, b));
    REQUIRE (a.key == 11);
    REQUIRE (dsp::chordOf (Mood::bright, ChordFunction::home, 0, 14).key == 2);

    // The default start chord is the sketch's D maj7.
    const auto d = dsp::chordOf (Mood::bright, ChordFunction::home, 0, 2);
    REQUIRE (sameChord (d, dsp::Chord::dMaj7()));
    REQUIRE (intervalsOf (d) == "0,4,7,11");
}

TEST_CASE ("names and roman numerals match the sketch", "[harmony]")
{
    using dsp::ChordFunction;
    using dsp::Mood;

    const auto dmaj7 = dsp::chordOf (Mood::bright, ChordFunction::home, 0, 2);
    REQUIRE (labelOf (dmaj7) == "Dmaj7");
    REQUIRE (romanOf (dmaj7) == "I");
    REQUIRE_FALSE (dsp::nameOf (dmaj7).minor);

    // D major's ♭VI is spelled B♭, not A♯ (harmony.js's KEY_NAMES/DEGREE_LETTER spelling).
    const auto flatSix = dsp::chordOf (Mood::bright, ChordFunction::awe, 0, 2);
    REQUIRE (labelOf (flatSix) == "B♭maj7");
    REQUIRE (romanOf (flatSix) == "♭VI");
    REQUIRE (flatSix.count == 4);

    const auto minorFour = dsp::chordOf (Mood::bright, ChordFunction::borrowed, 0, 2);
    REQUIRE (labelOf (minorFour) == "Gm add9");
    REQUIRE (romanOf (minorFour) == "iv");
    REQUIRE (dsp::nameOf (minorFour).minor);

    REQUIRE (labelOf (Mood::bright, ChordFunction::home, 2, 2) == "D6/9");
    REQUIRE (labelOf (Mood::bittersweet, ChordFunction::home, 2, 2) == "Bm9");
    REQUIRE (labelOf (Mood::nocturnal, ChordFunction::home, 2, 2) == "Dm11");
    REQUIRE (labelOf (Mood::nocturnal, ChordFunction::tension, 1, 2) == "Csus2");

    // Every vocabulary chord: the derived name agrees with the sketch's stored shape name (and
    // every roman numeral is one of the sketch's twelve).
    for (int m = 0; m < 3; ++m)
        for (int f = 0; f < 4; ++f)
            for (int i = 0; i < 3; ++i)
            {
                const auto& s = dsp::vocabularyChord (static_cast<Mood> (m), static_cast<ChordFunction> (f), i);
                if (i >= dsp::vocabulary (static_cast<Mood> (m), static_cast<ChordFunction> (f)).count)
                    break;
                const auto chord = dsp::chordOf (static_cast<Mood> (m), static_cast<ChordFunction> (f), i, 2);
                INFO ("mood " << m << " function " << f << " chord " << i << " (" << intervalsOf (chord) << ")");
                REQUIRE (labelOf (chord).endsWith (textOf (s.name)));
                REQUIRE (romanOf (chord).isNotEmpty());
            }
}

TEST_CASE ("a latched chord gets a derived name", "[harmony]")
{
    dsp::Chord c;
    c.key = 0;   // C
    c.count = 3;
    c.intervals = { 0, 4, 7, 0, 0 };
    REQUIRE (labelOf (c) == "Cmaj");
    REQUIRE (romanOf (c) == "I");

    c.intervals = { 0, 3, 7, 0, 0 };
    REQUIRE (labelOf (c) == "Cm");
    REQUIRE (romanOf (c) == "i");
    REQUIRE (dsp::nameOf (c).minor);

    c.intervals = { 0, 2, 4, 7, 0 };   // pitch-class intervals, as the latch stores them
    c.count = 4;
    REQUIRE (labelOf (c) == "Cadd9");

    c.intervals = { 0, 1, 6, 0, 0 };   // not a shape we know
    REQUIRE (textOf (dsp::nameOf (c).label).startsWith ("C"));
    REQUIRE (romanOf (c).isNotEmpty());
}

//==================================================================================================
TEST_CASE ("scales", "[harmony]")
{
    using dsp::Scale;
    using dsp::scaleIntervals;

    REQUIRE (scaleIntervals (Scale::major).size() == 7);
    REQUIRE (scaleIntervals (Scale::major)[3] == 5);
    REQUIRE (scaleIntervals (Scale::lydian)[3] == 6);
    REQUIRE (scaleIntervals (Scale::minor)[2] == 3);
    REQUIRE (scaleIntervals (Scale::dorian)[5] == 9);
    REQUIRE (scaleIntervals (Scale::majorPentatonic).size() == 5);
    REQUIRE (scaleIntervals (Scale::minorPentatonic).size() == 5);
    REQUIRE (scaleIntervals (Scale::minorPentatonic)[1] == 3);

    REQUIRE (dsp::inScale (Scale::major, 0, 0));
    REQUIRE (dsp::inScale (Scale::major, 0, 11));
    REQUIRE_FALSE (dsp::inScale (Scale::major, 0, 1));
    REQUIRE (dsp::inScale (Scale::major, 2, 4));   // F♯ in D major
    REQUIRE_FALSE (dsp::inScale (Scale::major, 2, 3));
    REQUIRE (dsp::inScale (Scale::minorPentatonic, 0, 10));

    REQUIRE (dsp::snapToScale (Scale::major, 0, 1) == 2);    // 0 and 2 tie: up
    REQUIRE (dsp::snapToScale (Scale::major, 0, 6) == 7);    // 5 and 7 tie: up
    REQUIRE (dsp::snapToScale (Scale::major, 0, 11) == 11);
    REQUIRE (dsp::snapToScale (Scale::major, 2, 3) == 4);    // D major's nearest to D♯ is E
    REQUIRE (dsp::snapToScale (Scale::minor, 0, 1) == 2);
    REQUIRE (dsp::snapToScale (Scale::minorPentatonic, 0, 8) == 7);   // between 7 and 10
}

//==================================================================================================
TEST_CASE ("chord hold follows the controls", "[harmony]")
{
    using dsp::ChordFunction;
    using dsp::Mood;

    dsp::Harmony h;
    REQUIRE (h.key() == 2);
    REQUIRE (h.mood() == Mood::bright);
    REQUIRE (h.function() == ChordFunction::home);
    REQUIRE (h.transpose() == 0);

    dsp::Chord out;
    REQUIRE (h.poll (out));                       // the initial chord is a change
    REQUIRE (labelOf (out) == "Dmaj7");
    REQUIRE_FALSE (h.poll (out));                 // and only once

    h.setMood (Mood::nocturnal);
    REQUIRE (h.poll (out));
    REQUIRE (labelOf (out) == "Dm9");
    REQUIRE_FALSE (h.poll (out));

    h.setKey (0);
    REQUIRE (h.poll (out));
    REQUIRE (labelOf (out) == "Cm9");

    h.setFunction (ChordFunction::borrowed);
    REQUIRE (h.poll (out));
    REQUIRE (labelOf (out) == "Fm add9");

    h.setChordIndex (1);
    REQUIRE (h.poll (out));
    REQUIRE (labelOf (out) == "B♭add9");

    h.setChordIndex (99);                         // clamps to the function's last chord
    REQUIRE (h.chordIndex() == 2);
    REQUIRE (labelOf (h.chord()) == "E♭maj9");

    h.setMood (Mood::bright);
    h.setFunction (ChordFunction::home);
    h.setChordIndex (0);
    h.setKey (2);
    h.setTranspose (5);
    REQUIRE (labelOf (h.chord()) == "Gmaj7");
    h.setTranspose (-14);                         // wraps
    REQUIRE (labelOf (h.chord()) == "Cmaj7");
    h.setTranspose (0);

    REQUIRE (labelOf (h.chord()) == "Dmaj7");
}

TEST_CASE ("MIDI latch: held notes become the chord, the last one stays", "[harmony]")
{
    dsp::Harmony h;
    h.noteOn (60);        // C4
    h.noteOn (64);        // E4
    h.noteOn (67);        // G4
    REQUIRE (h.midiHeld());
    REQUIRE (h.heldCount() == 3);

    auto c = h.chord();
    REQUIRE (c.key == 0);
    REQUIRE (c.count == 3);
    REQUIRE (intervalsOf (c) == "0,4,7");
    REQUIRE (labelOf (c) == "Cmaj");
    REQUIRE (romanOf (c) == "I");

    dsp::Chord out;
    REQUIRE (h.poll (out));
    REQUIRE (labelOf (out) == "Cmaj");
    REQUIRE_FALSE (h.poll (out));

    // A first inversion still reads as C major: the root is the lowest note that spells a shape.
    releaseAll (h);
    REQUIRE_FALSE (h.midiHeld());
    REQUIRE (labelOf (h.chord()) == "Cmaj");      // the last chord stays
    h.noteOn (64); h.noteOn (67); h.noteOn (72);
    REQUIRE (h.chord().key == 0);
    REQUIRE (labelOf (h.chord()) == "Cmaj");

    // C E G B♭ → C7; add D → C9? no, seven notes: after releasing B♭ it is C E G D → Cadd9.
    releaseAll (h);
    h.noteOn (60); h.noteOn (64); h.noteOn (67); h.noteOn (70);
    REQUIRE (labelOf (h.chord()) == "C7");
    h.noteOn (62);
    REQUIRE (labelOf (h.chord()) == "C9");        // C E G B♭ D

    // Removing a tone re-latches; all notes off keeps the last chord.
    h.noteOff (70);
    REQUIRE (labelOf (h.chord()) == "Cadd9");     // C E G D
    releaseAll (h);
    REQUIRE (labelOf (h.chord()) == "Cadd9");

    // More than five distinct pitch classes keep the five lowest.
    releaseAll (h);
    for (const int n : { 60, 62, 64, 65, 67, 69 })   // C D E F G A
        h.noteOn (n);
    REQUIRE (h.chord().count == 5);
    REQUIRE (h.chord().key == 0);
    REQUIRE (h.heldCount() == 6);

    // A chord control clears the latch; the next note-on takes over again.
    releaseAll (h);
    h.setKey (5);                                 // F, bright home → Fmaj7
    REQUIRE (labelOf (h.chord()) == "Fmaj7");
    h.noteOn (60); h.noteOn (64); h.noteOn (67); h.noteOn (71);
    REQUIRE (labelOf (h.chord()) == "Cmaj7");

    // Transpose applies to a latched chord too.
    releaseAll (h);
    h.setTranspose (0);
    h.setKey (2);                                 // clears the latch
    h.noteOn (60); h.noteOn (64); h.noteOn (67);
    REQUIRE (labelOf (h.chord()) == "Cmaj");
    h.setTranspose (2);
    REQUIRE (labelOf (h.chord()) == "Dmaj");
    h.setTranspose (0);
    REQUIRE (labelOf (h.chord()) == "Cmaj");
}

//==================================================================================================
TEST_CASE ("Harmony doesn't allocate", "[harmony][realtime]")
{
    dsp::Harmony h;
    const auto before = testing::allocationsOnThisThread();

    dsp::Chord out;
    h.poll (out);
    for (int n = 48; n < 72; ++n)
        h.noteOn (n);
    h.chord();
    h.poll (out);
    for (int n = 48; n < 72; n += 2)
        h.noteOff (n);
    const auto name = dsp::nameOf (h.chord());
    REQUIRE (name.label[0] != '\0');

    for (int i = 0; i < 10; ++i)
    {
        h.setKey (i);
        h.setMood (static_cast<dsp::Mood> (i % 3));
        h.setFunction (static_cast<dsp::ChordFunction> (i % 4));
        h.setChordIndex (i % 4);
        h.setScale (static_cast<dsp::Scale> (i % 6));
        h.poll (out);
    }

    REQUIRE (testing::allocationsOnThisThread() == before);
}
