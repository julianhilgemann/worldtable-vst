#include "AllocationCounter.h"
#include "data/SkyDays.h"
#include "dsp/Exciter.h"
#include "dsp/Harmony.h"
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

/** Stereo output of a whole run, and the allocations SkySound::process made. */
struct Render
{
    std::vector<float> left, right;
    int64_t allocations = 0;

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
        const auto before = testing::allocationsOnThisThread();
        sound.process (rt, r.left.data() + done, r.right.data() + done, n);
        r.allocations += testing::allocationsOnThisThread() - before;
        done += n;
    }
    return r;
}

template <typename BlockSize>
Render play (sky::SkyRuntime& rt, dsp::SkySound& sound, double sampleRate, double seconds, BlockSize blockSize)
{
    return play (rt, sound, sampleRate, seconds, blockSize, [] (int, int) {});
}

/** Output gain g on resonator k from now on (the ramp keeps going past its target: stop it). */
void holdGain (dsp::ResonatorBank& bank, int k, double g)
{
    float l = 0.0f, r = 0.0f;
    bank.ramp (k, g, g, 1);
    bank.process (&l, &r, 1);
    bank.ramp (k, g, g, 1);
}

/** Decays and fades ten times faster than the defaults, so the chord tests run quickly. */
dsp::SkySound::Settings fastSettings()
{
    dsp::SkySound::Settings s;
    s.t60 = { 2.0, 0.6, 0.25 };
    s.crossfadeSeconds = 1.2;
    return s;
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
        holdGain (bank, k, 1.0);

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
        holdGain (bank, k, 1.0);

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
    holdGain (bank, k, 1.0);
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
juce::String labelOf (const dsp::Chord& chord) { return juce::String::fromUTF8 (dsp::nameOf (chord).label.data()); }

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
    sound.prepare (48000.0, kNodes, static_cast<uint32_t> (static_cast<int> (json["seed"])), dsp::SkySound::Settings {}, chordOf (json["start"]));
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
    // fastSettings(): T60 2 / 0.6 / 0.25 s, crossfade 1.2 s; the urgent damping stays 3 s
    auto rt = quietRuntime (48000.0);
    dsp::SkySound sound;
    sound.prepare (48000.0, kNodes, 1, fastSettings(), kDmaj7);
    const auto tickBlocks = [] (int) { return 480; };

    play (*rt, sound, 48000.0, 0.2, tickBlocks);
    CHECK (sound.bankActive (0));
    CHECK_FALSE (sound.bankActive (1));

    // A wander (not urgent) with a free bank: taken at once, crossfades over 1.2 s
    sound.requestChord (kGadd9, false);
    play (*rt, sound, 48000.0, 0.01, tickBlocks);
    CHECK (sound.chordChanges() == 1);
    CHECK (sound.currentBank() == 1);
    CHECK (sound.bankActive (0));
    CHECK (sound.bankActive (1));
    play (*rt, sound, 48000.0, 0.6, tickBlocks);
    CHECK_THAT (sound.crossfade(), Catch::Matchers::WithinAbs (0.5, 0.01));
    play (*rt, sound, 48000.0, 0.6, tickBlocks);
    CHECK_THAT (sound.crossfade(), Catch::Matchers::WithinAbs (1.0, 1e-12));

    // The next wander waits until the old bank has rung out (the longest T60 after the fade)
    sound.requestChord (kBbmaj7, false);
    sound.requestChord (kDmaj7, false);   // replaces the pending one
    play (*rt, sound, 48000.0, 1.9, tickBlocks);
    CHECK (sound.hasPendingChord());
    CHECK (sound.bankActive (0));
    CHECK (sound.chordChanges() == 1);
    play (*rt, sound, 48000.0, 0.2, tickBlocks);
    CHECK_FALSE (sound.hasPendingChord());
    CHECK (sound.chordChanges() == 2);
    CHECK (sound.currentBank() == 0);
    CHECK (sound.chord().degree == kDmaj7.degree);

    // An urgent change finds the old bank ringing: damps it (T60 3 s) instead of waiting
    play (*rt, sound, 48000.0, 1.3, tickBlocks);
    CHECK (sound.bankActive (1));
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
    sound.prepare (48000.0, kNodes, 2, dsp::SkySound::Settings {}, kDmaj7);   // the real decays
    const auto tickBlocks = [] (int) { return 480; };

    const auto out = play (*rt, sound, 48000.0, 12.0, tickBlocks, [&] (int i, int)
    {
        if (i == 300) sound.requestChord (kGadd9, true);    // 3 s: a free bank, taken at once
        if (i == 600) sound.requestChord (kBbmaj7, true);   // 6 s: mid-fade, the old bank ringing
        if (i == 601)
        {
            CHECK (sound.damping());
            CHECK_THAT (sound.crossfade(), Catch::Matchers::WithinAbs (1.0, 1e-12));
        }
    });
    CHECK (sound.chordChanges() == 2);   // the B♭ came 3 s after its request
    for (const auto* ch : { &out.left, &out.right })
    {
        const float steady = maxStepOf (*ch, 48000, 3 * 48000);
        CHECK (steady > 0.0f);
        CHECK (maxStepOf (*ch, 3 * 48000, ch->size()) < 1.5f * steady + 1e-4f);
    }
}

