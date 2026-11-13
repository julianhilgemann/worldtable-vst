// T3.7: the filter between the sources and Drive, and the low resonator.

#include "dsp/DspCommon.h"
#include "dsp/LowResonator.h"
#include "dsp/fx/AutoGain.h"
#include "dsp/fx/Filter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>
#include <vector>

using namespace worldtable;

namespace
{
constexpr double kRate = 48000.0;

double dbOf (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1.0e-30)); }

double rmsOf (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    for (size_t i = from; i < to; ++i)
        s += static_cast<double> (x[i]) * x[i];
    return std::sqrt (s / static_cast<double> (std::max<size_t> (1, to - from)));
}

float maxStep (const std::vector<float>& x, size_t from, size_t to)
{
    float m = 0.0f;
    for (size_t i = std::max<size_t> (from, 1); i < to; ++i)
        m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m;
}

/** Goertzel power of `x[from, to)` at `hz`. */
double powerAt (const std::vector<float>& x, size_t from, size_t to, double hz)
{
    const double w = 2.0 * std::numbers::pi * hz / kRate;
    const double k = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        const double s0 = x[i] + k * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - k * s1 * s2;
}

/** The filter's gain (dB) for a sine at `hz`, after it settles. */
double filterGainDb (int type, float cutoff, float reso, double hz)
{
    dsp::Filter f;
    f.prepare (kRate);
    f.setSettings ({ type, cutoff, reso });
    f.reset();
    const int n = static_cast<int> (kRate);
    std::vector<float> l (static_cast<size_t> (n)), r (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        l[static_cast<size_t> (i)] = r[static_cast<size_t> (i)] = static_cast<float> (std::sin (2.0 * std::numbers::pi * hz * i / kRate));
    f.process (l.data(), r.data(), n);
    return dbOf (rmsOf (l, static_cast<size_t> (n / 2), static_cast<size_t> (n)) / std::sqrt (0.5));
}
} // namespace

TEST_CASE ("the filter's types pass and stop where they should", "[filter]")
{
    using F = dsp::Filter;
    // LP12 at 1 kHz: the passband stays, three octaves up loses ≥ 30 dB (12 dB/oct)
    CHECK_THAT (filterGainDb (F::lp12, 1000.0f, 0.0f, 150.0), Catch::Matchers::WithinAbs (0.0, 0.5));
    CHECK (filterGainDb (F::lp12, 1000.0f, 0.0f, 8000.0) < -30.0);
    // LP24 is twice as steep
    CHECK (filterGainDb (F::lp24, 1000.0f, 0.0f, 8000.0) < -60.0);
    CHECK_THAT (filterGainDb (F::lp24, 1000.0f, 0.0f, 150.0), Catch::Matchers::WithinAbs (0.0, 1.0));
    // HP mirrors it
    CHECK (filterGainDb (F::hp, 1000.0f, 0.0f, 125.0) < -30.0);
    CHECK_THAT (filterGainDb (F::hp, 1000.0f, 0.0f, 8000.0), Catch::Matchers::WithinAbs (0.0, 0.5));
    // BP: unity at the centre (normalised), both sides down
    CHECK_THAT (filterGainDb (F::bp, 1000.0f, 0.0f, 1000.0), Catch::Matchers::WithinAbs (0.0, 0.5));
    CHECK (filterGainDb (F::bp, 1000.0f, 0.0f, 100.0) < -14.0);
    CHECK (filterGainDb (F::bp, 1000.0f, 0.0f, 10000.0) < -14.0);
    // resonance lifts the cutoff, at most Q 4 (+12 dB)
    const double peak = filterGainDb (F::lp12, 1000.0f, 1.0f, 1000.0);
    CHECK (peak > 9.0);
    CHECK (peak < 12.5);
    // open (the default) is transparent where the music is
    CHECK_THAT (filterGainDb (F::lp12, 20000.0f, 0.1f, 2000.0), Catch::Matchers::WithinAbs (0.0, 0.2));
}

