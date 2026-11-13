#include "dsp/DspCommon.h"
#include "dsp/fx/AutoGain.h"
#include "dsp/fx/Chorus.h"
#include "dsp/fx/Phaser.h"
#include "dsp/fx/ShimmerReverb.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace worldtable;

namespace
{
double dbOf (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1.0e-30)); }
} // namespace

TEST_CASE ("AutoGain settles to its target within its range", "[fx][autogain]")
{
    dsp::AutoGain ag;
    ag.prepare (48000.0);

    // A sine of amplitude 0.0707 is −23 dBFS RMS (needs +5 dB); 0.354 is −9 dBFS RMS (needs −9).
    for (const float amplitude : { 0.0707f, 0.354f })
    {
        ag.reset();
        const int total = 70 * 48000;
        constexpr int block = 4096;
        std::vector<float> left (block), right (block);
        double sum = 0.0;
        int count = 0;
        for (int at = 0; at < total;)
        {
            const int n = std::min (block, total - at);
            for (int i = 0; i < n; ++i)
            {
                const float s = amplitude * std::sin (dsp::kTwoPi * 440.0f
                                                     * static_cast<float> (at + i) / 48000.0f);
                left[static_cast<size_t> (i)] = s;
                right[static_cast<size_t> (i)] = s;
            }
            ag.process (left.data(), right.data(), n);
            if (at > total - 5 * 48000)
                for (int i = 0; i < n; ++i)
                {
                    sum += static_cast<double> (left[static_cast<size_t> (i)]) * left[static_cast<size_t> (i)];
                    ++count;
                }
            at += n;
        }
        const double rms = std::sqrt (sum / std::max (count, 1));
        INFO ("amplitude " << amplitude << " gain " << ag.currentGain());
        CHECK_THAT (dbOf (rms), Catch::Matchers::WithinAbs (-18.0, 1.0));
        CHECK (ag.currentGain() <= dsp::dbToGain (12.0) + 1.0e-3f);
        CHECK (ag.currentGain() >= dsp::dbToGain (-12.0) - 1.0e-3f);
    }
}

TEST_CASE ("the DC blocker removes a constant and passes a sine", "[fx][dc]")
{
    dsp::DcBlocker dc;
    dc.prepare (48000.0);
    float last = 0.0f;
    for (int i = 0; i < 48000; ++i)
        last = dc.process (1.0f);
    CHECK (std::abs (last) < 1.0e-3f);

    dsp::DcBlocker pass;
    pass.prepare (48000.0);
    double in = 0.0, out = 0.0;
    for (int i = 0; i < 2 * 48000; ++i)
    {
        const float x = 0.5f * std::sin (dsp::kTwoPi * 1000.0f * static_cast<float> (i) / 48000.0f);
        const float y = pass.process (x);
        if (i >= 48000)
        {
            in += static_cast<double> (x) * x;
            out += static_cast<double> (y) * y;
        }
    }
    const double lossDb = 10.0 * std::log10 (out / std::max (in, 1.0e-30));
    CHECK_THAT (lossDb, Catch::Matchers::WithinAbs (0.0, 0.1));
}

