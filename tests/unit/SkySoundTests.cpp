#include "AllocationCounter.h"
#include "data/SkyDays.h"
#include "dsp/ResonatorBank.h"
#include "dsp/SkySound.h"
#include "dsp/Voicing.h"
#include "sky/SkyFeed.h"
#include "sky/SkyRuntime.h"

#include <juce_core/juce_core.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <random>
#include <vector>

#ifndef WORLDTABLE_GOLDEN_DIR
 #error "WORLDTABLE_GOLDEN_DIR must be defined by tests/CMakeLists.txt"
#endif

using namespace worldtable;
using dsp::Layer;

namespace
{
constexpr double kStormStart = 1715299200.0;   // 2024-05-10T00:00:00Z
constexpr int kNodes = 64;                     // SkyParams::nodes

double dbOf (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1e-30)); }

double rmsOf (const float* x, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; ++i)
        s += static_cast<double> (x[i]) * x[i];
    return std::sqrt (s / std::max (n, 1));
}

float maxStepOf (const std::vector<float>& x, size_t from, size_t to)
{
    float m = 0.0f;
    for (size_t i = std::max<size_t> (from, 1); i < std::min (to, x.size()); ++i)
        m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m;
}

/** The storm day at `speed` from `utc` (seconds since the epoch). */
std::unique_ptr<sky::SkyRuntime> stormRuntime (double sampleRate, double utc, double speed = 24.0)
{
    auto rt = std::make_unique<sky::SkyRuntime>();
    sky::SkyFeed feed (*rt);
    REQUIRE (feed.load (data::kDefaultSkyDay, 1, sky::SkyParams {}));
    rt->seek (utc);
    rt->setSpeed (speed);
    rt->prepare (sampleRate);
    return rt;
}

/** A runtime with no track: the sky holds its quiet start and the curtain keeps moving. Cheap. */
std::unique_ptr<sky::SkyRuntime> quietRuntime (double sampleRate)
{
    auto rt = std::make_unique<sky::SkyRuntime>();
    rt->prepare (sampleRate);
    return rt;
}

dsp::Chord chordOf (const juce::var& v)
{
    dsp::Chord c;
    c.key = v["key"];
    c.degree = v["degree"];
    const auto* iv = v["intervals"].getArray();
    REQUIRE (iv != nullptr);
    c.count = iv->size();
    for (int i = 0; i < c.count; ++i)
        c.intervals[static_cast<size_t> (i)] = static_cast<int8_t> (static_cast<int> ((*iv)[i]));
    return c;
}

/** Stereo output of a whole run. */
struct Render
{
    std::vector<float> left, right;

    void resize (size_t n) { left.assign (n, 0.0f); right.assign (n, 0.0f); }
};

/** Plays `seconds` through SkySound in blocks from `blockSize(i)`. `beforeBlock(i, done)` may ask
    for chords. */
template <typename BlockSize, typename BeforeBlock>
Render play (sky::SkyRuntime& rt, dsp::SkySound& sound, double sampleRate, double seconds,
             BlockSize blockSize, BeforeBlock beforeBlock)
{
    Render r;
    const auto total = static_cast<int> (std::lround (seconds * sampleRate));
    r.resize (static_cast<size_t> (total));
    for (int done = 0, i = 0; done < total; ++i)
    {
        const int n = std::min (blockSize (i), total - done);
        beforeBlock (i, done);
        sound.process (rt, r.left.data() + done, r.right.data() + done, n);
        done += n;
    }
    return r;
}

template <typename BlockSize>
Render play (sky::SkyRuntime& rt, dsp::SkySound& sound, double sampleRate, double seconds, BlockSize blockSize)
{
    return play (rt, sound, sampleRate, seconds, blockSize, [] (int, int) {});
}

const dsp::Chord kDmaj7 = dsp::Chord::dMaj7();
const dsp::Chord kGadd9 { 2, 5, { 0, 4, 7, 14, 0 }, 4 };    // IV add9 in D
const dsp::Chord kBbmaj7 { 2, 8, { 0, 4, 7, 11, 0 }, 4 };   // ♭VI maj7 in D
} // namespace

