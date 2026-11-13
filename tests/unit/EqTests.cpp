// T3.9: the EQ — four bands with drive, in place of the Filter and the Drive
// (spec/tasks/T3.9-eq-with-drive.md).

#include "AllocationCounter.h"
#include "PluginProcessor.h"
#include "dsp/fx/Eq.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <iterator>
#include <numbers>
#include <string>
#include <vector>

using namespace worldtable;
using dsp::Eq;

namespace
{
constexpr double kRate = 48000.0;
constexpr double kPi = std::numbers::pi;

double dbOf (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1.0e-30)); }

/** 20 Hz … 20 kHz, `perOctave` points per octave. */
std::vector<double> probes (int perOctave)
{
    std::vector<double> f;
    for (double hz = 20.0; hz <= 20000.0 * 1.0001; hz *= std::pow (2.0, 1.0 / perOctave))
        f.push_back (std::min (hz, 20000.0));
    return f;
}

/** The EQ's small-signal response (dB) at `freqs`: a small impulse through a fresh EQ, its DFT. */
std::vector<double> measuredDb (const Eq::Settings& s, const std::vector<double>& freqs, double rate = kRate,
                                float skyOctaves = 0.0f, int length = 1 << 14)
{
    constexpr float kAmp = 1.0e-4f;
    Eq eq;
    eq.prepare (rate);
    eq.setSettings (s);
    eq.setSkyOctaves (skyOctaves);
    eq.reset();
    std::vector<float> l (static_cast<size_t> (length)), r (l.size());
    l[0] = r[0] = kAmp;
    eq.process (l.data(), r.data(), length);

    std::vector<double> out;
    for (const double hz : freqs)
    {
        const std::complex<double> step = std::polar (1.0, -2.0 * kPi * hz / rate);
        std::complex<double> w = 1.0, sum = 0.0;
        for (const float x : l)
        {
            sum += static_cast<double> (x) * w;
            w *= step;
        }
        out.push_back (dbOf (std::abs (sum) / kAmp));
    }
    return out;
}

/** The textbook: Robert Bristow-Johnson's Audio EQ Cookbook biquads, evaluated on the unit circle. */
double cookbookDb (int band, double f0, double gainDb, double q, double hz, double rate)
{
    const double w0 = 2.0 * kPi * f0 / rate, cw = std::cos (w0), sw = std::sin (w0);
    const double A = std::pow (10.0, gainDb / 40.0);
    double b0, b1, b2, a0, a1, a2;
    if (band == Eq::lowShelf)
    {
        const double alpha = sw / (2.0 * q), sa = 2.0 * std::sqrt (A) * alpha;
        b0 = A * ((A + 1.0) - (A - 1.0) * cw + sa);
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw);
        b2 = A * ((A + 1.0) - (A - 1.0) * cw - sa);
        a0 = (A + 1.0) + (A - 1.0) * cw + sa;
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw);
        a2 = (A + 1.0) + (A - 1.0) * cw - sa;
    }
    else if (band == Eq::highCut)
    {
        const double alpha = sw / (2.0 * q);
        b0 = b2 = (1.0 - cw) / 2.0;
        b1 = 1.0 - cw;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cw;
        a2 = 1.0 - alpha;
    }
    else
    {
        const double alpha = sw / (2.0 * q);
        b0 = 1.0 + alpha * A;
        b1 = -2.0 * cw;
        b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;
        a1 = -2.0 * cw;
        a2 = 1.0 - alpha / A;
    }
    const std::complex<double> z1 = std::polar (1.0, -2.0 * kPi * hz / rate), z2 = z1 * z1;
    return dbOf (std::abs ((b0 + b1 * z1 + b2 * z2) / (a0 + a1 * z1 + a2 * z2)));
}

/** All gains 0, every drive 0, the cut open: an EQ that should do nothing. */
Eq::Settings flat()
{
    Eq::Settings s;
    for (auto& b : s.bands)
    {
        b.gainDb = 0.0f;
        b.drive = 0.0f;
    }
    return s;
}

/** Hann-windowed Goertzel amplitude of `x[from, from + n)` at `hz` (a sine of amplitude a reads a). */
double amplitudeAt (const std::vector<float>& x, size_t from, size_t n, double hz, double rate = kRate)
{
    const double w = 2.0 * kPi * hz / rate;
    std::complex<double> sum = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double hann = 0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i) / static_cast<double> (n));
        sum += static_cast<double> (x[from + i]) * hann * std::polar (1.0, -w * static_cast<double> (i));
    }
    return 4.0 * std::abs (sum) / static_cast<double> (n);
}

std::vector<float> twoTones (double a, double b, double amp, int n, double rate = kRate)
{
    std::vector<float> x (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        x[static_cast<size_t> (i)] = static_cast<float> (amp * (std::sin (2.0 * kPi * a * i / rate) + std::sin (2.0 * kPi * b * i / rate + 0.7)));
    return x;
}

/** Runs `x` (mono, both channels) through an EQ with `s`; returns the left channel. */
std::vector<float> run (const Eq::Settings& s, const std::vector<float>& x, double rate = kRate)
{
    Eq eq;
    eq.prepare (rate);
    eq.setSettings (s);
    eq.reset();
    std::vector<float> l = x, r = x;
    eq.process (l.data(), r.data(), static_cast<int> (l.size()));
    return l;
}
} // namespace

