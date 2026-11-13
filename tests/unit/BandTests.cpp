// T3.8: the resonator bands — crossovers, tones, shift (spec/tasks/T3.8-resonator-bands.md).

#include "dsp/Voicing.h"

#include <catch2/catch_test_macros.hpp>

using namespace worldtable;
using dsp::BandEdges;
using dsp::Layer;
using dsp::Slots;
using dsp::Tones;

namespace
{
bool sameEdges (const BandEdges& e, int floor, int x1, int x2, int x3, int ceiling)
{
    return e.floor == floor && e.x1 == x1 && e.x2 == x2 && e.x3 == x3 && e.ceiling == ceiling;
}

BandEdges edges (int floor, int x1, int x2, int x3, int ceiling, double blend = 1.5)
{
    BandEdges e;
    e.floor = floor;
    e.x1 = x1;
    e.x2 = x2;
    e.x3 = x3;
    e.ceiling = ceiling;
    e.blend = blend;
    return e;
}

// Chords in D (key 2, degree 0) unless said otherwise.
const dsp::Chord kMaj7 { 2, 0, { 0, 4, 7, 11, 0 }, 4 };         // D F♯ A C♯
const dsp::Chord kSus2 { 2, 0, { 0, 2, 7, 0, 0 }, 3 };          // D E A
const dsp::Chord kMinorAdd9 { 2, 0, { 0, 3, 7, 14, 0 }, 4 };    // D F A E
const dsp::Chord kBbMaj7 { 2, 8, { 0, 4, 7, 11, 0 }, 4 };       // ♭VI maj7 in D: B♭ D F A
const dsp::Chord kNoFifth { 2, 0, { 0, 4, 10, 0, 0 }, 3 };      // D F♯ C (no fifth)
const dsp::Chord kFifths { 2, 0, { 0, 7, 12, 0, 0 }, 3 };       // D A D (no colours)
} // namespace

TEST_CASE ("Bands: the default edges give contiguous bands", "[sound][voicing][bands]")
{
    const BandEdges e;
    CHECK (sameEdges (e, 24, 36, 50, 73, 91));
    CHECK (e.blend == 1.5);
    for (int i = 0; i < BandEdges::kCount; ++i)
        CHECK (e.edge (i) == (std::array<int, 5> { 24, 36, 50, 73, 91 }[static_cast<size_t> (i)]));
    CHECK (sameEdges (dsp::clampEdges (e), 24, 36, 50, 73, 91));
}

TEST_CASE ("Bands: ranges at the defaults reach round(blend) into the neighbours", "[sound][voicing][bands]")
{
    const BandEdges e;   // blend 1.5 → 2 semitones
    const auto red = dsp::bandRange (e, Layer::red);
    const auto green = dsp::bandRange (e, Layer::green);
    const auto violet = dsp::bandRange (e, Layer::violet);
    CHECK (red.lo == 34);       // C2 − 2: red reaches into the low band
    CHECK (red.hi == 51);       // C♯3 + 2
    CHECK (green.lo == 48);
    CHECK (green.hi == 74);     // as before T3.8
    CHECK (violet.lo == 71);
    CHECK (violet.hi == 90);    // the ceiling is the first note above: not widened outward

    // no blend: exactly the spans
    const auto e0 = edges (24, 36, 50, 73, 91, 0.0);
    CHECK (dsp::bandRange (e0, Layer::red).lo == 36);
    CHECK (dsp::bandRange (e0, Layer::red).hi == 49);
    CHECK (dsp::bandRange (e0, Layer::green).lo == 50);
    CHECK (dsp::bandRange (e0, Layer::green).hi == 72);
    CHECK (dsp::bandRange (e0, Layer::violet).lo == 73);
    CHECK (dsp::bandRange (e0, Layer::violet).hi == 90);

    // the blend never reaches past the ceiling (violet at its minimum width, blend 6)
    const auto narrow = dsp::clampEdges (edges (24, 36, 50, 80, 85, 6.0));
    CHECK (dsp::bandRange (narrow, Layer::green).hi == 84);
    CHECK (dsp::bandRange (narrow, Layer::violet).hi == 84);
    CHECK (dsp::bandRange (narrow, Layer::violet).lo == 74);
    CHECK (dsp::bandRange (narrow, Layer::red).lo == 30);
}

