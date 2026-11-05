#include "dsp/DspCommon.h"
#include "dsp/fx/AutoGain.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
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