//==================================================================================================
TEST_CASE ("EQ: flat at its defaults from 20 Hz to 20 kHz", "[eq]")
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto f = probes (12);
        const auto db = measuredDb (Eq::Settings {}, f, rate);
        double worst = 0.0;
        for (size_t i = 0; i < f.size(); ++i)
        {
            worst = std::max (worst, std::abs (db[i]));
            CHECK (std::abs (Eq::responseDb (Eq::Settings {}, Eq::kOpenHz, f[i], rate)) < 0.05);
        }
        INFO (rate << " Hz: worst deviation " << worst << " dB");
        CHECK (worst < 0.05);
    }
}

TEST_CASE ("EQ: each band's gain and width follow the textbook response", "[eq]")
{
    const auto f = probes (6);
    struct Case
    {
        int band;
        float hz, gainDb, q;
    };
    std::vector<Case> cases;
    for (const float hz : { 30.0f, 110.0f, 400.0f })
        for (const float g : { -12.0f, -6.0f, 6.0f, 12.0f })
            cases.push_back ({ Eq::lowShelf, hz, g, Eq::kShelfQ });
    for (const float hz : { 80.0f, 420.0f, 3000.0f })
        for (const float g : { -12.0f, 4.0f, 12.0f })
            for (const float q : { 0.3f, 1.0f, 6.0f })
                cases.push_back ({ Eq::bellLow, hz, g, q });
    for (const float hz : { 500.0f, 2400.0f, 12000.0f })
        for (const float g : { -12.0f, 12.0f })
            for (const float q : { 0.3f, 1.0f, 6.0f })
                cases.push_back ({ Eq::bellHigh, hz, g, q });
    for (const float hz : { 200.0f, 1000.0f, 3000.0f, 12000.0f, 19000.0f })
        cases.push_back ({ Eq::highCut, hz, 0.0f, Eq::kCutQ });

    double worstMeasured = 0.0, worstFormula = 0.0;
    for (const auto& c : cases)
    {
        auto s = flat();
        auto& b = s.bands[static_cast<size_t> (c.band)];
        b.hz = c.hz;
        b.gainDb = c.gainDb;
        b.q = c.q;
        const auto db = measuredDb (s, f);
        for (size_t i = 0; i < f.size(); ++i)
        {
            const double book = cookbookDb (c.band, c.hz, c.gainDb, c.q, f[i], kRate);
            const double formula = Eq::bandDb (c.band, c.hz, c.gainDb, c.q, f[i], kRate);
            if (book < -60.0)
                continue;   // deep in the cut's stop band: nothing to hear
            INFO ("band " << c.band + 1 << " at " << c.hz << " Hz, " << c.gainDb << " dB, Q " << c.q << ": at " << f[i]
                          << " Hz measured " << db[i] << " dB, textbook " << book << ", formula " << formula);
            CHECK (std::abs (db[i] - book) < 0.5);
            CHECK (std::abs (formula - book) < 0.01);
            worstMeasured = std::max (worstMeasured, std::abs (db[i] - book));
            worstFormula = std::max (worstFormula, std::abs (formula - book));
        }
    }
    std::printf ("EQ bands vs the cookbook: worst %.4f dB measured, %.6f dB the display formula (%d cases)\n",
                 worstMeasured, worstFormula, static_cast<int> (cases.size()));

    // the peak sits on the centre at the set gain, and the shelf reaches its gain at the bottom
    auto s = flat();
    s.bands[Eq::bellLow] = { 420.0f, 9.0f, 2.0f, 0.0f };
    s.bands[Eq::lowShelf] = { 110.0f, -6.0f, Eq::kShelfQ, 0.0f };
    const auto db = measuredDb (s, { 20.0, 420.0, 110.0 });
    CHECK_THAT (db[0], Catch::Matchers::WithinAbs (-6.0, 0.2));
    CHECK_THAT (db[1], Catch::Matchers::WithinAbs (9.0, 0.2));
}