TEST_CASE ("SkySound is block-size independent and allocation-free", "[sound][determinism]")
{
    // From 17:38:30, so the 17:38:48 onset strikes within the first second (24×)
    const double from = kStormStart + (17 * 60 + 38) * 60.0 + 30.0;
    const auto run = [&] (auto blockSize)
    {
        auto rt = stormRuntime (48000.0, from);
        dsp::SkySound sound;
        sound.prepare (48000.0, kNodes, 1, dsp::SkySound::Settings {}, kDmaj7);
        auto out = play (*rt, sound, 48000.0, 3.0, blockSize);
        CHECK (out.allocations == 0);
        CHECK (rt->model().current().onsetCount > 0);
        return out;
    };

    const auto reference = run ([] (int) { return 4800; });
    CHECK (rmsOf (reference.left.data(), static_cast<int> (reference.left.size())) > 1e-4);
    std::mt19937 rng (5);
    std::uniform_int_distribution<int> sizes (1, 2000);
    const std::vector<Render> variants {
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
    auto rt = stormRuntime (48000.0, kStormStart + (17 * 60 + 38) * 60.0);   // onset after 2 s
    dsp::SkySound sound;
    sound.prepare (48000.0, kNodes, 1, fastSettings(), kDmaj7);
    const auto out = play (*rt, sound, 48000.0, 8.0, [] (int) { return 512; }, [&] (int i, int)
    {
        if (i == 100) sound.requestChord (kGadd9, true);   // free bank
        if (i == 300) sound.requestChord (kBbmaj7, true);  // damps
        if (i == 320) sound.requestChord (kDmaj7, false);  // replaces it while it waits
    });
    CHECK (out.allocations == 0);
    CHECK (sound.chordChanges() == 2);
    CHECK (sound.chord().degree == kDmaj7.degree);
    CHECK (rt->model().current().onsetCount > 0);
}

TEST_CASE ("SkySound takes waiting chords on the same ticks at 44.1 and 48 kHz", "[sound][determinism]")
{
    const auto ticksOfChanges = [] (double rate)
    {
        auto rt = quietRuntime (rate);
        dsp::SkySound sound;
        sound.prepare (rate, kNodes, 1, fastSettings(), kDmaj7);
        const int perTick = static_cast<int> (rate / 100.0);
        std::vector<uint64_t> ticks;
        uint32_t seen = 0;
        play (*rt, sound, rate, 5.0, [perTick] (int) { return perTick; }, [&] (int i, int)
        {
            if (sound.chordChanges() != seen)
            {
                seen = sound.chordChanges();
                ticks.push_back (sound.ticks());
            }
            if (i == 20) sound.requestChord (kGadd9, false);
            if (i == 150) sound.requestChord (kBbmaj7, false);   // waits for the ring-out
        });
        return ticks;
    };
    const auto at48 = ticksOfChanges (48000.0);
    REQUIRE (at48.size() == 2);
    CHECK (at48[0] == 21);
    CHECK (at48[1] >= 21 + 120 + 200);   // the 1.2 s fade, then the 2 s ring-out
    CHECK (ticksOfChanges (44100.0) == at48);
}

//==================================================================================================
TEST_CASE ("the sky's progression writes the golden's chord changes", "[sound][progression][golden]")
{
    const auto& g = soundGolden();
    const auto& json = g.json;
    const auto* applied = json["applied"].getArray();
    REQUIRE (applied != nullptr);
    const auto seed = static_cast<uint32_t> (static_cast<int> (json["seed"]));

    struct Change { uint64_t tick; int bank; juce::String label; };
    struct Run
    {
        std::vector<Change> changes;
        std::array<std::array<std::array<int, 128>, dsp::kNumLayers>, 2> notes {};
        int voices = 0;
        int64_t allocations = 0;
        juce::String startLabel;
        int startNoteErrors = 0;
    };

    const auto run = [&] (int block, double seconds) -> Run
    {
        auto rt = stormRuntime (48000.0, g.startUtc, json["speed"]);
        rt->process (0, nullptr);   // apply the seek so current().phase is the phase at 17:20
        dsp::SkySound sound;
        auto& harmony = sound.harmony();
        harmony.setSeed (seed);
        harmony.setKey (2);
        harmony.setMood (dsp::Mood::bright);
        harmony.setProgression (1.0);
        harmony.start (static_cast<dsp::Phase> (rt->model().current().phase));

        Run r;
        r.startLabel = labelOf (harmony.chord());
        sound.prepare (48000.0, kNodes, seed, dsp::SkySound::Settings {}, harmony.chord());
        r.startNoteErrors = noteMismatches (sound, 0, json["start"]["notes"]);

        const auto total = static_cast<int> (std::lround (seconds * 48000.0));
        std::vector<float> l (static_cast<size_t> (total)), rr (static_cast<size_t> (total));
        uint32_t seen = sound.chordChanges();
        for (int done = 0; done < total;)
        {
            const int n = std::min (block > 0 ? block : 64 + static_cast<int> ((static_cast<uint64_t> (done) * 7919u) % 1937u), total - done);
            const auto before = testing::allocationsOnThisThread();
            sound.process (*rt, l.data() + done, rr.data() + done, n);
            r.allocations += testing::allocationsOnThisThread() - before;
            if (sound.chordChanges() != seen)
            {
                seen = sound.chordChanges();
                r.changes.push_back ({ sound.lastChordTick(), sound.currentBank(), labelOf (sound.chord()) });
            }
            done += n;
        }
        r.voices = sound.voices();
        for (int b = 0; b < 2; ++b)
            for (int layer = 0; layer < dsp::kNumLayers; ++layer)
                for (int v = 0; v < r.voices; ++v)
                    r.notes[static_cast<size_t> (b)][static_cast<size_t> (layer)][static_cast<size_t> (v)]
                        = sound.note (b, static_cast<Layer> (layer), v);
        return r;
    };

    const auto full = run (480, g.seconds);
    CHECK (full.startLabel == juce::String (json["start"]["label"].toString()));
    CHECK (full.startNoteErrors == 0);
    CHECK (full.changes.size() == static_cast<size_t> (applied->size()));
    CHECK (full.allocations == 0);

    for (size_t i = 0; i < full.changes.size() && i < static_cast<size_t> (applied->size()); ++i)
    {
        const auto& want = (*applied)[static_cast<int> (i)];
        INFO ("change " << i);
        CHECK (full.changes[i].tick == static_cast<uint64_t> (static_cast<int> (want["tick"])));
        CHECK (full.changes[i].bank == static_cast<int> (want["bank"]));
        CHECK (full.changes[i].label == juce::String (want["label"].toString()));

        const auto bank = static_cast<size_t> (static_cast<int> (want["bank"]));
        int bad = 0;
        for (int layer = 0; layer < dsp::kNumLayers; ++layer)
            for (int v = 0; v < full.voices; ++v)
                if (full.notes[bank][static_cast<size_t> (layer)][static_cast<size_t> (v)]
                    != static_cast<int> (want["notes"][layer][v]))
                    ++bad;
        CHECK (bad == 0);
    }

    if (full.changes.size() == 2)   // the golden's storm: onset at 4692, recovery at 10863
    {
        CHECK (full.changes[0].tick == 4692);
        CHECK (full.changes[0].label == juce::String::fromUTF8 ("B♭maj7"));
        CHECK (full.changes[1].tick == 10863);
        CHECK (full.changes[1].label == juce::String::fromUTF8 ("Gm add9"));
    }

    // Block size: the first change must land on the same tick with a 37-sample block.
    const auto small = run (37, 60.0);
    REQUIRE_FALSE (small.changes.empty());
    CHECK (small.changes[0].tick == full.changes[0].tick);
    CHECK (small.changes[0].label == full.changes[0].label);
    CHECK (small.changes[0].bank == full.changes[0].bank);
    CHECK (small.allocations == 0);

    const auto mid = run (128, 60.0);
    REQUIRE_FALSE (mid.changes.empty());
    CHECK (mid.changes[0].tick == full.changes[0].tick);
    CHECK (mid.changes[0].label == full.changes[0].label);

    const auto mixed = run (0, 60.0);   // deterministic 64–2000 blocks
    REQUIRE_FALSE (mixed.changes.empty());
    CHECK (mixed.changes[0].tick == full.changes[0].tick);
    CHECK (mixed.changes[0].label == full.changes[0].label);
    CHECK (mixed.allocations == 0);
}

TEST_CASE ("A re-based exciter doesn't read a jump in the sky as a rise", "[sound][exciter][seek]")
{
    // T6.6: a seek can brighten the curtain in one tick; without a re-base the exciter would hear a
    // steep rise and swell the bank for ~0.3 s.
    const auto excitationAfterJump = [] (bool rebase)
    {
        dsp::Exciter ex;
        ex.prepare (4, 1, {});
        std::array<double, 4> b { 0.1, 0.1, 0.1, 0.1 };
        double t = 0.0;
        for (int i = 0; i < 600; ++i, t += dsp::Exciter::kTickSeconds)   // settle: the start's own rise decays
            ex.tick (t, 0.5, b.data());
        b.fill (0.9);   // the jump
        if (rebase)
            ex.rebase (b.data());
        double peak = 0.0;
        for (int i = 0; i < 100; ++i, t += dsp::Exciter::kTickSeconds)
        {
            ex.tick (t, 0.5, b.data());
            peak = std::max (peak, ex.continuous (0));
        }
        return peak;
    };
    const double bed = 0.3 * (0.5 + 0.5);
    CHECK (excitationAfterJump (false) > 2.0 * bed);   // the swell it prevents
    CHECK_THAT (excitationAfterJump (true), Catch::Matchers::WithinAbs (bed, 1.0e-6));
}
