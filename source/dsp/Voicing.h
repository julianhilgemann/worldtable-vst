#pragma once

#include <array>
#include <cstdint>

namespace worldtable::dsp
{

/** Voicing (spec/05-sound-engine.md §1, §3): prototype/sky/sound/harmony.js, the voicing section.
    12-TET only: every note is an integer MIDI number. Pure and real-time safe. */

/** A chord in a key: its root `degree` semitones above the key root, its tones `intervals` above
    the chord root (the sketch's { deg, iv }). Up to five tones, as the vocabulary has. */
struct Chord
{
    static constexpr int kMaxTones = 5;

    int key = 2;                                 ///< pitch class of the key root, 0 = C (2 = D)
    int degree = 0;                              ///< semitones above the key root
    std::array<int8_t, kMaxTones> intervals {};  ///< semitones above the chord root, may exceed 12
    int count = 0;                               ///< tones used

    /** D maj7 (the bright home chord, I maj7 in D). */
    static Chord dMaj7() noexcept { return { 2, 0, { 0, 4, 7, 11, 0 }, 4 }; }

    bool has (int interval) const noexcept;
};

/** Same key, degree, tone count and intervals (not the unused tail of the array). */
inline bool sameChord (const Chord& a, const Chord& b) noexcept
{
    if (a.key != b.key || a.degree != b.degree || a.count != b.count)
        return false;
    for (int i = 0; i < a.count; ++i)
        if (a.intervals[static_cast<size_t> (i)] != b.intervals[static_cast<size_t> (i)])
            return false;
    return true;
}

/** The three resonator layers, by aurora emission colour (spec/05 §1). */
enum class Layer : int { red = 0, green = 1, violet = 2 };
inline constexpr int kNumLayers = 3;

/** Pitch-class slots per layer: every voice plays the slot `voice % kSlots`. */
inline constexpr int kSlots = 5;
using Slots = std::array<int, kSlots>;

/** A MIDI range [lo, hi], both ends included. */
struct NoteRange
{
    int lo, hi;
};

//== Bands (T3.8, spec/tasks/T3.8-resonator-bands.md) ===============================================

/** The four resonator bands share one pitch axis, cut by three crossovers between a floor and a
    ceiling: low [floor, x1), red [x1, x2), green [x2, x3), violet [x3, ceiling). MIDI notes.
    Moving a crossover lengthens one band and shortens its neighbour. `blend` (semitones) lets the
    neighbours reach into each other. */
struct BandEdges
{
    static constexpr int kCount = 5;            ///< floor, x1, x2, x3, ceiling
    static constexpr int kMinLow = 12;          ///< the low band holds an octave: the root always has a place
    static constexpr int kMinBand = 5;          ///< red, green and violet
    static constexpr int kLowestFloor = 21;     ///< A0
    static constexpr int kHighestFloor = 60;
    static constexpr int kHighestCeiling = 108; ///< C8
    static constexpr double kMaxBlend = 6.0;

    int floor = 24;      ///< C1: the lowest note of the low band
    int x1 = 36;         ///< C2: low │ red
    int x2 = 50;         ///< D3: red │ green
    int x3 = 73;         ///< C♯5: green │ violet
    int ceiling = 91;    ///< G6: the first note above the violet band
    double blend = 1.5;  ///< semitones, 0 … kMaxBlend

    /** Edge i: 0 floor, 1–3 the crossovers, 4 the ceiling. */
    int edge (int i) const noexcept;

    bool operator== (const BandEdges& o) const noexcept
    {
        return floor == o.floor && x1 == o.x1 && x2 == o.x2 && x3 == o.x3 && ceiling == o.ceiling
            && ! (blend < o.blend) && ! (blend > o.blend);
    }
};

/** Order and minimum widths, whatever the host automates, clamped from the floor upward: the floor
    into [kLowestFloor, kHighestFloor]; each further edge at least its band's minimum width above the
    one below (kMinLow, then kMinBand); the ceiling at most kHighestCeiling; the blend into
    [0, kMaxBlend]. The parameter ranges are such that pushing an edge up never passes its maximum. */
BandEdges clampEdges (const BandEdges&) noexcept;

/** The notes layer `l` rings: its band's span [edge, next edge − 1], widened by round(blend) on each
    inner side (the sides that touch another band), never below the floor nor up to the ceiling.
    The edges as given: clamp them first. */
NoteRange bandRange (const BandEdges&, Layer) noexcept;

/** The low band's note (the low resonator): the lowest note of `pitchClass` at or above `floor`. */
int lowNote (int pitchClass, int floor) noexcept;

/** Which chord tones a band rings (`red_tones`, `green_tones`, `violet_tones`; the parameter's choice
    index). */
enum class Tones : int { root = 0, rootFifth = 1, chord = 2, colours = 3, all = 4 };
inline constexpr int kNumTones = 5;

/** A band's pitch classes, padded to kSlots, every one moved `shiftSemitones` against the chord (a
    pitch-class shift: ±12 is the same set). Root: the chord root. Root + fifth: the root, and the
    fifth when the chord has one (red before T3.8). Chord: the chord, padded with root, fifth, then
    the rest (green). Colours: the tones other than root and fifth, or fifth and root when there are
    none (violet). All: the chord, as Chord. */
Slots bandSlots (const Chord&, Tones, int shiftSemitones) noexcept;

/** Everything the band voicing reads: the edges, and per layer (red, green, violet) its tones and
    shift. The defaults are the parameters'. */
struct Bands
{
    static constexpr int kMaxShift = 12;

    BandEdges edges;
    std::array<Tones, kNumLayers> tones { Tones::rootFifth, Tones::chord, Tones::colours };
    std::array<int, kNumLayers> shift {};   ///< semitones, −kMaxShift … kMaxShift

    bool operator== (const Bands&) const noexcept = default;
};

/** The edges clamped (clampEdges), the tones and shifts into their ranges. */
Bands clampBands (const Bands&) noexcept;

/** The MIDI note of pitch class `pc` nearest to `near` (within ± 6 semitones), folded into
    [lo, hi]. A range narrower than an octave keeps lo. */
int nearestNote (int pc, double near, int lo, int hi) noexcept;

/** Reorders `next` so that slot k moves from from[k] to the result's k-th pitch class with the least
    total motion on the circle of semitones. Ties go to the first permutation in lexicographic order. */
struct Lead
{
    Slots slots;
    int cost;   ///< total semitones moved
};
Lead leadSlots (const Slots& from, const Slots& next) noexcept;

} // namespace worldtable::dsp
