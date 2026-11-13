#pragma once

#include <array>
#include <cstdint>

namespace worldtable::dsp::mod
{

/** A slow LFO, a source of the modulation grid (T4.2: LFO 1 and LFO 2).

    0.01 … 2 Hz. Four shapes, as the mockup draws them (spec/mockups/ui-v2.html `lfoVal`), with p the
    position in the cycle (0 … 1) and k_i the seeded knots (knot()):
      sine            sin(2π·p)                          0 at p = 0, 1 at p = ¼
      triangle        1 − 4·|p − ½|                      −1 at p = 0, 1 at p = ½
      smooth random   k_i + (k_{i+1} − k_i)·s(p), s(p) = p²(3 − 2p)   passes k_i at each cycle start
      stepped random  k_i through cycle i; each step glides from k_{i−1} over kStepGlideSeconds
                      (at most a quarter of the cycle), so a step is never a click
    The output is −1 … 1.

    The phase is double precision: at 0.01 Hz it doesn't drift over days. A rate change keeps the
    phase, so the output never jumps. A shape change crossfades from the value heard at the change to
    the new shape over kShapeFadeSeconds (a change during a fade starts from where the fade was). setShape() with the shape it already has does nothing, and so does
    setRate() with its rate: the engine sets both every block.

    Seeded (rule 5): the knots depend only on (seed, stream, cycle index). LFO 1 and LFO 2 are two
    streams of the instrument's seed. The LFO advances only in tick(), once per sky tick, so its
    output is the same at any block size.

    prepare() on the message thread. Everything else is real-time safe: no allocation, no locks,
    bounded time. */
class SlowLfo
{
public:
    enum class Shape : uint8_t { sine, triangle, smoothRandom, steppedRandom };   ///< lfoN_shape
    static constexpr int kNumShapes = 4;

    static constexpr double kTickSeconds = 0.01;          ///< the sky's tick
    static constexpr double kMinHz = 0.01, kMaxHz = 2.0;  ///< lfoN_rate's range
    static constexpr double kShapeFadeSeconds = 0.25;     ///< a shape change crossfades
    static constexpr double kStepGlideSeconds = 0.05;     ///< stepped random's step (≤ ¼ cycle)
    static constexpr int kViewKnots = 6;                  ///< knots k_{i−2} … k_{i+3} in View

    /** What the LFO panel draws: the wave scrolling under a fixed "now" marker, what is still to
        come drawn dimmer. For the random shapes the page draws from `knots` (kViewKnots of them,
        starting at cycle − 2). */
    struct View
    {
        double phase = 0.0;        ///< 0 … 1, the position in the cycle now
        uint64_t cycle = 0;        ///< whole cycles since prepare()
        float rateHz = 0.0f;
        float value = 0.0f;
        Shape shape = Shape::sine;
        std::array<float, kViewKnots> knots {};
    };

    /** Phase 0, cycle 0, no fade. `stream` tells LFO 1 and LFO 2 apart (0 and 1). */
    void prepare (uint32_t seed, uint32_t stream) noexcept;

    /** Clamped to kMinHz … kMaxHz. The phase carries on from where it is. */
    void setRate (double hz) noexcept;

    /** Crossfades from the shape heard now to `shape` over kShapeFadeSeconds. `fade` = false
        switches at once: use it before playback (prepare, a session load). */
    void setShape (Shape shape, bool fade = true) noexcept;

    /** One sky tick: advances the phase by rate · kTickSeconds and returns value(). */
    float tick() noexcept;

    float value() const noexcept     { return out; }        ///< −1 … 1, as of the last tick
    double phase() const noexcept    { return position; }   ///< 0 … 1: the cycle position
    uint64_t cycle() const noexcept  { return cycles; }
    double rate() const noexcept     { return hz; }
    Shape shape() const noexcept     { return current; }    ///< the shape faded to (or heard)
    View view() const noexcept;

    /** The shape alone, without a crossfade: what the panel draws and what tick() returns when no
        fade is running. `rateHz` sets the length of stepped random's glide. */
    static float shapeAt (Shape, double rateHz, uint32_t seed, uint32_t stream, uint64_t cycle, double phase) noexcept;

    /** The random shapes' knot for cycle `index`: −1 … 1, seeded (sky::rng). */
    static float knot (uint32_t seed, uint32_t stream, uint64_t index) noexcept;

private:
    uint32_t seed = 1, stream = 0;
    double hz = 0.1;
    double position = 0.0;     ///< 0 … 1
    uint64_t cycles = 0;
    Shape current = Shape::sine;
    double fade = 1.0;         ///< 0 … 1 from `fadeFrom` to `current`
    float fadeFrom = 0.0f;     ///< the value heard when the shape changed
    float out = 0.0f;
};

} // namespace worldtable::dsp::mod