TEST_CASE ("Bands: minimum widths are kept, clamped from the floor upward", "[sound][voicing][bands]")
{
    // everything squeezed onto the floor: low 12, the others 5
    CHECK (sameEdges (dsp::clampEdges (edges (24, 30, 32, 33, 34)), 24, 36, 41, 46, 51));
    CHECK (sameEdges (dsp::clampEdges (edges (40, 40, 40, 40, 40)), 40, 52, 57, 62, 67));

    // one crossover pushed onto the next: the upper one gives way, the lower stays
    CHECK (sameEdges (dsp::clampEdges (edges (24, 48, 50, 73, 91)), 24, 48, 53, 73, 91));
    CHECK (sameEdges (dsp::clampEdges (edges (24, 36, 70, 73, 91)), 24, 36, 70, 75, 91));

    // the floor and the ceiling stay in their ranges; the blend too
    CHECK (dsp::clampEdges (edges (10, 36, 50, 73, 91)).floor == 21);
    CHECK (dsp::clampEdges (edges (70, 84, 96, 103, 108)).floor == 60);
    CHECK (dsp::clampEdges (edges (24, 36, 50, 73, 120)).ceiling == 108);
    CHECK (sameEdges (dsp::clampEdges (edges (60, 84, 96, 103, 108)), 60, 84, 96, 103, 108));
    CHECK (dsp::clampEdges (edges (24, 36, 50, 73, 91, -1.0)).blend == 0.0);
    CHECK (dsp::clampEdges (edges (24, 36, 50, 73, 91, 9.0)).blend == 6.0);

    // every edge at its parameter maximum or minimum: still ordered, still within the ranges
    for (const auto& e : { edges (60, 84, 96, 103, 108), edges (21, 28, 33, 38, 43), edges (60, 28, 33, 38, 43), edges (21, 84, 33, 103, 43) })
    {
        const auto c = dsp::clampEdges (e);
        INFO (c.floor << " " << c.x1 << " " << c.x2 << " " << c.x3 << " " << c.ceiling);
        CHECK (c.x1 - c.floor >= BandEdges::kMinLow);
        CHECK (c.x2 - c.x1 >= BandEdges::kMinBand);
        CHECK (c.x3 - c.x2 >= BandEdges::kMinBand);
        CHECK (c.ceiling - c.x3 >= BandEdges::kMinBand);
        CHECK (c.floor >= 21);
        CHECK (c.ceiling <= 108);
    }
}

TEST_CASE ("Bands: the order is enforced whatever the host automates", "[sound][voicing][bands]")
{
    const auto c = dsp::clampEdges (edges (40, 30, 80, 60, 50));
    CHECK (sameEdges (c, 40, 52, 80, 85, 90));
    for (int i = 1; i < BandEdges::kCount; ++i)
        CHECK (c.edge (i) > c.edge (i - 1));
}

