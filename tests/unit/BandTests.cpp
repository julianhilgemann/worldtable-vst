// T3.8: the resonator bands — crossovers, tones, shift (spec/tasks/T3.8-resonator-bands.md).

#include "AllocationCounter.h"
#include "PluginProcessor.h"
#include "data/SkyDays.h"
#include "dsp/SkySound.h"
#include "dsp/Voicing.h"
#include "sky/SkyFeed.h"
#include "sky/SkyRuntime.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <vector>

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

//==================================================================================================
// SkySound: a band change re-voices the chord through the crossfade, once still for 300 ms.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kNodes = 64;
constexpr int kTick = 480;                       // samples per sky tick at 48 kHz
constexpr double kStormStart = 1715299200.0;     // 2024-05-10T00:00:00Z

/** A runtime with no track: the sky holds its quiet start and the curtain keeps moving. */
std::unique_ptr<sky::SkyRuntime> quietRuntime()
{
    auto rt = std::make_unique<sky::SkyRuntime>();
    rt->prepare (kRate);
    return rt;
}

/** The storm day at 24× from `utc`. */
std::unique_ptr<sky::SkyRuntime> stormRuntime (double utc)
{
    auto rt = std::make_unique<sky::SkyRuntime>();
    sky::SkyFeed feed (*rt);
    REQUIRE (feed.load (data::kDefaultSkyDay, 1, sky::SkyParams {}));
    rt->seek (utc);
    rt->setSpeed (24.0);
    rt->prepare (kRate);
    return rt;
}

struct Audio
{
    std::vector<float> left, right;
    int64_t allocations = 0;
};

/** Plays `samples` through SkySound in blocks of `block(i)`, cut at every `grid` samples so that
    `atGrid(sample)` (settings changes) lands on the same samples whatever the block size. */
Audio play (sky::SkyRuntime& rt, dsp::SkySound& sound, int samples, const std::function<int (int)>& block,
            const std::function<void (int)>& atGrid = {}, int grid = 4800)
{
    Audio a;
    a.left.assign (static_cast<size_t> (samples), 0.0f);
    a.right.assign (static_cast<size_t> (samples), 0.0f);
    for (int done = 0, i = 0; done < samples; ++i)
    {
        if (atGrid && done % grid == 0)
            atGrid (done);
        const int n = std::min ({ block (i), samples - done, grid - done % grid });
        const auto before = testing::allocationsOnThisThread();
        sound.process (rt, a.left.data() + done, a.right.data() + done, n);
        a.allocations += testing::allocationsOnThisThread() - before;
        done += n;
    }
    return a;
}

/** Ticks until the next chord change (a re-voice counts), at most `maxTicks`; −1 if none. */
int ticksToChange (sky::SkyRuntime& rt, dsp::SkySound& sound, int maxTicks)
{
    const auto before = sound.chordChanges();
    std::vector<float> l (kTick), r (kTick);
    for (int t = 1; t <= maxTicks; ++t)
    {
        sound.process (rt, l.data(), r.data(), kTick);
        if (sound.chordChanges() != before)
            return t;
    }
    return -1;
}

void moveBands (dsp::SkySound& sound, const std::function<void (dsp::Bands&)>& change)
{
    auto s = sound.getSettings();
    change (s.bands);
    sound.setSettings (s);
}

/** Every note layer `l` of `bank` plays lies in `range` (a band at least an octave wide). */
bool notesWithin (const dsp::SkySound& sound, int bank, Layer l, dsp::NoteRange range)
{
    for (int v = 0; v < sound.voices(); ++v)
    {
        const int n = sound.note (bank, l, v);
        if (n < range.lo || n > range.hi)
            return false;
    }
    return true;
}

std::vector<int> pitchClasses (const dsp::SkySound& sound, int bank, Layer l)
{
    std::vector<int> pcs;
    for (int v = 0; v < sound.voices(); ++v)
        pcs.push_back (sound.note (bank, l, v) % 12);
    std::sort (pcs.begin(), pcs.end());
    pcs.erase (std::unique (pcs.begin(), pcs.end()), pcs.end());
    return pcs;
}