//==================================================================================================
TEST_CASE ("phaser and chorus stay clean and stereo", "[fx][modulation]")
{
    const int n = 4 * 48000;
    std::vector<float> dry (static_cast<size_t> (n));
    for (int i = 0; i < n; ++i)
        dry[static_cast<size_t> (i)] = 0.2f * std::sin (dsp::kTwoPi * 1000.0f * static_cast<float> (i) / 48000.0f);

    const auto rms = [] (const std::vector<float>& x)
    {
        double s = 0.0;
        for (const float v : x)
            s += static_cast<double> (v) * v;
        return std::sqrt (s / std::max<size_t> (x.size(), 1));
    };

    {
        dsp::Phaser phaser;
        phaser.prepare (48000.0);
        dsp::Phaser::Settings s;
        s.mix = 0.5f;
        phaser.setSettings (s);
        auto l = dry, r = dry;
        phaser.process (l.data(), r.data(), n);
        const double level = rms (l) / rms (dry);
        double diff = 0.0;
        for (int i = 0; i < n; ++i)
            diff = std::max (diff, static_cast<double> (std::abs (l[static_cast<size_t> (i)] - r[static_cast<size_t> (i)])));
        INFO ("phaser level " << level << ", stereo difference " << diff);
        CHECK (level > 0.2);
        CHECK (level < 2.0);
        CHECK (diff > 1.0e-4);
        CHECK (std::all_of (l.begin(), l.end(), [] (float x) { return std::isfinite (x); }));

        s.mix = 0.0f;
        phaser.setSettings (s);
        auto bl = dry, br = dry;
        phaser.process (bl.data(), br.data(), n);
        CHECK (bl == dry);
        CHECK (br == dry);
    }

    {
        dsp::Chorus chorus;
        chorus.prepare (48000.0);
        dsp::Chorus::Settings s;
        s.mix = 0.4f;
        chorus.setSettings (s);
        auto l = dry, r = dry;
        chorus.process (l.data(), r.data(), n);
        const double level = rms (l) / rms (dry);
        double diff = 0.0;
        for (int i = 0; i < n; ++i)
            diff = std::max (diff, static_cast<double> (std::abs (l[static_cast<size_t> (i)] - r[static_cast<size_t> (i)])));
        INFO ("chorus level " << level << ", stereo difference " << diff);
        CHECK (level > 0.2);
        CHECK (level < 2.0);
        CHECK (diff > 1.0e-4);
        CHECK (std::all_of (l.begin(), l.end(), [] (float x) { return std::isfinite (x); }));

        s.mix = 0.0f;
        chorus.setSettings (s);
        auto bl = dry, br = dry;
        chorus.process (bl.data(), br.data(), n);
        CHECK (bl == dry);
        CHECK (br == dry);
    }
}