//==================================================================================================
TEST_CASE ("ResonatorBank rings at its frequency and decays 60 dB in its T60", "[sound][bank]")
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        dsp::ResonatorBank bank;
        bank.prepare (1, rate, 1);
        bank.setActive (0, true);
        const int k = bank.index (0, 1, 0);
        const double t60 = 2.0, hz = 440.0;
        const double r = bank.radius (t60);
        bank.tune (k, hz, r);
        bank.ramp (k, 1.0, 1.0, 1);

        // 10 ms of noise, then let it ring for 1.2 s
        const auto total = static_cast<int> (1.21 * rate);
        std::vector<float> l (static_cast<size_t> (total)), rr (static_cast<size_t> (total));
        const auto burst = static_cast<int> (0.01 * rate);
        bank.setInput (k, std::sqrt (1.0 - r * r) * 50.0);
        bank.process (l.data(), rr.data(), burst);
        bank.setInput (k, 0.0);
        bank.process (l.data() + burst, rr.data() + burst, total - burst);

        // frequency from the zero crossings between 0.1 s and 1.1 s
        int crossings = 0;
        const auto a = static_cast<size_t> (0.1 * rate), b = static_cast<size_t> (1.1 * rate);
        for (size_t i = a + 1; i < b; ++i)
            crossings += (l[i - 1] < 0.0f) != (l[i] < 0.0f) ? 1 : 0;
        CHECK_THAT (crossings / 2.0, Catch::Matchers::WithinAbs (hz, 1.0));

        // decay: half a T60 apart → −30 dB
        const auto w = static_cast<int> (0.05 * rate);
        const double early = rmsOf (l.data() + static_cast<size_t> (0.1 * rate), w);
        const double late = rmsOf (l.data() + static_cast<size_t> ((0.1 + t60 / 2.0) * rate), w);
        CHECK_THAT (dbOf (late / early), Catch::Matchers::WithinAbs (-30.0, 0.3));
    }
}

TEST_CASE ("ResonatorBank loudness doesn't depend on the decay (input × √(1−r²))", "[sound][bank]")
{
    const double rate = 48000.0;
    std::vector<double> levels;
    for (const double t60 : { 2.5, 6.0, 20.0 })
    {
        dsp::ResonatorBank bank;
        bank.prepare (1, rate, 7);
        bank.setActive (0, true);
        const int k = bank.index (0, 0, 0);
        const double r = bank.radius (t60);
        bank.tune (k, 220.0, r);
        bank.setInput (k, std::sqrt (1.0 - r * r));
        bank.ramp (k, 1.0, 1.0, 1);

        // settle for 2 × T60, then measure 20 s
        std::vector<float> l (48000), rr (48000);
        for (int s = 0; s < static_cast<int> (2.0 * t60); ++s)
            bank.process (l.data(), rr.data(), 48000);
        double sum = 0.0;
        for (int s = 0; s < 20; ++s)
        {
            bank.process (l.data(), rr.data(), 48000);
            sum += rmsOf (l.data(), 48000);
        }
        levels.push_back (sum / 20.0);
    }
    CHECK_THAT (dbOf (levels[1] / levels[0]), Catch::Matchers::WithinAbs (0.0, 1.0));
    CHECK_THAT (dbOf (levels[2] / levels[0]), Catch::Matchers::WithinAbs (0.0, 1.0));
}

TEST_CASE ("ResonatorBank: retuning a ringing resonator doesn't jump; the guard resets NaN", "[sound][bank]")
{
    dsp::ResonatorBank bank;
    bank.prepare (1, 48000.0, 3);
    bank.setActive (0, true);
    const int k = bank.index (0, 2, 0);
    const double r = bank.radius (5.0);
    bank.tune (k, 880.0, r);
    bank.ramp (k, 1.0, 1.0, 1);
    bank.setInput (k, std::sqrt (1.0 - r * r) * 20.0);

    std::vector<float> l (9600), rr (9600);
    bank.process (l.data(), rr.data(), 4800);
    bank.setInput (k, 0.0);
    bank.tune (k, 880.0 * std::exp2 (1.0 / 12.0), r);   // a semitone up, mid-ring
    bank.process (l.data() + 4800, rr.data() + 4800, 4800);
    const float before = maxStepOf (l, 4700, 4800);
    const float after = maxStepOf (l, 4800, 4900);
    CHECK (after < before * 1.15f);
    CHECK (after > 0.0f);

    bank.setInput (k, std::numeric_limits<double>::quiet_NaN());
    bank.process (l.data(), rr.data(), 1);
    bank.setInput (k, 0.0);
    CHECK (bank.guard() == 1);
    bank.process (l.data(), rr.data(), 64);
    CHECK (std::all_of (l.begin(), l.begin() + 64, [] (float x) { return std::isfinite (x); }));
}