TEST_CASE ("the filter stays bounded at full resonance and moves without clicks", "[filter]")
{
    dsp::Filter f;
    f.prepare (kRate);
    f.setSettings ({ dsp::Filter::lp24, 200.0f, 1.0f });
    f.reset();

    // white noise, full resonance, a cutoff sweep up and down: finite and bounded
    dsp::FastRandom rng;
    rng.seed (7);
    const int block = 480;
    std::vector<float> l (block), r (block);
    float peak = 0.0f;
    bool finite = true;
    for (int b = 0; b < 1000; ++b)
    {
        for (int i = 0; i < block; ++i)
            l[static_cast<size_t> (i)] = r[static_cast<size_t> (i)] = 0.5f * rng.nextBipolar();
        const float t = static_cast<float> (b) / 1000.0f;
        f.setSettings ({ dsp::Filter::lp24, 20.0f * std::pow (1000.0f, 1.0f - std::abs (2.0f * t - 1.0f)), 1.0f });
        f.process (l.data(), r.data(), block);
        for (int i = 0; i < block; ++i)
        {
            finite = finite && std::isfinite (l[static_cast<size_t> (i)]);
            peak = std::max (peak, std::abs (l[static_cast<size_t> (i)]));
        }
    }
    CHECK (finite);
    CHECK (peak < 8.0f);

    // a steady 300 Hz sine; then the type jumps LP12 → HP and the cutoff 20 kHz → 100 Hz at once
    dsp::Filter g;
    g.prepare (kRate);
    g.setSettings ({ dsp::Filter::lp12, 20000.0f, 0.3f });
    g.reset();
    const int n = static_cast<int> (kRate);
    std::vector<float> x (static_cast<size_t> (n)), y (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        x[static_cast<size_t> (i)] = static_cast<float> (0.5 * std::sin (2.0 * std::numbers::pi * 300.0 * i / kRate));
    std::vector<float> yr = x;
    y = x;
    g.process (y.data(), yr.data(), n / 2);
    g.setSettings ({ dsp::Filter::hp, 100.0f, 0.3f });
    g.process (y.data() + n / 2, yr.data() + n / 2, n / 2);
    const float steady = maxStep (y, static_cast<size_t> (n / 4), static_cast<size_t> (n / 2));
    const float across = maxStep (y, static_cast<size_t> (n / 2), static_cast<size_t> (n / 2 + 4800));
    INFO ("steady " << steady << ", across the switch " << across);
    CHECK (across < 1.5f * steady);
}

TEST_CASE ("the sky opens the filter by octaves", "[filter]")
{
    dsp::Filter f;
    f.prepare (kRate);
    f.setSettings ({ dsp::Filter::lp12, 500.0f, 0.0f });
    f.reset();
    f.setSkyOctaves (2.0f);
    std::vector<float> l (4800), r (4800);
    for (int b = 0; b < 20; ++b)
        f.process (l.data(), r.data(), 4800);
    CHECK_THAT (f.currentHz(), Catch::Matchers::WithinRel (2000.0f, 0.01f));
    CHECK (dsp::Filter::openDirection (dsp::Filter::hp) < 0.0f);
    CHECK (dsp::Filter::openDirection (dsp::Filter::lp24) > 0.0f);
}

//==================================================================================================
namespace
{
/** Runs the low resonator on the sky's 10 ms grid for `seconds`; `atTick` may change the note. */
std::vector<float> runLow (dsp::LowResonator& low, double seconds, double excitation, double gain,
                           const std::function<void (int)>& atTick = {})
{
    const int tick = static_cast<int> (kRate / 100.0);
    const int ticks = static_cast<int> (seconds * 100.0);
    std::vector<float> l (static_cast<size_t> (ticks * tick)), r (l.size());
    for (int t = 0; t < ticks; ++t)
    {
        if (atTick)
            atTick (t);
        low.tick (excitation, gain, tick);
        low.process (l.data() + t * tick, r.data() + t * tick, tick);
    }
    return l;
}
} // namespace

TEST_CASE ("the low resonator sits on the bass note in the low band", "[low]")
{
    CHECK (dsp::lowNote (2, 24) == 26);    // D1 above the default floor (C1)
    CHECK (dsp::lowNote (0, 36) == 36);    // C2 with the floor on C2
    CHECK (dsp::lowNote (11, 24) == 35);   // B1

    dsp::LowResonator low;
    low.prepare (kRate, 1);
    low.setSettings ({ 4.0 });
    low.setFloor (24);
    low.setNote (9, 4.0);   // A1, 55 Hz
    REQUIRE (low.note() == 33);
    const auto out = runLow (low, 12.0, 0.4, 1.0);
    const auto from = static_cast<size_t> (4.0 * kRate), to = out.size();
    const double at = powerAt (out, from, to, 55.0);
    const double body = powerAt (out, from, to, 110.0);
    const double below = powerAt (out, from, to, 55.0 * std::pow (2.0, -2.0 / 12.0));
    const double above = powerAt (out, from, to, 55.0 * std::pow (2.0, 2.0 / 12.0));
    INFO ("55 Hz " << dbOf (std::sqrt (at)) << " dB, 110 Hz " << dbOf (std::sqrt (body)) << ", ±2 st "
                   << dbOf (std::sqrt (below)) << " / " << dbOf (std::sqrt (above)));
    CHECK (at > 100.0 * below);
    CHECK (at > 100.0 * above);
    CHECK (body > 10.0 * above);   // the octave up speaks too
    CHECK (body < at);
}

TEST_CASE ("the low resonator moves to a new note without a click", "[low]")
{
    dsp::LowResonator low;
    low.prepare (kRate, 3);
    low.setSettings ({ 8.0 });
    low.setNote (2, 4.0);   // D1 (the default floor, C1)
    const auto out = runLow (low, 16.0, 0.5, 1.0, [&low] (int t)
    {
        if (t == 800)
            low.setNote (7, 0.05);   // G1, a fast move (Play mode's crossfade)
    });
    const auto change = static_cast<size_t> (8.0 * kRate);
    const float steady = maxStep (out, change - static_cast<size_t> (kRate), change);
    const float across = maxStep (out, change, change + static_cast<size_t> (kRate / 2));
    INFO ("steady " << steady << ", across " << across);
    CHECK (across < 1.5f * steady + 1.0e-4f);
    CHECK (low.note() == 31);

    // the energy moved to G1 (49 Hz) and left D1 (36.7 Hz)
    const auto late = change + static_cast<size_t> (4.0 * kRate);
    CHECK (powerAt (out, late, out.size(), 49.0) > 30.0 * powerAt (out, late, out.size(), 36.71));
}

TEST_CASE ("a strike rings at once without a click, the same seed gives the same sound", "[low]")
{
    dsp::LowResonator a, b;
    for (auto* low : { &a, &b })
    {
        low->prepare (kRate, 9);
        low->setSettings ({ 3.0 });
        low->setFloor (36);   // C2
        low->setNote (0, 0.02);
    }
    const auto run = [] (dsp::LowResonator& low)
    {
        return runLow (low, 2.0, 0.0, 1.0, [&low] (int t)
        {
            if (t == 10)
                low.strike (1.0);
        });
    };
    const auto x = run (a), y = run (b);
    CHECK (x == y);
    const auto at = static_cast<size_t> (10 * 480);
    CHECK (std::abs (x[at]) < 0.005f);               // the phasor's imaginary part starts near zero
    CHECK (rmsOf (x, at, at + 4800) > 0.01);         // and rings at once
    CHECK (maxStep (x, at, at + 4800) < 0.02f);
}

TEST_CASE ("AutoGain's hold glides back to unity", "[fx][autogain]")
{
    dsp::AutoGain ag;
    ag.prepare (kRate);
    std::vector<float> l (4800, 0.001f), r (4800, 0.001f);   // very quiet: wants +12 dB
    for (int b = 0; b < 600; ++b)
    {
        std::fill (l.begin(), l.end(), 0.001f);
        std::fill (r.begin(), r.end(), 0.001f);
        ag.process (l.data(), r.data(), 4800);
    }
    REQUIRE (ag.currentGain() > 3.5f);

    dsp::AutoGain::Settings s;
    s.hold = true;
    ag.setSettings (s);
    for (int b = 0; b < 200; ++b)   // 20 s: ten hold time constants
        ag.process (l.data(), r.data(), 4800);
    CHECK_THAT (ag.currentGain(), Catch::Matchers::WithinAbs (1.0f, 1.0e-3f));
}