float maxStep (const std::vector<float>& x, size_t from, size_t to)
{
    float m = 0.0f;
    for (size_t i = std::max<size_t> (from, 1); i < std::min (to, x.size()); ++i)
        m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m;
}
} // namespace

TEST_CASE ("Bands: an edge move re-voices the chord 300 ms after it, into the other bank", "[sound][bands][revoice]")
{
    auto rt = quietRuntime();
    dsp::SkySound sound;
    sound.prepare (kRate, kNodes, 1, {}, kMaj7);
    play (*rt, sound, 2 * 48000, [] (int) { return kTick; });
    REQUIRE (sound.chordChanges() == 0);
    REQUIRE_FALSE (sound.bankActive (1));
    CHECK (notesWithin (sound, 0, Layer::red, dsp::bandRange ({}, Layer::red)));
    CHECK (notesWithin (sound, 0, Layer::green, dsp::bandRange ({}, Layer::green)));
    CHECK (notesWithin (sound, 0, Layer::violet, dsp::bandRange ({}, Layer::violet)));

    // red │ green up a fourth: red grows, green shrinks
    moveBands (sound, [] (dsp::Bands& b) { b.edges.x2 = 55; });
    const int after = ticksToChange (*rt, sound, 200);
    INFO ("re-voiced " << after << " ticks after the move");
    CHECK (after >= dsp::SkySound::kRevoiceStillTicks);   // not before it has been still for 300 ms
    CHECK (after <= dsp::SkySound::kRevoiceStillTicks + 1);
    CHECK (sound.currentBank() == 1);
    CHECK (sound.bankActive (0));                          // the old voicing rings out under the crossfade
    CHECK (sound.crossfade() < 0.1);
    CHECK (sound.chord().degree == kMaj7.degree);          // the same chord, voiced again
    CHECK (sound.bands().edges.x2 == 55);
    CHECK (sound.bankBands (1).edges.x2 == 55);
    CHECK (sound.bankBands (0).edges.x2 == 50);
    auto moved = BandEdges {};
    moved.x2 = 55;
    CHECK (notesWithin (sound, 1, Layer::red, dsp::bandRange (moved, Layer::red)));
    CHECK (notesWithin (sound, 1, Layer::green, dsp::bandRange (moved, Layer::green)));
    CHECK (notesWithin (sound, 1, Layer::violet, dsp::bandRange (moved, Layer::violet)));

    // the unmoved violet band keeps every note
    int same = 0;
    for (int v = 0; v < sound.voices(); ++v)
        same += sound.note (0, Layer::violet, v) == sound.note (1, Layer::violet, v) ? 1 : 0;
    CHECK (same == sound.voices());
}

TEST_CASE ("Bands: a re-voice is heard within a second even while the old bank rings", "[sound][bands][revoice]")
{
    auto rt = quietRuntime();
    dsp::SkySound sound;
    sound.prepare (kRate, kNodes, 2, {}, kMaj7);   // the real decays: red rings 20 s
    play (*rt, sound, 48000, [] (int) { return kTick; });

    // a chord change by hand: bank 1 takes over, bank 0 rings out for 12 s + 20 s
    sound.requestChord (kBbMaj7, true);
    play (*rt, sound, 3 * 48000, [] (int) { return kTick; });
    REQUIRE (sound.chordChanges() == 1);
    REQUIRE (sound.bankActive (0));

    moveBands (sound, [] (dsp::Bands& b) { b.edges.x3 = 66; });
    const int after = ticksToChange (*rt, sound, 300);
    INFO ("re-voiced " << after << " ticks after the move");
    CHECK (after > 0);
    CHECK (after <= 100);   // within 1 s: 300 ms still, then the old bank damped over 0.5 s
    CHECK (sound.chord().degree == kBbMaj7.degree);
    CHECK (sound.bankBands (sound.currentBank()).edges.x3 == 66);
}