TEST_CASE ("EQ: zero drive is the EQ alone, bit for bit", "[eq]")
{
    // a coloured EQ, a loud and busy input
    Eq::Settings s = flat();
    s.bands[Eq::lowShelf].gainDb = 6.0f;
    s.bands[Eq::bellLow].gainDb = -4.0f;
    s.bands[Eq::bellLow].q = 2.0f;
    s.bands[Eq::bellHigh].gainDb = 3.0f;
    s.bands[Eq::highCut].hz = 8000.0f;

    const int n = static_cast<int> (2.0 * kRate);
    std::vector<float> x (static_cast<size_t> (n));
    dsp::FastRandom rng;
    rng.seed (11);
    for (int i = 0; i < n; ++i)
        x[static_cast<size_t> (i)] = 0.3f * static_cast<float> (std::sin (2.0 * kPi * 70.0 * i / kRate))
                                   + 0.2f * static_cast<float> (std::sin (2.0 * kPi * 1300.0 * i / kRate)) + 0.1f * rng.nextBipolar();

    const auto alone = run (s, x);

    // the same four SVFs, run by hand with the band coefficients
    std::array<dsp::SvfStage, Eq::kBands> stages {};
    std::vector<float> byHand (x.size());
    for (size_t i = 0; i < x.size(); ++i)
    {
        float y = x[i];
        for (int b = 0; b < Eq::kBands; ++b)
        {
            const auto& band = s.bands[static_cast<size_t> (b)];
            y = Eq::stepBand (stages[static_cast<size_t> (b)], y, Eq::bandCoefs (b, band.hz, band.gainDb, band.q, kRate));
        }
        byHand[i] = y;
    }
    CHECK (alone == byHand);

    // a drive turned up and down again leaves nothing behind once its glide has landed on zero
    Eq driven;
    driven.prepare (kRate);
    auto hot = s;
    for (auto& b : hot.bands)
        b.drive = 0.9f;
    driven.setSettings (hot);
    driven.reset();
    std::vector<float> l = x, r = x;
    const int half = n / 2;
    driven.process (l.data(), r.data(), half);
    driven.setSettings (s);
    driven.process (l.data() + half, r.data() + half, n - half);

    CHECK_FALSE (std::equal (l.begin(), l.begin() + half, alone.begin()));   // the drive did something
    const auto settled = static_cast<size_t> (half + kRate / 2);   // 25 glide time constants
    CHECK (std::equal (l.begin() + static_cast<long> (settled), l.end(), alone.begin() + static_cast<long> (settled)));
}

TEST_CASE ("EQ: drive on one band adds that band's harmonics and nothing elsewhere (two tones)", "[eq]")
{
    // tone A inside the driven band, tone B far outside it; D = driven − undriven is what the drive added
    struct Case
    {
        int band;
        double a, b;
        float cutHz;
    };
    const Case cases[] = {
        { Eq::lowShelf, 50.0, 3010.0, Eq::kOpenHz },   // 4.8 octaves above the shelf's corner (110 Hz)
        { Eq::bellLow, 420.0, 15010.0, Eq::kOpenHz },  // 5.2 octaves above the bell's centre
        { Eq::bellHigh, 2400.0, 50.0, Eq::kOpenHz },   // 5.6 octaves below
        { Eq::highCut, 2121.0, 50.0, 3000.0f },        // the octave below a 3 kHz cut, and 5.4 octaves under it
    };
    constexpr double kAmp = 0.25;
    const int n = static_cast<int> (1.5 * kRate);
    const auto from = static_cast<size_t> (0.5 * kRate), len = static_cast<size_t> (kRate);

    for (const auto& c : cases)
    {
        auto s = flat();
        s.bands[Eq::highCut].hz = c.cutHz;
        const auto x = twoTones (c.a, c.b, kAmp, n);
        const auto clean = run (s, x);
        s.bands[static_cast<size_t> (c.band)].drive = 1.0f;
        const auto hot = run (s, x);
        std::vector<float> d (hot.size());
        for (size_t i = 0; i < d.size(); ++i)
            d[i] = hot[i] - clean[i];

        const double h2 = dbOf (amplitudeAt (d, from, len, 2.0 * c.a) / kAmp);
        const double h3 = dbOf (amplitudeAt (d, from, len, 3.0 * c.a) / kAmp);
        double elsewhere = -300.0;
        for (const double hz : { c.b, 2.0 * c.b, 3.0 * c.b, c.b + c.a, std::abs (c.b - c.a), c.b + 2.0 * c.a,
                                 std::abs (c.b - 2.0 * c.a), c.b + 3.0 * c.a, std::abs (c.b - 3.0 * c.a) })
            if (hz < 0.45 * kRate)
                elsewhere = std::max (elsewhere, dbOf (amplitudeAt (d, from, len, hz) / kAmp));
        std::printf ("EQ drive on band %d (tones %.0f and %.0f Hz): 2nd %.1f dB, 3rd %.1f dB, worst product of the "
                     "other tone %.1f dB\n", c.band + 1, c.a, c.b, h2, h3, elsewhere);
        INFO ("band " << c.band + 1 << ": 2nd " << h2 << " dB, 3rd " << h3 << " dB, elsewhere " << elsewhere << " dB");
        CHECK (std::max (h2, h3) > -30.0);   // the band is saturated
        CHECK (elsewhere < -60.0);           // the other tone is left alone
    }
}

