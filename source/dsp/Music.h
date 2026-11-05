#pragma once

#include "params/Params.h"

#include <array>
#include <cstdint>

namespace worldtable::dsp
{

/** Pure music-theory helpers: scales, chords, Euclidean rhythms. No state, no allocation. */
namespace music
{

struct ScaleDef
{
    std::array<int, 7> semitones;
    int size;
};

inline const ScaleDef& scaleDef (int scale) noexcept
{
    static constexpr std::array<ScaleDef, params::Scale::kCount> defs { {
        { { 0, 2, 4, 5, 7, 9, 11 }, 7 },   // Major
        { { 0, 2, 3, 5, 7, 8, 10 }, 7 },   // Minor
        { { 0, 2, 3, 5, 7, 9, 10 }, 7 },   // Dorian
        { { 0, 1, 3, 5, 7, 8, 10 }, 7 },   // Phrygian
        { { 0, 2, 4, 6, 7, 9, 11 }, 7 },   // Lydian
        { { 0, 2, 4, 5, 7, 9, 10 }, 7 },   // Mixolydian
        { { 0, 2, 4, 7, 9, 0, 0 },  5 },   // Penta Major
        { { 0, 3, 5, 7, 10, 0, 0 }, 5 },   // Penta Minor
        { { 0, 2, 3, 5, 7, 8, 11 }, 7 },   // Harmonic Minor
    } };
    return defs[static_cast<size_t> (scale < 0 || scale >= params::Scale::kCount ? 0 : scale)];
}

/** Semitone offset of a scale degree (degree may exceed the scale size or be negative). */
inline int degreeToSemitone (int scale, int degree) noexcept
{
    const auto& s = scaleDef (scale);
    int octave = degree >= 0 ? degree / s.size : -((-degree + s.size - 1) / s.size);
    const int index = degree - octave * s.size;
    return octave * 12 + s.semitones[static_cast<size_t> (index)];
}

struct ChordDef
{
    std::array<int, 5> degrees;
    int size;
};

/** Chords are stacked from scale degrees, so they always fit the chosen scale. */
inline const ChordDef& chordDef (int chord) noexcept
{
    static constexpr std::array<ChordDef, params::Chord::kCount> defs { {
        { { 0, 0, 0, 0, 0 }, 1 },    // Root
        { { 0, 4, 7, 0, 0 }, 3 },    // Fifth: root, fifth, octave
        { { 0, 2, 4, 0, 0 }, 3 },    // Triad
        { { 0, 2, 4, 6, 0 }, 4 },    // Seventh
        { { 0, 2, 4, 6, 8 }, 5 },    // Ninth
        { { 0, 4, 8, 9, 0 }, 4 },    // Open: root, fifth, ninth, tenth
    } };
    return defs[static_cast<size_t> (chord < 0 || chord >= params::Chord::kCount ? 0 : chord)];
}

/** Euclidean rhythm (Bresenham form, equivalent to Bjorklund up to rotation).
    Step 0 is always a hit when pulses > 0 and rotation is 0. */
inline bool euclidHit (int step, int steps, int pulses, int rotation) noexcept
{
    if (steps <= 0 || pulses <= 0)
        return false;
    if (pulses >= steps)
        return true;

    const int i = ((step + rotation) % steps + steps) % steps;
    return (i * pulses) % steps < pulses;
}

/** Step length in quarter notes for params::EuRate. */
inline double rateToQuarterNotes (int rate) noexcept
{
    static constexpr std::array<double, params::EuRate::kCount> q {
        4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0
    };
    return q[static_cast<size_t> (rate < 0 || rate >= params::EuRate::kCount ? 4 : rate)];
}

/** Delay time in quarter notes for params::DlyTime. */
inline double delayToQuarterNotes (int t) noexcept
{
    static constexpr std::array<double, params::DlyTime::kCount> q {
        0.25, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 2.0, 4.0
    };
    return q[static_cast<size_t> (t < 0 || t >= params::DlyTime::kCount ? 5 : t)];
}

} // namespace music
} // namespace worldtable::dsp