TEST_CASE ("Bands: tone sets for each choice", "[sound][voicing][bands]")
{
    SECTION ("maj7")
    {
        CHECK (dsp::bandSlots (kMaj7, Tones::root, 0) == Slots { 2, 2, 2, 2, 2 });
        CHECK (dsp::bandSlots (kMaj7, Tones::rootFifth, 0) == Slots { 2, 9, 2, 9, 2 });
        CHECK (dsp::bandSlots (kMaj7, Tones::chord, 0) == Slots { 2, 6, 9, 1, 2 });
        CHECK (dsp::bandSlots (kMaj7, Tones::colours, 0) == Slots { 6, 1, 6, 1, 6 });
        CHECK (dsp::bandSlots (kMaj7, Tones::all, 0) == dsp::bandSlots (kMaj7, Tones::chord, 0));
    }
    SECTION ("sus2")
    {
        CHECK (dsp::bandSlots (kSus2, Tones::root, 0) == Slots { 2, 2, 2, 2, 2 });
        CHECK (dsp::bandSlots (kSus2, Tones::rootFifth, 0) == Slots { 2, 9, 2, 9, 2 });
        CHECK (dsp::bandSlots (kSus2, Tones::chord, 0) == Slots { 2, 4, 9, 2, 9 });   // padded: root, fifth
        CHECK (dsp::bandSlots (kSus2, Tones::colours, 0) == Slots { 4, 4, 4, 4, 4 });
        CHECK (dsp::bandSlots (kSus2, Tones::all, 0) == dsp::bandSlots (kSus2, Tones::chord, 0));
    }
    SECTION ("minor add9")
    {
        CHECK (dsp::bandSlots (kMinorAdd9, Tones::root, 0) == Slots { 2, 2, 2, 2, 2 });
        CHECK (dsp::bandSlots (kMinorAdd9, Tones::rootFifth, 0) == Slots { 2, 9, 2, 9, 2 });
        CHECK (dsp::bandSlots (kMinorAdd9, Tones::chord, 0) == Slots { 2, 5, 9, 4, 2 });
        CHECK (dsp::bandSlots (kMinorAdd9, Tones::colours, 0) == Slots { 5, 4, 5, 4, 5 });
        CHECK (dsp::bandSlots (kMinorAdd9, Tones::all, 0) == dsp::bandSlots (kMinorAdd9, Tones::chord, 0));
    }
    SECTION ("the chord's own root, not the key's")
    {
        CHECK (dsp::bandSlots (kBbMaj7, Tones::root, 0) == Slots { 10, 10, 10, 10, 10 });
        CHECK (dsp::bandSlots (kBbMaj7, Tones::rootFifth, 0) == Slots { 10, 5, 10, 5, 10 });
        CHECK (dsp::bandSlots (kBbMaj7, Tones::chord, 0) == Slots { 10, 2, 5, 9, 10 });
    }
    SECTION ("fallbacks: no fifth, no colours")
    {
        CHECK (dsp::bandSlots (kNoFifth, Tones::rootFifth, 0) == Slots { 2, 2, 2, 2, 2 });
        CHECK (dsp::bandSlots (kFifths, Tones::colours, 0) == Slots { 9, 2, 9, 2, 9 });
    }
    SECTION ("the defaults voice as red, green and violet did")
    {
        for (const auto& c : { kMaj7, kSus2, kMinorAdd9, kBbMaj7, kNoFifth, kFifths })
        {
            CHECK (dsp::bandSlots (c, Tones::rootFifth, 0) == dsp::layerSlots (c, Layer::red));
            CHECK (dsp::bandSlots (c, Tones::chord, 0) == dsp::layerSlots (c, Layer::green));
            CHECK (dsp::bandSlots (c, Tones::colours, 0) == dsp::layerSlots (c, Layer::violet));
        }
    }
}

TEST_CASE ("Bands: shift moves a band against the chord", "[sound][voicing][bands]")
{
    CHECK (dsp::bandSlots (kMaj7, Tones::rootFifth, 7) == Slots { 9, 4, 9, 4, 9 });     // a fifth up: A E
    CHECK (dsp::bandSlots (kMaj7, Tones::rootFifth, -7) == Slots { 7, 2, 7, 2, 7 });    // a fifth down: G D
    CHECK (dsp::bandSlots (kMaj7, Tones::chord, 7) == Slots { 9, 1, 4, 8, 9 });
    CHECK (dsp::bandSlots (kMaj7, Tones::colours, -7) == Slots { 11, 6, 11, 6, 11 });
    for (const auto t : { Tones::root, Tones::rootFifth, Tones::chord, Tones::colours, Tones::all })
    {
        CHECK (dsp::bandSlots (kMinorAdd9, t, 12) == dsp::bandSlots (kMinorAdd9, t, 0));    // the band's range sets the octave
        CHECK (dsp::bandSlots (kMinorAdd9, t, -12) == dsp::bandSlots (kMinorAdd9, t, 0));
    }

    // clampBands keeps the shifts and tones in their ranges
    dsp::Bands b;
    b.shift = { 20, -30, 5 };
    b.tones = { static_cast<Tones> (9), Tones::all, static_cast<Tones> (-1) };
    const auto c = dsp::clampBands (b);
    CHECK (c.shift == std::array<int, 3> { 12, -12, 5 });
    CHECK (c.tones == std::array<Tones, 3> { Tones::all, Tones::all, Tones::root });
}

TEST_CASE ("Bands: the low note for every key at floors 24 and 36", "[sound][voicing][bands][low]")
{
    for (int pc = 0; pc < 12; ++pc)
    {
        INFO ("pitch class " << pc);
        CHECK (dsp::lowNote (pc, 24) == 24 + pc);   // C1 … B1, the old Sub octave
        CHECK (dsp::lowNote (pc, 36) == 36 + pc);   // C2 … B2, the old Bass octave
        const int onD = dsp::lowNote (pc, 26);       // a floor on D1: D1 … C♯2
        CHECK (onD >= 26);
        CHECK (onD < 26 + 12);
        CHECK (((onD % 12) + 12) % 12 == pc);
    }
    CHECK (dsp::lowNote (2, 26) == 26);
    CHECK (dsp::lowNote (1, 26) == 37);
    CHECK (dsp::lowNote (14, 24) == 26);   // pitch classes fold
    CHECK (dsp::lowNote (-1, 24) == 35);
}