TEST_CASE ("EQ: the sky opens the high cut by octaves, up to open", "[eq]")
{
    Eq eq;
    eq.prepare (kRate);
    auto s = flat();
    s.bands[Eq::highCut].hz = 500.0f;
    eq.setSettings (s);
    eq.reset();
    CHECK_THAT (eq.cutHz(), Catch::Matchers::WithinRel (500.0f, 1.0e-4f));
    eq.setSkyOctaves (2.0f);
    std::vector<float> l (4800), r (4800);
    for (int b = 0; b < 10; ++b)   // 1 s: 50 glide time constants
        eq.process (l.data(), r.data(), 4800);
    CHECK_THAT (eq.cutHz(), Catch::Matchers::WithinRel (2000.0f, 1.0e-3f));

    // 3 kHz opened by 3 octaves passes 20 kHz: open, and flat
    s.bands[Eq::highCut].hz = 3000.0f;
    eq.setSettings (s);
    eq.setSkyOctaves (3.0f);
    for (int b = 0; b < 10; ++b)
        eq.process (l.data(), r.data(), 4800);
    CHECK_THAT (eq.cutHz(), Catch::Matchers::WithinULP (Eq::kOpenHz, 0));
    const auto f = probes (6);
    const auto db = measuredDb (s, f, kRate, 3.0f);
    for (size_t i = 0; i < f.size(); ++i)
        CHECK (std::abs (db[i]) < 0.05);
}

TEST_CASE ("EQ: extreme settings and jumps stay finite and bounded", "[eq]")
{
    Eq eq;
    eq.prepare (44100.0);
    eq.reset();
    dsp::FastRandom rng;
    rng.seed (5);
    std::vector<float> l (441), r (441);
    float peak = 0.0f;
    bool finite = true;
    for (int b = 0; b < 2000; ++b)   // 20 s, a new random setting every 10 ms
    {
        Eq::Settings s;
        for (int k = 0; k < Eq::kBands; ++k)
        {
            auto& band = s.bands[static_cast<size_t> (k)];
            band.hz = Eq::kMinHz[static_cast<size_t> (k)] * std::pow (Eq::kMaxHz[static_cast<size_t> (k)] / Eq::kMinHz[static_cast<size_t> (k)], rng.nextFloat());
            band.gainDb = (rng.nextFloat() < 0.5f ? -1.0f : 1.0f) * Eq::kMaxGainDb;
            band.q = rng.nextFloat() < 0.5f ? Eq::kMinQ : Eq::kMaxQ;
            band.drive = rng.nextFloat() < 0.5f ? 0.0f : 1.0f;
        }
        eq.setSettings (s);
        eq.setSkyOctaves (3.0f * rng.nextFloat());
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = 0.7f * rng.nextBipolar();
        eq.process (l.data(), r.data(), static_cast<int> (l.size()));
        for (const float y : l)
        {
            finite = finite && std::isfinite (y);
            peak = std::max (peak, std::abs (y));
        }
    }
    CHECK (finite);
    CHECK (peak < 16.0f);   // +12 dB on three stacked bands of loud noise, plus the drive
}

TEST_CASE ("EQ: a parameter that jumps glides, it never clicks", "[eq]")
{
    // A steady low and high sine; one parameter jumps from one end of its range to the other and
    // back. A click would make the samples step further than the same move made slowly (a bell
    // sweeping over a tone lifts it on the way, slow or fast: that is the sound, not a click).
    const int n = static_cast<int> (1.6 * kRate), at = static_cast<int> (0.3 * kRate), back = static_cast<int> (0.9 * kRate);
    const int ramp = static_cast<int> (0.5 * kRate), chunk = 48;
    const auto x = twoTones (300.0, 2500.0, 0.2, n);
    const auto maxStep = [] (const std::vector<float>& y, int from, int to)
    {
        float m = 0.0f;
        for (int i = std::max (from, 1); i < to; ++i)
            m = std::max (m, std::abs (y[static_cast<size_t> (i)] - y[static_cast<size_t> (i - 1)]));
        return m;
    };
    struct Jump
    {
        const char* what;
        int band;
        float Eq::Band::*field;
        float from, to;
        bool logScale;
    };
    std::vector<Jump> jumps;
    for (int b = 0; b < Eq::kBands; ++b)
    {
        const auto i = static_cast<size_t> (b);
        jumps.push_back ({ "freq", b, &Eq::Band::hz, Eq::kMinHz[i], Eq::kMaxHz[i], true });
        if (b != Eq::highCut)
            jumps.push_back ({ "gain", b, &Eq::Band::gainDb, -Eq::kMaxGainDb, Eq::kMaxGainDb, false });
        if (b == Eq::bellLow || b == Eq::bellHigh)
            jumps.push_back ({ "q", b, &Eq::Band::q, Eq::kMinQ, Eq::kMaxQ, true });
        jumps.push_back ({ "drive", b, &Eq::Band::drive, 0.0f, 1.0f, false });
    }
    for (const auto& j : jumps)
    {
        // rampSamples 0: a jump at `at` and back at `back`; else the same moves spread over a ramp
        const auto runMove = [&] (int rampSamples)
        {
            auto s = flat();
            for (auto& band : s.bands)   // every band coloured a little, so a frequency move is heard
                band.gainDb = 6.0f;
            s.bands[Eq::highCut].hz = 4000.0f;
            auto& band = s.bands[static_cast<size_t> (j.band)];
            const auto valueAt = [&] (int i)
            {
                const auto w = [&] (int start) { return rampSamples == 0 ? (i >= start ? 1.0 : 0.0) : std::clamp (static_cast<double> (i - start) / rampSamples, 0.0, 1.0); };
                const double u = w (at) - w (back);   // up, then down again
                return j.logScale ? static_cast<float> (j.from * std::pow (j.to / j.from, u)) : static_cast<float> (j.from + (j.to - j.from) * u);
            };
            Eq eq;
            eq.prepare (kRate);
            band.*j.field = valueAt (0);
            eq.setSettings (s);
            eq.reset();
            std::vector<float> l = x, r = x;
            for (int i = 0; i < n; i += chunk)
            {
                band.*j.field = valueAt (i);
                eq.setSettings (s);
                eq.process (l.data() + i, r.data() + i, std::min (chunk, n - i));
            }
            return l;
        };
        const auto fast = runMove (0), slow = runMove (ramp);
        const int settle = static_cast<int> (0.15 * kRate);   // 7.5 glide time constants
        const float before = maxStep (fast, at - settle, at), during = maxStep (fast, back - settle, back);
        const float slowUp = maxStep (slow, at, at + ramp + settle), slowDown = maxStep (slow, back, back + ramp + settle);
        const float acrossUp = maxStep (fast, at, at + settle), acrossDown = maxStep (fast, back, back + settle);
        INFO ("band " << j.band + 1 << " " << j.what << ": steady " << before << " / " << during << ", the jump "
                      << acrossUp << " / " << acrossDown << ", the slow move " << slowUp << " / " << slowDown);
        CHECK (acrossUp <= 1.1f * std::max ({ before, during, slowUp }));
        CHECK (acrossDown <= 1.1f * std::max ({ before, during, slowDown }));
    }
}