TEST_CASE ("Bands: a drag re-voices once, after the last move; a blend that rounds the same, never", "[sound][bands][revoice]")
{
    auto rt = quietRuntime();
    dsp::SkySound sound;
    sound.prepare (kRate, kNodes, 3, {}, kMaj7);
    play (*rt, sound, 48000, [] (int) { return kTick; });

    // a drag: a semitone every 100 ms for 2 s
    for (int step = 1; step <= 20; ++step)
    {
        moveBands (sound, [step] (dsp::Bands& b) { b.edges.x1 = 36 + step; });
        play (*rt, sound, 10 * kTick, [] (int) { return kTick; });
    }
    CHECK (sound.chordChanges() == 0);
    CHECK (sound.bands().edges.x1 == 36);
    const int after = ticksToChange (*rt, sound, 200);
    CHECK (after == dsp::SkySound::kRevoiceStillTicks - 10);   // 30 ticks after the last move
    CHECK (sound.bands().edges.x1 == 56);

    // blend 1.5 → 1.7 still reaches 2 semitones: nothing to re-voice
    play (*rt, sound, 48000, [] (int) { return kTick; });
    const auto changes = sound.chordChanges();
    moveBands (sound, [] (dsp::Bands& b) { b.edges.blend = 1.7; });
    play (*rt, sound, 48000, [] (int) { return kTick; });
    CHECK (sound.chordChanges() == changes);

    // 2.6 reaches 3: re-voiced
    moveBands (sound, [] (dsp::Bands& b) { b.edges.blend = 2.6; });
    CHECK (ticksToChange (*rt, sound, 200) > 0);
}

TEST_CASE ("Bands: tones and shift re-voice a band against the chord", "[sound][bands][revoice]")
{
    auto rt = quietRuntime();
    dsp::SkySound sound;
    sound.prepare (kRate, kNodes, 4, {}, kMaj7);
    play (*rt, sound, 48000, [] (int) { return kTick; });
    CHECK (pitchClasses (sound, 0, Layer::red) == std::vector<int> { 2, 9 });         // D A
    CHECK (pitchClasses (sound, 0, Layer::violet) == std::vector<int> { 1, 6 });      // C♯ F♯

    moveBands (sound, [] (dsp::Bands& b)
    {
        b.shift[0] = 7;                    // red a fifth up: A E
        b.tones[2] = Tones::root;          // violet rings the root only
        b.tones[1] = Tones::rootFifth;
        b.shift[1] = -12;                  // an octave is the same pitch classes
    });
    REQUIRE (ticksToChange (*rt, sound, 200) > 0);
    const int bank = sound.currentBank();
    CHECK (pitchClasses (sound, bank, Layer::red) == std::vector<int> { 4, 9 });
    CHECK (pitchClasses (sound, bank, Layer::green) == std::vector<int> { 2, 9 });
    CHECK (pitchClasses (sound, bank, Layer::violet) == std::vector<int> { 2 });
}

TEST_CASE ("Bands: a band that moves an octave keeps its voices spread across it", "[sound][bands][revoice]")
{
    auto rt = quietRuntime();
    dsp::SkySound sound;
    sound.prepare (kRate, kNodes, 5, {}, kMaj7);
    play (*rt, sound, 48000, [] (int) { return kTick; });
    const auto spread = [&sound] (int bank)
    {
        int lo = 999, hi = -999;
        for (int v = 0; v < sound.voices(); ++v)
        {
            lo = std::min (lo, sound.note (bank, Layer::green, v));
            hi = std::max (hi, sound.note (bank, Layer::green, v));
        }
        return hi - lo;
    };
    const int before = spread (0);
    moveBands (sound, [] (dsp::Bands& b) { b.edges.x2 = 62; b.edges.x3 = 85; b.edges.ceiling = 103; });
    REQUIRE (ticksToChange (*rt, sound, 200) > 0);
    CHECK (spread (1) >= before - 2);
}