//==================================================================================================
TEST_CASE ("Voicing: slots, ranges, nearest notes and voice-leading as in the sketch", "[sound][voicing]")
{
    const dsp::Chord sus2 { 2, 0, { 0, 2, 7, 11, 0 }, 4 };   // D maj7sus2: D E A C♯
    CHECK (dsp::layerSlots (sus2, Layer::red) == dsp::Slots { 2, 9, 2, 9, 2 });
    CHECK (dsp::layerSlots (sus2, Layer::green) == dsp::Slots { 2, 4, 9, 1, 2 });
    CHECK (dsp::layerSlots (sus2, Layer::violet) == dsp::Slots { 4, 1, 4, 1, 4 });

    const dsp::Chord m11 { 0, 0, { 0, 3, 7, 10, 17 }, 5 };   // C m11: C E♭ G B♭ F
    CHECK (dsp::layerSlots (m11, Layer::green) == dsp::Slots { 0, 3, 7, 10, 5 });
    CHECK (dsp::layerSlots (m11, Layer::violet) == dsp::Slots { 3, 10, 5, 3, 10 });

    // spec/05 §12: red 31–49, green 50–74, violet 72–90 at Register 0, spread 2
    CHECK (dsp::layerRange (Layer::red, 0.0, 2.0).lo == 31);
    CHECK (dsp::layerRange (Layer::red, 0.0, 2.0).hi == 49);
    CHECK (dsp::layerRange (Layer::green, 0.0, 2.0).lo == 50);
    CHECK (dsp::layerRange (Layer::green, 0.0, 2.0).hi == 74);
    CHECK (dsp::layerRange (Layer::violet, 0.0, 2.0).lo == 72);
    CHECK (dsp::layerRange (Layer::violet, 0.0, 2.0).hi == 90);
    CHECK (dsp::layerRange (Layer::violet, 0.0, 0.5).hi - dsp::layerRange (Layer::violet, 0.0, 0.5).lo == 12);

    CHECK (dsp::nearestNote (2, 40.3, 31, 49) == 38);    // D near E
    CHECK (dsp::nearestNote (9, 40.3, 31, 49) == 45);    // A: up 5 rather than down 7
    CHECK (dsp::nearestNote (9, 30.0, 31, 49) == 33);    // folded into the range
    CHECK (dsp::nearestNote (1, 60.0, 72, 78) == 73);

    // D maj7sus2 → B♭ maj7: the least total motion; of the equal ones, the first in lexicographic
    // order (values from the sketch)
    const auto lead = dsp::leadSlots ({ 2, 4, 9, 1, 2 }, { 10, 2, 5, 9, 10 });
    CHECK (lead.cost == 8);
    CHECK (lead.slots == dsp::Slots { 10, 5, 9, 10, 2 });
}

//==================================================================================================
namespace
{
struct SoundGolden
{
    juce::var json;
    double startUtc = 0.0;
    double seconds = 0.0;
};

const SoundGolden& soundGolden()
{
    static const SoundGolden g = []
    {
        SoundGolden s;
        s.json = juce::JSON::parse (juce::File (WORLDTABLE_GOLDEN_DIR).getChildFile ("sound/2024-05-10-1720.json").loadFileAsString());
        s.startUtc = kStormStart + (17 * 60 + 20) * 60.0;
        s.seconds = s.json["seconds"];
        return s;
    }();
    return g;
}

/** The notes of `bank` match the golden's, per layer and voice. Returns the mismatches. */
int noteMismatches (const dsp::SkySound& sound, int bank, const juce::var& notes)
{
    int bad = 0;
    for (int l = 0; l < dsp::kNumLayers; ++l)
        for (int v = 0; v < sound.voices(); ++v)
            if (sound.note (bank, static_cast<Layer> (l), v) != static_cast<int> (notes[l][v]))
                ++bad;
    return bad;
}
} // namespace