//==================================================================================================
namespace
{
/** Goertzel power at `hz` over `x` (the last full cycles, for a clean comparison). */
double goertzelPower (const std::vector<float>& x, double hz, double sampleRate)
{
    const double w = 2.0 * 3.14159265358979323846 * hz / sampleRate;
    const double c = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (const float v : x)
    {
        const double s = static_cast<double> (v) + c * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    return (s1 * s1 + s2 * s2 - c * s1 * s2) / (static_cast<double> (x.size()) * static_cast<double> (x.size()));
}

std::vector<float> shimmerTail (float shimmer)
{
    dsp::ShimmerReverb reverb;
    reverb.prepare (48000.0);
    dsp::ShimmerReverb::Settings s;
    s.size = 0.6f;
    s.decaySeconds = 4.0f;
    s.damping = 0.3f;
    s.predelayMs = 0.0f;
    s.shimmer = shimmer;
    s.modulation = 0.0f;   // no FM sidebands, for a clean spectral comparison
    s.mix = 1.0f;
    reverb.setSettings (s);

    const int total = 4 * 48000;
    std::vector<float> l (static_cast<size_t> (total)), r (static_cast<size_t> (total));
    for (int i = 0; i < total; ++i)
        l[static_cast<size_t> (i)] = r[static_cast<size_t> (i)]
            = 0.2f * std::sin (dsp::kTwoPi * 440.0f * static_cast<float> (i) / 48000.0f);
    reverb.process (l.data(), r.data(), total);
    return { l.end() - 48000, l.end() };
}
} // namespace

TEST_CASE ("shimmer adds the octave and the fifth", "[fx][shimmer]")
{
    const auto dry = shimmerTail (0.0f);
    const auto wet = shimmerTail (0.6f);
    const double dry440 = goertzelPower (dry, 440.0, 48000.0);
    const double wet440 = goertzelPower (wet, 440.0, 48000.0);
    const double dry880 = goertzelPower (dry, 880.0, 48000.0);
    const double wet880 = goertzelPower (wet, 880.0, 48000.0);
    const double dry1320 = goertzelPower (dry, 1320.0, 48000.0);
    const double wet1320 = goertzelPower (wet, 1320.0, 48000.0);
    INFO ("440 dry " << dry440 << " wet " << wet440 << " | 880 dry " << dry880 << " wet " << wet880
          << " | 1320 dry " << dry1320 << " wet " << wet1320);
    CHECK (wet440 > dry440 * 0.01);
    CHECK (wet880 > dry880 * 100.0);
    CHECK (wet1320 > dry1320 * 100.0);
    CHECK (wet880 > wet440 * 1.0e-5);
    CHECK (wet1320 > wet440 * 1.0e-6);
}

//==================================================================================================
TEST_CASE ("the phaser picture's response is the phaser's, on a sine sweep", "[fx][phaser][T6.12]")
{
    // T6.12: views.js `phaserResponse` draws dsp::Phaser::magnitude. Freeze the LFO where a run at
    // 0.25 Hz leaves it, read the break frequency from lfoPhase(), then play sines through it.
    constexpr double fs = 48000.0;
    struct Case { int stages; float feedback, mix, depth; double runSeconds; };
    const Case cases[] = { { 6, 0.3f, 0.4f, 0.6f, 0.0 }, { 8, 0.8f, 1.0f, 1.0f, 1.0 }, { 4, 0.8f, 0.5f, 1.0f, 3.0 },
                           { 2, 0.0f, 0.7f, 0.5f, 0.5 }, { 6, 0.55f, 0.6f, 0.8f, 2.2 } };
    double worst = 0.0;
    for (const auto& c : cases)
    {
        for (int k = 0; k <= 36; ++k)
        {
            const double hz = 21.0 * std::pow (950.0, k / 36.0);   // 21 Hz … 20 kHz
            dsp::Phaser phaser;
            phaser.prepare (fs);
            dsp::Phaser::Settings s;
            s.stages = c.stages;
            s.feedback = c.feedback;
            s.mix = c.mix;
            s.depth = c.depth;
            s.rateHz = 0.25f;
            phaser.setSettings (s);
            if (c.runSeconds > 0.0)
            {
                std::vector<float> l (static_cast<size_t> (c.runSeconds * fs)), r (l.size());
                phaser.process (l.data(), r.data(), static_cast<int> (l.size()));
            }
            s.rateHz = 0.0f;   // hold the sweep where it is
            phaser.setSettings (s);
            const double breakHz = dsp::Phaser::breakHz (c.depth, phaser.lfoPhase());

            const int n = static_cast<int> (fs);
            std::vector<float> l (static_cast<size_t> (n)), r (static_cast<size_t> (n));
            constexpr double amp = 0.01;   // small: the feedback's tanh stays linear
            for (int i = 0; i < n; ++i)
                l[static_cast<size_t> (i)] = r[static_cast<size_t> (i)] = static_cast<float> (amp * std::sin (2.0 * std::numbers::pi * hz * i / fs));
            phaser.process (l.data(), r.data(), n);
            // the steady state: the second half, against the input's own power there
            const std::vector<float> out (l.begin() + n / 2, l.end());
            std::vector<float> in (static_cast<size_t> (n / 2));
            for (int i = 0; i < n / 2; ++i)
                in[static_cast<size_t> (i)] = static_cast<float> (amp * std::sin (2.0 * std::numbers::pi * hz * (i + n / 2) / fs));
            const double measured = 10.0 * std::log10 (goertzelPower (out, hz, fs) / goertzelPower (in, hz, fs));
            const double drawn = 20.0 * std::log10 (dsp::Phaser::magnitude (hz, breakHz, fs, c.stages, c.feedback, c.mix));
            if (drawn < -6.0)
                continue;   // a notch: away from them the picture must hold to 1 dB
            INFO ("stages " << c.stages << " fb " << c.feedback << " mix " << c.mix << " fc " << breakHz
                  << " Hz, at " << hz << " Hz: drawn " << drawn << " dB, measured " << measured << " dB");
            CHECK (std::abs (measured - drawn) <= 1.0);
            worst = std::max (worst, std::abs (measured - drawn));
        }
    }
    INFO ("worst error away from the notches " << worst << " dB");
    CHECK (worst < 0.25);   // T6.12: the formula is the filter's own, not an approximation
    WARN ("phaser picture vs Phaser, worst error away from the notches: " << worst << " dB");
}
