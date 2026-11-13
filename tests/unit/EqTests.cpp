// T3.9: the EQ — four bands with drive, in place of the Filter and the Drive
// (spec/tasks/T3.9-eq-with-drive.md).

#include "dsp/fx/Eq.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
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