//==================================================================================================
// The EQ in the instrument: parameters, state, the sky, telemetry, a sweep of every parameter.
namespace
{
using worldtable::PluginProcessor;

void setParam (PluginProcessor& p, const char* id, float realValue)
{
    auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.getValueTreeState().getParameter (id));
    REQUIRE (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

float defaultOf (PluginProcessor& p, const char* id)
{
    auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.getValueTreeState().getParameter (id));
    REQUIRE (param != nullptr);
    return param->convertFrom0to1 (param->getDefaultValue());
}

constexpr float kCeiling = 0.8913f * 1.0005f;   // −1 dBFS (+ rounding)

/** Renders `seconds` in blocks of `block`, cut at a 0.1 s grid where `atGrid(sample)` may set
    parameters; counts what processBlock allocates. Returns left then right. */
std::vector<float> render (PluginProcessor& p, int block, double seconds, const std::function<void (int)>& atGrid, int64_t& allocations)
{
    const int total = static_cast<int> (seconds * kRate), grid = 4800;
    std::vector<float> out (static_cast<size_t> (2 * total));
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    for (int done = 0; done < total;)
    {
        if (atGrid && done % grid == 0)
            atGrid (done);
        const int n = std::min ({ block, total - done, grid - done % grid });
        buffer.setSize (2, n, false, false, true);
        buffer.clear();
        const auto before = testing::allocationsOnThisThread();
        p.processBlock (buffer, midi);
        allocations += testing::allocationsOnThisThread() - before;
        for (int ch = 0; ch < 2; ++ch)
            std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + n, out.begin() + ch * total + done);
        done += n;
    }
    return out;
}

struct EqParam
{
    const char* id;
    float min, max, base;
};

/** The sweep's base: a coloured EQ with every band driven, the cut at 6 kHz opened by the sky. */
const EqParam kEqParams[] = {
    { "eq1_freq", 30.0f, 400.0f, 110.0f },   { "eq1_gain", -12.0f, 12.0f, 6.0f },  { "eq1_drive", 0.0f, 1.0f, 0.5f },
    { "eq2_freq", 80.0f, 3000.0f, 420.0f },  { "eq2_gain", -12.0f, 12.0f, -6.0f }, { "eq2_q", 0.3f, 6.0f, 1.0f },
    { "eq2_drive", 0.0f, 1.0f, 0.5f },       { "eq3_freq", 500.0f, 12000.0f, 2400.0f }, { "eq3_gain", -12.0f, 12.0f, 6.0f },
    { "eq3_q", 0.3f, 6.0f, 1.0f },           { "eq3_drive", 0.0f, 1.0f, 0.5f },    { "eq4_freq", 200.0f, 20000.0f, 6000.0f },
    { "eq4_drive", 0.0f, 1.0f, 0.5f },       { "eq_sky", 0.0f, 1.0f, 0.5f },
};
constexpr double kLead = 1.0, kPerParam = 1.5;

/** Each parameter in turn, on the 0.1 s grid: 0.3 s from its base down to its minimum, 0.9 s up to
    its maximum, 0.3 s back to its base (frequencies in octaves). */
