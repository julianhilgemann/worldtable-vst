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

/** The three resonator layers, by aurora emission colour (spec/05 §1). */
enum class Layer : int { red = 0, green = 1, violet = 2 };
inline constexpr int kNumLayers = 3;

/** Pitch-class slots per layer: every voice plays the slot `voice % kSlots`. */
inline constexpr int kSlots = 5;
using Slots = std::array<int, kSlots>;

/** Pitch classes per layer, padded to kSlots. red: root + fifth; green: the chord, padded with
    root, fifth, then the rest; violet: the tones other than root and fifth. */
Slots layerSlots (const Chord&, Layer) noexcept;

/** MIDI range [lo, hi] of a layer at `registerSemitones` and `spread` (octaves per layer at 2). */
struct NoteRange
{
    int lo, hi;
};
NoteRange layerRange (Layer, double registerSemitones, double spread) noexcept;

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