TEST_CASE ("Bands: edge sweeps are block-size independent and allocation-free", "[sound][bands][determinism]")
{
    // Every edge swept up and down in steps, some still for 300 ms (re-voices, some damping a
    // ringing bank), some not; the storm, so an onset strikes on the way. The changes land on a
    // 0.1 s grid, the blocks vary.
    const auto run = [] (const std::function<int (int)>& block)
    {
        auto rt = stormRuntime (kStormStart + (17 * 60 + 38) * 60.0 + 30.0);
        dsp::SkySound sound;
        sound.prepare (kRate, kNodes, 1, {}, kMaj7);
        const auto out = play (*rt, sound, 12 * 48000, block, [&sound] (int at)
        {
            const int step = at / 4800;   // 0.1 s
            moveBands (sound, [step] (dsp::Bands& b)
            {
                const int k = step / 4;   // every 0.4 s: a move, then still long enough for a re-voice
                b.edges.floor = 24 + (k % 7) * 3;
                b.edges.x1 = 36 + ((k * 5) % 23);
                b.edges.x2 = 50 + ((k * 7) % 31) - 10;
                b.edges.x3 = 73 + ((k * 3) % 17) - 8;
                b.edges.ceiling = 91 + ((k * 11) % 15) - 4;
                b.edges.blend = 0.5 * (k % 9);
                b.shift[0] = (k % 3) * 7 - 7;
            });
        });
        CHECK (out.allocations == 0);
        CHECK (sound.chordChanges() > 5);
        CHECK (sound.resets() == 0);
        return out;
    };
    const auto reference = run ([] (int) { return 4800; });
    std::mt19937 rng (11);
    std::uniform_int_distribution<int> sizes (1, 2000);
    for (const auto& v : { run ([] (int) { return 128; }), run ([] (int) { return 37; }), run ([&] (int) { return sizes (rng); }) })
    {
        CHECK (std::memcmp (v.left.data(), reference.left.data(), reference.left.size() * sizeof (float)) == 0);
        CHECK (std::memcmp (v.right.data(), reference.right.data(), reference.right.size() * sizeof (float)) == 0);
    }
    for (const auto* ch : { &reference.left, &reference.right })
    {
        CHECK (std::all_of (ch->begin(), ch->end(), [] (float x) { return std::isfinite (x); }));
        CHECK (maxStep (*ch, 0, ch->size()) < 0.25f);   // the golden's bound for the dry bank
    }
}

//==================================================================================================
// The instrument: the parameters, the low band, the telemetry, the state.
namespace
{
void setParam (PluginProcessor& p, const char* id, float realValue)
{
    auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.getValueTreeState().getParameter (id));
    REQUIRE (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

/** Renders `seconds` in blocks of `block`, cut at a 0.1 s grid where `atGrid(sample)` may set
    parameters; returns interleaved-free left then right. */
std::vector<float> render (PluginProcessor& p, int block, double seconds, const std::function<void (int)>& atGrid = {})
{
    const int total = static_cast<int> (seconds * kRate), grid = 4800;
    std::vector<float> out (static_cast<size_t> (2 * total));
    juce::AudioBuffer<float> buffer (2, block);
    for (int done = 0; done < total;)
    {
        if (atGrid && done % grid == 0)
            atGrid (done);
        const int n = std::min ({ block, total - done, grid - done % grid });
        buffer.setSize (2, n, false, false, true);
        buffer.clear();
        juce::MidiBuffer midi;
        p.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + n, out.begin() + ch * total + done);
        done += n;
    }
    return out;
}

constexpr float kCeiling = 0.8913f * 1.0005f;   // −1 dBFS (+ rounding)

/** The five edge parameters, swept in turn: each in 6 steps from its minimum to its maximum, half
    a second apart (so every step re-voices), 3 s per edge. */
void sweepEdges (PluginProcessor& p, int at)
{
    static constexpr const char* kIds[] = { "band_floor", "band_x1", "band_x2", "band_x3", "band_ceiling" };
    static constexpr int kMin[] = { 21, 28, 33, 38, 43 }, kMax[] = { 60, 84, 96, 103, 108 }, kDefault[] = { 24, 36, 50, 73, 91 };
    const double t = at / kRate - 1.0;   // 1 s of the default first
    if (t < 0.0)
        return;
    const int edge = static_cast<int> (t / 3.0), step = static_cast<int> (std::fmod (t, 3.0) / 0.5);
    for (int e = 0; e < 5; ++e)
        setParam (p, kIds[e], static_cast<float> (e == edge ? kMin[e] + (kMax[e] - kMin[e]) * step / 5 : kDefault[e]));
}
} // namespace