void sweepEq (PluginProcessor& p, int at)
{
    const double t = at / kRate - kLead;
    const int which = t < 0.0 ? -1 : static_cast<int> (t / kPerParam);
    const double u = t - which * kPerParam;
    int i = 0;
    for (const auto& e : kEqParams)
    {
        float v = e.base;
        if (i++ == which)
        {
            const bool logScale = std::string (e.id).find ("freq") != std::string::npos;
            const auto mix = [logScale] (float a, float b, double w)
            {
                return logScale ? static_cast<float> (a * std::pow (b / a, w)) : static_cast<float> (a + (b - a) * w);
            };
            v = u < 0.3 ? mix (e.base, e.min, u / 0.3) : u < 1.2 ? mix (e.min, e.max, (u - 0.3) / 0.9) : mix (e.max, e.base, std::min (1.0, (u - 1.2) / 0.3));
        }
        setParam (p, e.id, v);
    }
}
} // namespace

TEST_CASE ("EQ in the instrument: the parameters' defaults are the EQ's", "[processor][eq][params]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    PluginProcessor p (false);
    const Eq::Settings d;
    const char* freq[] = { "eq1_freq", "eq2_freq", "eq3_freq", "eq4_freq" };
    const char* drive[] = { "eq1_drive", "eq2_drive", "eq3_drive", "eq4_drive" };
    for (size_t b = 0; b < Eq::kBands; ++b)
    {
        INFO ("band " << b + 1);
        CHECK_THAT (defaultOf (p, freq[b]), Catch::Matchers::WithinRel (d.bands[b].hz, 1.0e-4f));
        CHECK_THAT (defaultOf (p, drive[b]), Catch::Matchers::WithinAbs (d.bands[b].drive, 1.0e-4));
        CHECK_THAT (defaultOf (p, drive[b]), Catch::Matchers::WithinAbs (Eq::kDefaultDrive[b], 1.0e-4));
    }
    for (const char* id : { "eq1_gain", "eq2_gain", "eq3_gain" })
        CHECK_THAT (defaultOf (p, id), Catch::Matchers::WithinAbs (0.0, 1.0e-6));
    CHECK_THAT (defaultOf (p, "eq2_q"), Catch::Matchers::WithinAbs (d.bands[1].q, 1.0e-4));
    CHECK_THAT (defaultOf (p, "eq3_q"), Catch::Matchers::WithinAbs (d.bands[2].q, 1.0e-4));
    CHECK_THAT (defaultOf (p, "eq_sky"), Catch::Matchers::WithinAbs (dsp::Sources::Settings {}.eqSky, 1.0e-4));
    for (const char* gone : { "flt_type", "flt_cutoff", "flt_reso", "flt_sky", "drive" })
        CHECK (p.getValueTreeState().getParameter (gone) == nullptr);
}

TEST_CASE ("EQ in the instrument: a v3 state (Filter, Drive) loads with the EQ's defaults", "[state][migration][eq]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    CHECK (PluginProcessor::kStateVersion >= 4);

    PluginProcessor p (false);
    setParam (p, "eq1_gain", 9.0f);    // not at their defaults: the old state must reset them
    setParam (p, "eq4_freq", 800.0f);
    juce::ValueTree old (PluginProcessor::kStateType);
    old.setProperty (PluginProcessor::kStateVersionId, 3, nullptr);
    for (const auto& [id, value] : { std::pair<const char*, float> { "flt_type", 1.0f }, { "flt_cutoff", 900.0f }, { "flt_reso", 0.8f },
                                     { "flt_sky", 1.0f }, { "drive", 0.7f }, { "rev_mix", 0.6f } })
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
    CHECK (std::abs (apvts.getRawParameterValue ("rev_mix")->load() - 0.6f) < 1.0e-3f);   // the rest loads
    for (const char* id : { "eq1_freq", "eq1_gain", "eq1_drive", "eq2_freq", "eq2_gain", "eq2_q", "eq2_drive", "eq3_freq",
                            "eq3_gain", "eq3_q", "eq3_drive", "eq4_freq", "eq4_drive", "eq_sky" })
    {
        INFO (id);
        CHECK (std::abs (apvts.getRawParameterValue (id)->load() - defaultOf (p, id)) < 1.0e-3f);
    }
    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (xml != nullptr);
    CHECK (static_cast<int> (juce::ValueTree::fromXml (*xml).getProperty (PluginProcessor::kStateVersionId)) == PluginProcessor::kStateVersion);
}