TEST_CASE ("SkySound matches the T1.3 sketch on the 10 May 2024 storm (dry, 120 s)", "[sound][golden]")
{
    const auto& g = soundGolden();
    const auto& json = g.json;
    REQUIRE (static_cast<int> (json["sampleRate"]) == 48000);
    REQUIRE (static_cast<int> (json["voices"]) == kNodes);

    auto rt = stormRuntime (48000.0, g.startUtc, json["speed"]);
    dsp::SkySound sound;
    sound.prepare (48000.0, kNodes, json["seed"], dsp::SkySound::Settings {}, chordOf (json["start"]));
    CHECK (noteMismatches (sound, 0, json["start"]["notes"]) == 0);

    // One tick per block, so each request lands on its tick as in the sketch (Harmony asks inside
    // the tick there; the request is taken in the same tick).
    const auto* requests = json["requests"].getArray();
    const auto* applied = json["applied"].getArray();
    size_t nextRequest = 0, nextApplied = 0;
    int appliedOnTick = 0, noteErrors = 0;
    uint32_t changesSeen = 0;
    const auto out = play (*rt, sound, 48000.0, g.seconds, [] (int) { return 480; }, [&] (int, int)
    {
        if (sound.chordChanges() != changesSeen)   // a change happened on the last tick: check its voicing
        {
            changesSeen = sound.chordChanges();
            if (nextApplied < static_cast<size_t> (applied->size()))
            {
                const auto& a = (*applied)[static_cast<int> (nextApplied++)];
                appliedOnTick += static_cast<uint64_t> (static_cast<int> (a["tick"])) == sound.ticks() ? 1 : 0;
                CHECK (static_cast<int> (a["bank"]) == sound.currentBank());
                noteErrors += noteMismatches (sound, sound.currentBank(), a["notes"]);
            }
        }
        const auto tick = sound.ticks() + 1;   // the tick this block starts with
        while (nextRequest < static_cast<size_t> (requests->size())
               && static_cast<uint64_t> (static_cast<int> ((*requests)[static_cast<int> (nextRequest)]["tick"])) == tick)
        {
            const auto& q = (*requests)[static_cast<int> (nextRequest++)];
            sound.requestChord (chordOf (q), q["urgent"]);
        }
    });
    CHECK (nextRequest == static_cast<size_t> (requests->size()));
    CHECK (nextApplied == static_cast<size_t> (applied->size()));
    CHECK (appliedOnTick == applied->size());
    CHECK (noteErrors == 0);
    CHECK (sound.resets() == 0);

    // RMS per 100 ms
    const auto& rms = json["rms"];
    const int window = rms["window"];
    double worstRms = 0.0;
    int worstWindow = 0;
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& want = rms[ch == 0 ? "left" : "right"];
        const auto& got = ch == 0 ? out.left : out.right;
        for (int w = 0; w < want.size(); ++w)
        {
            const double expected = want[w];
            const double actual = rmsOf (got.data() + static_cast<size_t> (w * window), window);
            const double rel = std::abs (actual - expected) / std::max (expected, 1e-6);
            if (rel > worstRms) { worstRms = rel; worstWindow = w; }
        }
    }

    // Sample windows: the null test
    double residual = 0.0, signal = 0.0, worstSample = 0.0;
    const auto* windows = json["windows"].getArray();
    for (const auto& w : *windows)
    {
        const int start = w["start"];
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto& want = w[ch == 0 ? "left" : "right"];
            const auto& got = ch == 0 ? out.left : out.right;
            for (int i = 0; i < want.size(); ++i)
            {
                const double e = want[i], a = got[static_cast<size_t> (start + i)];
                residual += (a - e) * (a - e);
                signal += e * e;
                worstSample = std::max (worstSample, std::abs (a - e));
            }
        }
    }
    const double nullDb = 10.0 * std::log10 (std::max (residual, 1e-30) / signal);
    std::printf ("Sound golden: RMS per 100 ms within %.2e relative (worst window %d), sample windows: "
                 "worst |error| %.2e, null-test residual %.1f dB below the signal\n",
                 worstRms, worstWindow, worstSample, -nullDb);
    CHECK (worstRms < 1e-4);
    CHECK (nullDb < -80.0);

    // No clicks through the start, the strike and both changes
    CHECK (maxStepOf (out.left, 0, out.left.size()) < 0.25f);
    CHECK (maxStepOf (out.right, 0, out.right.size()) < 0.25f);
}

