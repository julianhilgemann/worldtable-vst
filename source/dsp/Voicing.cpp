#include "Voicing.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

// prototype/sky/sound/harmony.js, the voicing section, line by line; the bands (T3.8) are the
// port's own (the sketch keeps its fixed layer ranges).

namespace worldtable::dsp
{

namespace
{
/** Distinct pitch classes of key + degree + intervals, in order of first appearance (the sketch's
    `[...new Set(...)]`). */
struct PitchClasses
{
    std::array<int, 8> pc {};
    int count = 0;

    void add (int p) noexcept
    {
        for (int i = 0; i < count; ++i)
            if (pc[static_cast<size_t> (i)] == p)
                return;
        pc[static_cast<size_t> (count++)] = p;
    }

    int operator[] (int i) const noexcept { return pc[static_cast<size_t> (i)]; }
};

int pcOf (const Chord& c, int interval) noexcept { return (c.key + c.degree + interval) % 12; }

Slots pad (const PitchClasses& list) noexcept
{
    Slots s {};
    for (int k = 0; k < kSlots; ++k)
        s[static_cast<size_t> (k)] = list[k % list.count];
    return s;
}

int circ (int a, int b) noexcept
{
    const int d = std::abs (a - b) % 12;
    return std::min (d, 12 - d);
}

/** All permutations of 0 … kSlots − 1 in lexicographic order, so ties break the same way. Built at
    compile time: no static initialisation (and no guard lock) on the audio thread. */
constexpr int kPermutations = 120;
using Permutation = std::array<int8_t, kSlots>;

constexpr std::array<Permutation, kPermutations> kPerms = []
{
    std::array<Permutation, kPermutations> p {};
    Permutation q { 0, 1, 2, 3, 4 };
    size_t n = 0;
    do
        p[n++] = q;
    while (std::next_permutation (q.begin(), q.end()));
    return p;
}();
} // namespace

bool Chord::has (int interval) const noexcept
{
    for (int i = 0; i < count; ++i)
        if (intervals[static_cast<size_t> (i)] == interval)
            return true;
    return false;
}

//== Bands (T3.8) ==================================================================================
int BandEdges::edge (int i) const noexcept
{
    switch (i)
    {
        case 0:  return floor;
        case 1:  return x1;
        case 2:  return x2;
        case 3:  return x3;
        default: return ceiling;
    }
}

BandEdges clampEdges (const BandEdges& e) noexcept
{
    // Each edge's parameter maximum (parameters.yaml). Pushed up from the floor, an edge can never
    // pass it: 60 + 12 ≤ 84, 84 + 5 ≤ 96, 96 + 5 ≤ 101 ≤ 103, 103 + 5 ≤ 108.
    static constexpr std::array<int, BandEdges::kCount> kMax { BandEdges::kHighestFloor, 84, 96, 103, BandEdges::kHighestCeiling };
    static constexpr std::array<int, BandEdges::kCount> kMinWidth { 0, BandEdges::kMinLow, BandEdges::kMinBand, BandEdges::kMinBand, BandEdges::kMinBand };

    std::array<int, BandEdges::kCount> x { e.floor, e.x1, e.x2, e.x3, e.ceiling };
    x[0] = std::clamp (x[0], BandEdges::kLowestFloor, kMax[0]);
    for (size_t i = 1; i < x.size(); ++i)
        x[i] = std::clamp (x[i], x[i - 1] + kMinWidth[i], std::max (kMax[i], x[i - 1] + kMinWidth[i]));

    BandEdges c;
    c.floor = x[0];
    c.x1 = x[1];
    c.x2 = x[2];
    c.x3 = x[3];
    c.ceiling = x[4];
    c.blend = std::isfinite (e.blend) ? std::clamp (e.blend, 0.0, BandEdges::kMaxBlend) : BandEdges {}.blend;
    return c;
}

NoteRange bandRange (const BandEdges& e, Layer layer) noexcept
{
    const auto band = static_cast<int> (layer) + 1;   // band 0 is the low one
    const int reach = static_cast<int> (std::lround (std::max (0.0, e.blend)));
    const int lo = e.edge (band) - reach;              // every layer's lower side touches a band
    const int hi = e.edge (band + 1) - 1 + (layer == Layer::violet ? 0 : reach);
    return { std::max (e.floor, lo), std::min (e.ceiling - 1, hi) };
}

int lowNote (int pitchClass, int floor) noexcept
{
    return floor + (((pitchClass - floor) % 12) + 12) % 12;
}

Slots bandSlots (const Chord& c, Tones tones, int shiftSemitones) noexcept
{
    PitchClasses pcs;
    Slots s {};
    switch (tones)
    {
        case Tones::root:
            pcs.add (pcOf (c, 0));
            s = pad (pcs);
            break;

        case Tones::rootFifth:   // red before T3.8
            pcs.add (pcOf (c, 0));
            if (c.has (7))
                pcs.add (pcOf (c, 7));
            s = pad (pcs);
            break;

        case Tones::colours:     // violet before T3.8
            for (int i = 0; i < c.count; ++i)
            {
                const int iv = c.intervals[static_cast<size_t> (i)];
                if (iv % 12 != 0 && iv % 12 != 7)
                    pcs.add (pcOf (c, iv));
            }
            if (pcs.count == 0)
            {
                pcs.add (pcOf (c, 7));
                pcs.add (pcOf (c, 0));
            }
            s = pad (pcs);
            break;

        case Tones::chord:       // green before T3.8
        case Tones::all:
        default:
        {
            PitchClasses priority;   // pad with root, fifth, then the rest
            priority.add (pcOf (c, 0));
            priority.add (pcOf (c, 7));
            for (int i = 0; i < c.count; ++i)
            {
                pcs.add (pcOf (c, c.intervals[static_cast<size_t> (i)]));
                priority.add (pcOf (c, c.intervals[static_cast<size_t> (i)]));
            }
            for (int k = 0; k < kSlots; ++k)
                s[static_cast<size_t> (k)] = k < pcs.count ? pcs[k] : priority[(k - pcs.count) % priority.count];
            break;
        }
    }

    for (auto& pc : s)
        pc = (((pc + shiftSemitones) % 12) + 12) % 12;
    return s;
}

Bands clampBands (const Bands& b) noexcept
{
    Bands c;
    c.edges = clampEdges (b.edges);
    for (size_t l = 0; l < kNumLayers; ++l)
    {
        c.tones[l] = static_cast<Tones> (std::clamp (static_cast<int> (b.tones[l]), 0, kNumTones - 1));
        c.shift[l] = std::clamp (b.shift[l], -Bands::kMaxShift, Bands::kMaxShift);
    }
    return c;
}

//==================================================================================================
int nearestNote (int pc, double near, int lo, int hi) noexcept
{
    // within ± 6 semitones of near; JavaScript's % is fmod. The sum is exactly an integer.
    double n = near + std::fmod (std::fmod (pc - near, 12.0) + 18.0, 12.0) - 6.0;
    while (n < lo)
        n += 12.0;
    while (n > hi)
        n -= 12.0;
    if (n < lo)
        n += 12.0;   // a range narrower than an octave keeps lo
    return static_cast<int> (std::lround (n));
}

Lead leadSlots (const Slots& from, const Slots& next) noexcept
{
    size_t best = 0;
    int bestCost = 1 << 30;
    for (size_t i = 0; i < kPerms.size(); ++i)
    {
        int cost = 0;
        for (size_t k = 0; k < kSlots; ++k)
            cost += circ (from[k], next[static_cast<size_t> (kPerms[i][k])]);
        if (cost < bestCost)
        {
            bestCost = cost;
            best = i;
        }
    }
    Lead lead { {}, bestCost };
    for (size_t k = 0; k < kSlots; ++k)
        lead.slots[k] = next[static_cast<size_t> (kPerms[best][k])];
    return lead;
}

} // namespace worldtable::dsp