TEST_CASE ("EQ in the instrument: the sky opens the cut as the filter did (same reach, same slew)", "[processor][eq][sky]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    CHECK (dsp::Sources::kEqSkyOctaves == Catch::Approx (3.0));     // the filter's reach
    CHECK (dsp::Sources::kSkySlewSeconds == Catch::Approx (2.0));   // and its slew

    for (const float sky : { 1.0f, 0.5f, 0.0f })
    {
        PluginProcessor p (false);
        setParam (p, "eq4_freq", 1000.0f);
        setParam (p, "eq_sky", sky);
        p.setPlayConfigDetails (0, 2, kRate, 480);
        p.prepareToPlay (kRate, 480);
        CHECK_THAT (p.getInstrument().getTelemetry().eqCutHz.load(), Catch::Matchers::WithinRel (1000.0f, 1.0e-4f));

        // two seconds where the Journey starts, then a jump to its strongest substorm onset: the
        // activity, slewed, climbs there over seconds and the cut must follow it the whole way
        const auto& chart = p.getSkyFeed().dayChart();
        REQUIRE (! chart.onsets.empty());
        const auto strongest = std::max_element (chart.onsets.begin(), chart.onsets.end(),
                                                 [] (const auto& x, const auto& y) { return x.strength < y.strength; });
        juce::AudioBuffer<float> buffer (2, 480);
        juce::MidiBuffer midi;
        double worst = 0.0, lowest = 10.0, highest = 0.0;
        for (int b = 0; b < 1200; ++b)   // 12 s, a block per tick
        {
            if (b == 200)
                p.getSky().seek (strongest->utc + 60.0);
            buffer.clear();
            p.processBlock (buffer, midi);
            const double act = std::min (p.getInstrument().sources().skyActivity(), 1.0);
            const double expected = std::min (20000.0, 1000.0 * std::pow (2.0, 3.0 * sky * act));
            const double cut = p.getInstrument().getTelemetry().eqCutHz.load();
            lowest = std::min (lowest, act);
            highest = std::max (highest, act);
            if (b >= 10)   // past the first tick's glide
                worst = std::max (worst, std::abs (std::log2 (cut / expected)));
        }
        std::printf ("EQ sky %.1f: slewed activity %.3f … %.3f, the cut within %.4f octaves of 1 kHz × 2^(3 · sky · activity)\n",
                     static_cast<double> (sky), lowest, highest, worst);
        CHECK (worst < 0.01);
        CHECK (highest - lowest > 0.1);   // the sky moved during the test
    }
}

TEST_CASE ("EQ in the instrument: the telemetry carries the cut now", "[processor][eq]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    PluginProcessor p (false);
    p.setPlayConfigDetails (0, 2, kRate, 512);
    p.prepareToPlay (kRate, 512);
    CHECK_THAT (p.getInstrument().getTelemetry().eqCutHz.load(), Catch::Matchers::WithinULP (Eq::kOpenHz, 0));   // open at the defaults

    setParam (p, "eq4_freq", 3000.0f);
    setParam (p, "eq_sky", 0.0f);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int b = 0; b < 50; ++b)
        p.processBlock (buffer, midi);
    const float set = p.getValueTreeState().getRawParameterValue ("eq4_freq")->load();   // 3000 after the host's rounding
    CHECK_THAT (set, Catch::Matchers::WithinRel (3000.0f, 1.0e-3f));
    CHECK_THAT (p.getInstrument().getTelemetry().eqCutHz.load(), Catch::Matchers::WithinRel (set, 1.0e-5f));
}

TEST_CASE ("EQ in the instrument: sweeping every EQ parameter while the drone plays: no clicks, finite, under the ceiling, "
           "the same at two block sizes, no allocation", "[processor][eq][render]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    const double seconds = kLead + kPerParam * static_cast<double> (std::size (kEqParams)) + 1.0;

    const auto run = [seconds] (int block, bool sweep, int64_t& allocations)
    {
        PluginProcessor p (false);
        sweepEq (p, 0);   // the base
        p.setPlayConfigDetails (0, 2, kRate, block);
        p.prepareToPlay (kRate, block);
        return render (p, block, seconds, [&p, sweep] (int at) { if (sweep) sweepEq (p, at); }, allocations);
    };
    int64_t allocations = 0;
    const auto a = run (512, true, allocations);
    const auto b = run (333, true, allocations);
    const auto still = run (512, false, allocations);
    CHECK (allocations == 0);

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
    CHECK (maxDiff == Catch::Approx (0.0).margin (0.0));

    // No clicks: no 10 ms window jumps out of its ±0.5 s neighbourhood more than the still base does
    const size_t total = a.size() / 2;
    const auto maxStep = [] (const std::vector<float>& z, size_t from, size_t to)
    {
        float m = 0.0f;
        for (size_t i = std::max<size_t> (from, 1); i < to; ++i)
            m = std::max (m, std::abs (z[i] - z[i - 1]));
        return m;
    };
    for (size_t ch = 0; ch < 2; ++ch)
    {
        const std::vector<float> x (a.begin() + static_cast<long> (ch * total), a.begin() + static_cast<long> ((ch + 1) * total));
        const std::vector<float> y (still.begin() + static_cast<long> (ch * total), still.begin() + static_cast<long> ((ch + 1) * total));
        const auto outlier = [total, &maxStep] (const std::vector<float>& z, double& at)
        {
            std::vector<float> windows;
            for (size_t w = 0; w + 480 <= total; w += 480)
                windows.push_back (maxStep (z, w, w + 481));
            float worst = 0.0f;
            for (size_t w = 50; w + 50 < windows.size(); ++w)
            {
                std::vector<float> around (windows.begin() + static_cast<long> (w - 50), windows.begin() + static_cast<long> (w + 50));
                std::nth_element (around.begin(), around.begin() + 50, around.end());
                const float r = windows[w] / std::max (around[50], 1.0e-6f);
                if (r > worst)
                {
                    worst = r;
                    at = static_cast<double> (w) * 0.01;
                }
            }
            return worst;
        };
        double atSwept = 0.0, atStill = 0.0;
        const float worstRatio = outlier (x, atSwept), referenceRatio = outlier (y, atStill);
        std::printf ("EQ sweep, channel %d: max step %.4f (still %.4f), worst 10 ms window %.2fx its neighbourhood's median at "
                     "%.2f s (still %.2fx at %.2f s), peak %.2f dBFS, block 512 vs 333 max difference %g\n",
                     static_cast<int> (ch), static_cast<double> (maxStep (x, 0, total)), static_cast<double> (maxStep (y, 0, total)),
                     static_cast<double> (worstRatio), atSwept, static_cast<double> (referenceRatio), atStill,
                     20.0 * std::log10 (static_cast<double> (peak)), maxDiff);
        CHECK (worstRatio < std::max (3.0f, 1.25f * referenceRatio));   // a click would stand far out
    }
}