TEST_CASE ("Bands: sweeping each edge while the drone plays: no clicks, finite, under the ceiling, the same at two block sizes", "[processor][bands][render]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    constexpr double kSeconds = 17.0;   // 1 s, then 3 s per edge, then 1 s

    const auto run = [] (int block, bool sweep)
    {
        PluginProcessor p (false);
        p.setPlayConfigDetails (0, 2, kRate, block);
        p.prepareToPlay (kRate, block);
        auto out = render (p, block, kSeconds, [&p, sweep] (int at) { if (sweep) sweepEdges (p, at); });
        // five edges × six steps; some steps clamp to the edges already sounding (x1 28 is 36 over
        // the floor at 24), the others re-voice
        if (sweep)
            CHECK (p.getInstrument().chordChanges() >= 12);
        return out;
    };
    const auto a = run (512, true);
    const auto b = run (333, true);
    const auto still = run (512, false);   // the same sky with the default bands

    int nonFinite = 0;
    float peak = 0.0f;
    for (const float x : a)
    {
        nonFinite += std::isfinite (x) ? 0 : 1;
        peak = std::max (peak, std::abs (x));
    }
    CHECK (nonFinite == 0);
    CHECK (peak <= kCeiling);

    double maxDiff = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        maxDiff = std::max (maxDiff, static_cast<double> (std::abs (a[i] - b[i])));
    INFO ("block 512 vs 333: max sample difference " << maxDiff);
    CHECK (maxDiff == 0.0);

    // No clicks: the largest sample step while sweeping stays near the default bands' over the
    // same sky (moving a band up raises its frequencies, so a little more is honest), and no 10 ms
    // window jumps out of its neighbourhood.
    const size_t total = a.size() / 2;
    for (size_t ch = 0; ch < 2; ++ch)
    {
        const std::vector<float> x (a.begin() + static_cast<long> (ch * total), a.begin() + static_cast<long> ((ch + 1) * total));
        const std::vector<float> y (still.begin() + static_cast<long> (ch * total), still.begin() + static_cast<long> ((ch + 1) * total));
        const float swept = maxStep (x, 0, total), reference = maxStep (y, 0, total);
        const auto outlier = [total] (const std::vector<float>& z)   // the worst 10 ms window against the median of ±0.5 s
        {
            std::vector<float> windows;
            for (size_t w = 0; w + 480 <= total; w += 480)
                windows.push_back (maxStep (z, w, w + 481));
            float worst = 0.0f;
            for (size_t w = 50; w + 50 < windows.size(); ++w)
            {
                std::vector<float> around (windows.begin() + static_cast<long> (w - 50), windows.begin() + static_cast<long> (w + 50));
                std::nth_element (around.begin(), around.begin() + 50, around.end());
                worst = std::max (worst, windows[w] / std::max (around[50], 1.0e-6f));
            }
            return worst;
        };
        const float worstRatio = outlier (x), referenceRatio = outlier (y);
        std::printf ("Band sweep, channel %d: max step %.4f (default bands %.4f), worst 10 ms window %.2fx its neighbourhood's "
                     "median (default bands %.2fx), peak %.2f dBFS, block 512 vs 333 max difference %g\n",
                     static_cast<int> (ch), static_cast<double> (swept), static_cast<double> (reference), static_cast<double> (worstRatio),
                     static_cast<double> (referenceRatio), 20.0 * std::log10 (static_cast<double> (peak)), maxDiff);
        CHECK (swept < 2.0f * reference);
        CHECK (worstRatio < std::max (3.0f, 1.25f * referenceRatio));   // a click would stand far out
    }
}

