#include "data/WorldData.h"
#include "data/WorldSources.h"
#include "transform/WavetableBuilder.h"

#include <juce_dsp/juce_dsp.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace worldtable;
using dsp::WavetableSet;

namespace
{
data::SpectrumSeries oceanSpectra()
{
    data::SpectrumSeries s;
    data::parseNdbcSpectral (data::loadFactoryData (data::sourceDef (0)), s);
    return s;
}

data::ScalarSeries magnetometer()
{
    data::ScalarSeries s;
    data::parseUsgsGeomag (data::loadFactoryData (data::sourceDef (5)), s);
    return s;
}

/** Energy fraction of a frame above harmonic `limit`. */
double energyAbove (const float* frame, int limit)
{
    juce::dsp::FFT fft (11);
    std::vector<float> buf (2 * WavetableSet::kFrameSize, 0.0f);
    std::copy (frame, frame + WavetableSet::kFrameSize, buf.begin());
    fft.performFrequencyOnlyForwardTransform (buf.data(), true);
    double above = 0.0, total = 0.0;
    for (int h = 1; h < WavetableSet::kFrameSize / 2; ++h)
    {
        const double e = static_cast<double> (buf[static_cast<size_t> (h)]) * buf[static_cast<size_t> (h)];
        total += e;
        if (h > limit)
            above += e;
    }
    return total > 0.0 ? above / total : 0.0;
}

void checkTableSane (const WavetableSet& t)
{
    for (int f = 0; f < t.numFrames; ++f)
        for (int m = 0; m < WavetableSet::kNumMips; ++m)
        {
            const float* d = t.frame (m, f);
            float peak = 0.0f;
            for (int i = 0; i <= WavetableSet::kFrameSize; ++i)
            {
                REQUIRE (std::isfinite (d[i]));
                peak = std::max (peak, std::abs (d[i]));
            }
            REQUIRE (peak <= 1.0f);   // level 0 is ≤ 0.95; truncated mips may ripple slightly above
            REQUIRE (std::abs (d[WavetableSet::kFrameSize] - d[0]) < 1.0e-12f);   // guard sample
        }
}
} // namespace

TEST_CASE ("Ocean spectra become a 64-frame band-limited table", "[builder]")
{
    transform::WavetableBuilder builder;
    auto table = std::make_unique<WavetableSet>();
    transform::TablePreview preview;

    REQUIRE (builder.build (oceanSpectra(), {}, *table, preview));
    REQUIRE (table->numFrames == 64);
    REQUIRE (preview.numFrames == 64);
    CHECK (preview.wave.size() == static_cast<size_t> (64 * transform::TablePreview::kWavePoints));
    CHECK (preview.raw.size() == static_cast<size_t> (64 * transform::TablePreview::kRawPoints));
    CHECK (std::is_sorted (preview.frameTimesUtc.begin(), preview.frameTimesUtc.end()));

    checkTableSane (*table);

    SECTION ("frames are RMS-normalised (or peak-limited below the target)")
    {
        for (int f = 0; f < table->numFrames; ++f)
        {
            const float* d = table->frame (0, f);
            double sum = 0.0;
            for (int i = 0; i < WavetableSet::kFrameSize; ++i)
                sum += static_cast<double> (d[i]) * d[i];
            const auto rms = std::sqrt (sum / WavetableSet::kFrameSize);
            CHECK (rms <= transform::WavetableBuilder::kTargetRms * 1.001);
            CHECK (rms > 0.1);
        }
    }

    SECTION ("mip levels hold no energy above their harmonic limit")
    {
        for (int m = 1; m < WavetableSet::kNumMips; ++m)
            CHECK (energyAbove (table->frame (m, 32), WavetableSet::harmonicsAtMip (m)) < 1.0e-6);
    }
}

TEST_CASE ("Builder is deterministic and transform settings change the sound", "[builder]")
{
    transform::WavetableBuilder a, b;
    auto t1 = std::make_unique<WavetableSet>();
    auto t2 = std::make_unique<WavetableSet>();
    transform::TablePreview p;
    const auto spectra = oceanSpectra();

    a.build (spectra, {}, *t1, p);
    b.build (spectra, {}, *t2, p);
    CHECK (std::equal (t1->frame (0, 10), t1->frame (0, 10) + WavetableSet::kStride, t2->frame (0, 10)));

    transform::TransformSettings bright;
    bright.tiltDbPerOct = 6.0f;
    bright.spread = 1.0f;
    b.build (spectra, bright, *t2, p);
    CHECK (energyAbove (t2->frame (0, 10), 32) > energyAbove (t1->frame (0, 10), 32) + 0.05);
}

TEST_CASE ("Magnetometer series become cycles", "[builder]")
{
    transform::WavetableBuilder builder;
    auto table = std::make_unique<WavetableSet>();
    transform::TablePreview preview;

    const auto series = magnetometer();
    REQUIRE (builder.build (series, {}, *table, preview));
    CHECK (table->numFrames == 64);
    CHECK_FALSE (preview.isSpectrum);
    checkTableSane (*table);

    // A 152-sample window can only carry 75 harmonics; nothing above that.
    const int window = transform::WavetableBuilder::windowForSpread (transform::TransformSettings{}.spread);
    CHECK (energyAbove (table->frame (0, 20), window / 2) < 1.0e-6);
}

TEST_CASE ("Silent data falls back to a sine instead of silence", "[builder]")
{
    data::SpectrumSeries s;
    data::SpectrumRow row;
    row.timeUtc = 0.0;
    row.frequencyHz = { 0.05f, 0.1f, 0.2f, 0.3f };
    row.density = { 0.0f, 0.0f, 0.0f, 0.0f };
    s.rows = { row };

    transform::WavetableBuilder builder;
    auto table = std::make_unique<WavetableSet>();
    transform::TablePreview preview;
    REQUIRE (builder.build (s, {}, *table, preview));
    CHECK (table->numFrames == 1);
    checkTableSane (*table);
    CHECK (energyAbove (table->frame (0, 0), 1) < 1.0e-6);
}