//==================================================================================================
TEST_CASE ("SkySound double-banks a chord change: crossfade, ring-out, then the next", "[sound][chords]")
{
    auto rt = quietRuntime (48000.0);
    dsp::SkySound sound;
    sound.prepare (48000.0, kNodes, 1, dsp::SkySound::Settings {}, kDmaj7);
    const auto tickBlocks = [] (int) { return 480; };

    play (*rt, sound, 48000.0, 1.0, tickBlocks);
    CHECK (sound.bankActive (0));
    CHECK_FALSE (sound.bankActive (1));

    // A wander (not urgent) with a free bank: taken at once, crossfades over 12 s
    sound.requestChord (kGadd9, false);
    play (*rt, sound, 48000.0, 0.01, tickBlocks);
    CHECK (sound.chordChanges() == 1);
    CHECK (sound.currentBank() == 1);
    CHECK (sound.bankActive (0));
    CHECK (sound.bankActive (1));
    play (*rt, sound, 48000.0, 6.0, tickBlocks);
    CHECK_THAT (sound.crossfade(), Catch::Matchers::WithinAbs (0.5, 0.01));
    play (*rt, sound, 48000.0, 6.0, tickBlocks);
    CHECK_THAT (sound.crossfade(), Catch::Matchers::WithinAbs (1.0, 1e-12));

    // The next wander waits until the old bank has rung out (the longest T60, 20 s, after the fade)
    sound.requestChord (kBbmaj7, false);
    sound.requestChord (kDmaj7, false);   // replaces the pending one
    play (*rt, sound, 48000.0, 19.9, tickBlocks);
    CHECK (sound.hasPendingChord());
    CHECK (sound.bankActive (0));
    CHECK (sound.chordChanges() == 1);
    play (*rt, sound, 48000.0, 0.2, tickBlocks);
    CHECK_FALSE (sound.hasPendingChord());
    CHECK (sound.chordChanges() == 2);
    CHECK (sound.currentBank() == 0);
    CHECK (sound.chord().degree == kDmaj7.degree);

    // An urgent change finds the old bank ringing: damps it (T60 3 s) instead of waiting
    play (*rt, sound, 48000.0, 13.0, tickBlocks);
    sound.requestChord (kBbmaj7, true);
    play (*rt, sound, 48000.0, 0.01, tickBlocks);
    CHECK (sound.damping());
    CHECK (sound.hasPendingChord());
    play (*rt, sound, 48000.0, 3.0, tickBlocks);
    CHECK_FALSE (sound.damping());
    CHECK (sound.chordChanges() == 3);
    CHECK (sound.chord().degree == kBbmaj7.degree);
}

TEST_CASE ("SkySound: an urgent change mid-crossfade ends the fade and damps the old bank, without clicks", "[sound][chords]")
{
    auto rt = quietRuntime (48000.0);
    dsp::SkySound sound;
    sound.prepare (48000.0, kNodes, 2, dsp::SkySound::Settings {}, kDmaj7);
    const auto tickBlocks = [] (int) { return 480; };

    const auto out = play (*rt, sound, 48000.0, 30.0, tickBlocks, [&] (int i, int)
    {
        if (i == 500) sound.requestChord (kGadd9, true);    //  5 s: a free bank, taken at once
        if (i == 1100) sound.requestChord (kBbmaj7, true);  // 11 s: mid-fade, old bank ringing
        if (i == 1101) { CHECK (sound.damping()); CHECK_THAT (sound.crossfade(), Catch::Matchers::WithinAbs (1.0, 1e-12)); }
    });
    CHECK (sound.chordChanges() == 2);   // the B♭ came 3 s after its request
    const float steady = maxStepOf (out.left, 48000, 5 * 48000);
    CHECK (steady > 0.0f);
    CHECK (maxStepOf (out.left, 5 * 48000, out.left.size()) < 1.5f * steady + 1e-4f);
    CHECK (maxStepOf (out.right, 5 * 48000, out.right.size()) < 1.5f * maxStepOf (out.right, 48000, 5 * 48000) + 1e-4f);
}