TEST_CASE ("Bands: a moved edge is heard within a second, and the chord readout carries the edges", "[processor][bands][revoice]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    PluginProcessor p (false);
    p.setPlayConfigDetails (0, 2, kRate, 512);
    p.prepareToPlay (kRate, 512);
    render (p, 512, 1.0);

    dsp::ChordState c;
    REQUIRE (p.getInstrument().readChord (c));
    CHECK (c.edges == std::array<int16_t, 5> { 24, 36, 50, 73, 91 });
    CHECK (c.blend == 1.5f);

    const auto changes = p.getInstrument().chordChanges();
    setParam (p, "band_x1", 40.0f);
    setParam (p, "band_x3", 70.0f);
    render (p, 512, 1.0);
    CHECK (p.getInstrument().chordChanges() == changes + 1);   // re-voiced, not waiting for the next chord
    CHECK (p.getInstrument().readChord (c));
    CHECK (c.edges == std::array<int16_t, 5> { 24, 40, 50, 70, 91 });
    for (int v = 0; v < c.voices; ++v)
        CHECK (c.notes[0][static_cast<size_t> (v)] >= 38);   // red now from E2 (− the blend's 2)
}

TEST_CASE ("Bands: the low resonator sits from the floor up, and follows a floor move", "[processor][bands][low]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    PluginProcessor p (false);
    setParam (p, "progression", 0.0f);   // hold D maj7
    p.setPlayConfigDetails (0, 2, kRate, 512);
    p.prepareToPlay (kRate, 512);
    render (p, 512, 0.5);
    const auto& low = p.getInstrument().sources().lowResonator();
    CHECK (low.note() == 26);   // D1, from C1 up

    setParam (p, "band_floor", 36.0f);
    render (p, 512, 0.2);
    CHECK (low.note() == 26);   // not before it has been still for 300 ms
    render (p, 512, 0.3);
    CHECK (low.note() == 38);   // D2

    setParam (p, "band_floor", 27.0f);   // D♯1: D sits at the top of the band
    render (p, 512, 1.0);
    CHECK (low.note() == 38);
    setParam (p, "band_floor", 26.0f);
    render (p, 512, 1.0);
    CHECK (low.note() == 26);
}

TEST_CASE ("Bands: a v2 state (Register, Spread, Low Octave) loads with the default bands", "[state][migration][bands]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    CHECK (PluginProcessor::kStateVersion == 3);

    PluginProcessor p (false);
    setParam (p, "band_x2", 60.0f);   // something not at its default, which the old state must reset
    juce::ValueTree old (PluginProcessor::kStateType);
    old.setProperty (PluginProcessor::kStateVersionId, 2, nullptr);
    for (const auto& [id, value] : { std::pair<const char*, float> { "register_semis", 12.0f }, { "spread", 3.0f },
                                     { "low_octave", 1.0f }, { "rev_mix", 0.6f } })
    {
        juce::ValueTree param ("PARAM");
        param.setProperty ("id", id, nullptr);
        param.setProperty ("value", value, nullptr);
        old.appendChild (param, nullptr);
    }
    juce::MemoryBlock blob;
    if (auto xml = old.createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, blob);
    p.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));

    auto& apvts = p.getValueTreeState();
    CHECK (apvts.getParameter ("register_semis") == nullptr);
    CHECK (apvts.getParameter ("spread") == nullptr);
    CHECK (apvts.getParameter ("low_octave") == nullptr);
    CHECK (std::abs (apvts.getRawParameterValue ("rev_mix")->load() - 0.6f) < 1.0e-3f);   // the rest loads
    for (const auto& [id, value] : { std::pair<const char*, float> { "band_floor", 24.0f }, { "band_x1", 36.0f }, { "band_x2", 50.0f },
                                     { "band_x3", 73.0f }, { "band_ceiling", 91.0f }, { "band_blend", 1.5f }, { "red_tones", 1.0f },
                                     { "green_tones", 2.0f }, { "violet_tones", 3.0f }, { "red_shift", 0.0f } })
    {
        INFO (id);
        CHECK (std::abs (apvts.getRawParameterValue (id)->load() - value) < 1.0e-4f);
    }

    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (xml != nullptr);
    CHECK (static_cast<int> (juce::ValueTree::fromXml (*xml).getProperty (PluginProcessor::kStateVersionId)) == 3);
}
