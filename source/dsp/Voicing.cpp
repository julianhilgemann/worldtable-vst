#include "Voicing.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

// prototype/sky/sound/harmony.js, the voicing section, line by line.

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

/** The sketch's `Math.round` for the non-negative values here. */
int roundHalfUp (double x) noexcept { return static_cast<int> (std::floor (x + 0.5)); }
} // namespace

bool Chord::has (int interval) const noexcept
{
    for (int i = 0; i < count; ++i)
        if (intervals[static_cast<size_t> (i)] == interval)
            return true;
    return false;
}

Slots layerSlots (const Chord& c, Layer layer) noexcept
{
    PitchClasses pcs;
    if (layer == Layer::red)
    {
        pcs.add (pcOf (c, 0));
        if (c.has (7))
            pcs.add (pcOf (c, 7));
        return pad (pcs);
    }

    if (layer == Layer::green)
    {
        PitchClasses tones, priority;   // pad with root, fifth, then the rest
        priority.add (pcOf (c, 0));
        priority.add (pcOf (c, 7));
        for (int i = 0; i < c.count; ++i)
        {
            tones.add (pcOf (c, c.intervals[static_cast<size_t> (i)]));
            priority.add (pcOf (c, c.intervals[static_cast<size_t> (i)]));
        }
        Slots s {};
        for (int k = 0; k < kSlots; ++k)
            s[static_cast<size_t> (k)] = k < tones.count ? tones[k] : priority[(k - tones.count) % priority.count];
        return s;
    }

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
    return pad (pcs);
}

NoteRange layerRange (Layer layer, double registerSemitones, double spread) noexcept
{
    // centre and half-width at Register 0 and Voicing spread 2
    static constexpr std::array<std::array<double, 2>, kNumLayers> kRange { { { 40.0, 9.0 }, { 62.0, 12.0 }, { 81.0, 9.0 } } };
    const auto& [centre, half] = kRange[static_cast<size_t> (layer)];
    const double h = std::max (6.0, (half * spread) / 2.0);
    return { roundHalfUp (centre + registerSemitones - h), roundHalfUp (centre + registerSemitones + h) };
}

//== Bands (T3.8): stubs, implemented in the next commit ===========================================
int BandEdges::edge (int i) const noexcept
{
    (void) i;
    return 0;
}

BandEdges clampEdges (const BandEdges& e) noexcept { return e; }

NoteRange bandRange (const BandEdges&, Layer) noexcept { return { 0, 0 }; }

int lowNote (int pitchClass, int) noexcept { return pitchClass; }

Slots bandSlots (const Chord&, Tones, int) noexcept { return {}; }

Bands clampBands (const Bands& b) noexcept { return b; }

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