TEST_CASE ("SkySound is block-size independent and allocation-free", "[sound][determinism]")
{
    // From 17:38:30, so the 17:38:48 onset strikes within the first seconds (24×)
    const double from = kStormStart + (17 * 60 + 38) * 60.0 + 30.0;
    const double seconds = 3.0;

    const auto run = [&] (auto blockSize)
    {
        auto rt = stormRuntime (48000.0, from);
        dsp::SkySound sound;
        sound.prepare (48000.0, kNodes, 1, dsp::SkySound::Settings {}, kDmaj7);
        const auto before = testing::allocationsOnThisThread();
        auto out = play (*rt, sound, 48000.0, seconds, blockSize);
        CHECK (testing::allocationsOnThisThread() - before <= 1);   // the Render vectors, before the loop
        return out;
    };

    const auto reference = run ([] (int) { return 4800; });
    CHECK (rmsOf (reference.left.data(), static_cast<int> (reference.left.size())) > 1e-4);
    std::mt19937 rng (5);
    std::uniform_int_distribution<int> sizes (1, 2000);
    const auto variants = {
        run ([] (int) { return 128; }),
        run ([] (int) { return 37; }),
        run ([&] (int) { return sizes (rng); }),
    };
    for (const auto& v : variants)
    {
        CHECK (std::memcmp (v.left.data(), reference.left.data(), reference.left.size() * sizeof (float)) == 0);
        CHECK (std::memcmp (v.right.data(), reference.right.data(), reference.right.size() * sizeof (float)) == 0);
    }
}

TEST_CASE ("SkySound doesn't allocate through onsets and chord changes", "[sound][realtime]")
{
    auto rt = stormRuntime (48000.0, kStormStart + (17 * 60 + 38) * 60.0);
    dsp::SkySound sound;
    sound.prepare (48000.0, kNodes, 1, dsp::SkySound::Settings {}, kDmaj7);
    std::vector<float> l (512), r (512);
    const auto before = testing::allocationsOnThisThread();
    for (int i = 0; i < 2000; ++i)   // 21 s
    {
        if (i == 100) sound.requestChord (kGadd9, true);
        if (i == 600) sound.requestChord (kBbmaj7, true);
        if (i == 700) sound.requestChord (kDmaj7, false);
        sound.process (*rt, l.data(), r.data(), 512);
    }
    CHECK (testing::allocationsOnThisThread() == before);
    CHECK (sound.chordChanges() >= 2);
    CHECK (rt->model().current().onsetCount > 0);
}

TEST_CASE ("SkySound takes waiting chords on the same ticks at 44.1 and 48 kHz", "[sound][determinism]")
{
    const auto ticksOfChanges = [] (double rate)
    {
        auto rt = quietRuntime (rate);
        dsp::SkySound sound;
        sound.prepare (rate, kNodes, 1, dsp::SkySound::Settings {}, kDmaj7);
        const int perTick = static_cast<int> (rate / 100.0);
        std::vector<uint64_t> ticks;
        uint32_t seen = 0;
        play (*rt, sound, rate, 50.0, [perTick] (int) { return perTick; }, [&] (int i, int)
        {
            if (sound.chordChanges() != seen) { seen = sound.chordChanges(); ticks.push_back (sound.ticks()); }
            if (i == 100) sound.requestChord (kGadd9, false);
            if (i == 1500) sound.requestChord (kBbmaj7, false);   // waits for the ring-out
        });
        return ticks;
    };
    const auto at48 = ticksOfChanges (48000.0);
    REQUIRE (at48.size() == 2);
    CHECK (at48[1] > 3200);   // 1 s + 12 s fade + 20 s ring-out
    CHECK (ticksOfChanges (44100.0) == at48);
}