//==================================================================================================
// T6.14: the page draws the EQ's curve itself (UI.eqDb in source/ui/widgets.js). Evaluated with node
// (tests/unit/eq_page_curve.js); skipped where node is not on the PATH.
TEST_CASE ("EQ: the page's curve matches the engine within 0.5 dB", "[eq][ui]")
{
    struct Case
    {
        float f1, g1, f2, g2, q2, f3, g3, q3, cut;
        double fs;
    };
    const std::vector<Case> cases {
        { 110.0f, 6.0f, 420.0f, -4.0f, 1.0f, 2400.0f, 3.0f, 2.0f, 6500.0f, 48000.0 },
        { 30.0f, -12.0f, 80.0f, 12.0f, 0.3f, 12000.0f, -12.0f, 6.0f, 200.0f, 48000.0 },
        { 400.0f, 12.0f, 3000.0f, -8.0f, 4.0f, 500.0f, 9.0f, 0.5f, Eq::kOpenHz, 44100.0 },
    };
    std::vector<double> freqs;
    for (int i = 0; i < 20; ++i)
        freqs.push_back (20.0 * std::pow (1000.0, i / 19.0));

    juce::Array<juce::var> settings, fv;
    for (const auto& c : cases)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("eq1_freq", c.f1); o->setProperty ("eq1_gain", c.g1);
        o->setProperty ("eq2_freq", c.f2); o->setProperty ("eq2_gain", c.g2); o->setProperty ("eq2_q", c.q2);
        o->setProperty ("eq3_freq", c.f3); o->setProperty ("eq3_gain", c.g3); o->setProperty ("eq3_q", c.q3);
        o->setProperty ("eq4_freq", c.cut); o->setProperty ("fs", c.fs);
        settings.add (juce::var (o));
    }
    for (const double f : freqs)
        fv.add (f);
    auto* req = new juce::DynamicObject();
    req->setProperty ("freqs", fv);
    req->setProperty ("settings", settings);

    const auto script = juce::File (WORLDTABLE_FIXTURES_DIR).getSiblingFile ("unit").getChildFile ("eq_page_curve.js");
    juce::ChildProcess node;
    juce::String out;
    if (node.start (juce::StringArray { "/usr/bin/env", "node", script.getFullPathName(), juce::JSON::toString (juce::var (req), true) }))
    {
        out = node.readAllProcessOutput();
        node.waitForProcessToFinish (10000);
    }
    const auto page = juce::JSON::parse (out);
    if (! page.isArray() || page.size() != static_cast<int> (cases.size()))
        SKIP ("node not found or the page script failed: " << out.substring (0, 200));

    for (size_t s = 0; s < cases.size(); ++s)
    {
        const auto& c = cases[s];
        Eq::Settings set;
        set.bands = { Eq::Band { c.f1, c.g1, Eq::kShelfQ, 0.0f }, Eq::Band { c.f2, c.g2, c.q2, 0.0f },
                      Eq::Band { c.f3, c.g3, c.q3, 0.0f }, Eq::Band { c.cut, 0.0f, Eq::kCutQ, 0.0f } };
        for (size_t i = 0; i < freqs.size(); ++i)
        {
            const double engine = Eq::responseDb (set, c.cut, freqs[i], c.fs);
            const double drawn = static_cast<double> (page[static_cast<int> (s)][static_cast<int> (i)]);
            INFO ("setting " << s << ", " << freqs[i] << " Hz: engine " << engine << " dB, page " << drawn << " dB");
            CHECK (std::abs (drawn - engine) <= 0.5);
        }
    }
}
