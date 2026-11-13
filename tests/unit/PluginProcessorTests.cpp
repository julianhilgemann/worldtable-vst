#include "AllocationCounter.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <functional>
#include <optional>
#include <string>
#include <vector>

using worldtable::PluginProcessor;
using worldtable::JourneyChange;
namespace sky = worldtable::sky;

namespace
{
juce::MemoryBlock toBinary (const juce::ValueTree& tree)
{
    juce::MemoryBlock blob;
    if (auto xml = tree.createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, blob);
    return blob;
}

void loadState (PluginProcessor& p, const juce::MemoryBlock& blob)
{
    p.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
}

void setParam (PluginProcessor& p, const char* id, float realValue)
{
    auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.getValueTreeState().getParameter (id));
    REQUIRE (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

struct RenderStats
{
    float peak = 0.0f;
    double rms = 0.0;
    float maxStep = 0.0f;
    int nonFinite = 0;
};

/** Prepares the processor once and renders in segments; MIDI can be injected per block. */
class Renderer
{
public:
    Renderer (PluginProcessor& processor, double sampleRate, int blockSize)
        : p (processor), buffer (2, blockSize)
    {
        p.setPlayConfigDetails (0, 2, sampleRate, blockSize);
        p.prepareToPlay (sampleRate, blockSize);
    }

    RenderStats run (double seconds, const std::function<void (int, juce::MidiBuffer&)>& eachBlock = {})
    {
        RenderStats s;
        const auto block = buffer.getNumSamples();
        const auto blocks = static_cast<int> (seconds * p.getSampleRate() / block);
        double sumSq = 0.0;
        long count = 0;
        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();
            juce::MidiBuffer midi;
            if (eachBlock)
                eachBlock (b, midi);
            p.processBlock (buffer, midi);

            for (int ch = 0; ch < 2; ++ch)
            {
                const float* d = buffer.getReadPointer (ch);
                for (int i = 0; i < block; ++i)
                {
                    if (! std::isfinite (d[i]))
                    {
                        ++s.nonFinite;
                        continue;
                    }
                    s.peak = std::max (s.peak, std::abs (d[i]));
                    sumSq += static_cast<double> (d[i]) * d[i];
                    ++count;
                }
            }
        }
        s.rms = count > 0 ? std::sqrt (sumSq / static_cast<double> (count)) : 0.0;
        return s;
    }

    /** The newest chord the instrument published (the editor's path). */
    const worldtable::dsp::ChordState& chord()
    {
        worldtable::dsp::ChordState s;
        if (p.getInstrument().readChord (s))
        {
            lastChord = s;
            haveChord = true;
        }
        return lastChord;
    }

    bool haveChord = false;
    worldtable::dsp::ChordState lastChord {};
    juce::String label() { return juce::String::fromUTF8 (chord().label.data()); }

private:
    PluginProcessor& p;
    juce::AudioBuffer<float> buffer;
};

constexpr float kCeiling = 0.8913f * 1.0005f;   // −1 dBFS default ceiling (+ rounding)
} // namespace

TEST_CASE ("The default instrument plays the storm Journey without MIDI", "[processor][render]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    const double sampleRate = GENERATE (44100.0, 48000.0, 96000.0);
    PluginProcessor p (false);
    Renderer r (p, sampleRate, 512);
    const auto s = r.run (6.0);

    INFO ("sample rate " << sampleRate << ", peak " << s.peak << ", rms " << s.rms);
    CHECK (s.nonFinite == 0);
    CHECK (s.peak <= kCeiling);
    CHECK (s.rms > 0.005);            // the sky sound, not silence (spec/05 §11)
    CHECK (r.chord().label[0] != '\0');
}

TEST_CASE ("The chord readout follows the UI chord hold", "[processor][chord]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    PluginProcessor p (false);
    setParam (p, "progression", 0.0f);   // hold one chord
    setParam (p, "key", 0.0f);           // C
    setParam (p, "mood", 0.0f);          // bright home → Cmaj7
    Renderer r (p, 48000.0, 512);
    r.run (0.5);
    INFO ("label " << r.label());
    CHECK (r.label() == "Cmaj7");
    CHECK_FALSE (r.chord().latched);

    setParam (p, "key", 5.0f);           // F
    r.run (0.5);
    CHECK (r.label() == "Fmaj7");
}

TEST_CASE ("MIDI latches the chord and the sky resumes on release", "[processor][midi]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    PluginProcessor p (false);
    setParam (p, "progression", 0.0f);   // the sky holds Dmaj7; the latch must still win
    Renderer r (p, 48000.0, 512);
    r.run (0.5);
    REQUIRE (r.label() == "Dmaj7");

    juce::MidiBuffer down;
    down.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
    down.addEvent (juce::MidiMessage::noteOn (1, 64, 0.9f), 0);
    down.addEvent (juce::MidiMessage::noteOn (1, 67, 0.9f), 0);
    const auto s = r.run (1.0, [&] (int b, juce::MidiBuffer& midi)
    {
        if (b == 0)
            midi = down;
    });
    CHECK (s.nonFinite == 0);
    CHECK (r.chord().latched);
    CHECK (r.label() == "Cmaj");

    // A sound control does not touch the latched chord; the readout stays.
    setParam (p, "rev_mix", 0.6f);
    r.run (0.3);
    CHECK (r.label() == "Cmaj");
    CHECK (r.chord().latched);

    juce::MidiBuffer up;
    up.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    up.addEvent (juce::MidiMessage::noteOff (1, 64), 0);
    up.addEvent (juce::MidiMessage::noteOff (1, 67), 0);
    const auto changesBefore = r.chord().changes;
    // The urgent request damps the old bank for 3 s before the chord can land (SkySound).
    const auto released = r.run (5.0, [&] (int b, juce::MidiBuffer& midi)
    {
        if (b == 0)
            midi = up;
    });
    CHECK (released.nonFinite == 0);
    CHECK_FALSE (r.chord().latched);
    CHECK (r.chord().changes > changesBefore);
    CHECK (r.label() == "Dmaj7");
}

TEST_CASE ("Block size does not change the sound", "[processor][determinism]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    const double sampleRate = 48000.0;
    const int blockA = GENERATE (256, 1000);
    PluginProcessor a (false), b (false);
    a.setPlayConfigDetails (0, 2, sampleRate, blockA);
    a.prepareToPlay (sampleRate, blockA);
    b.setPlayConfigDetails (0, 2, sampleRate, 512);
    b.prepareToPlay (sampleRate, 512);

    const auto total = static_cast<int> (3.0 * sampleRate);
    const auto renderInto = [] (PluginProcessor& proc, int block, std::vector<float>& out, int frames)
    {
        juce::AudioBuffer<float> buffer (2, block);
        for (int done = 0; done < frames; done += block)
        {
            const int n = std::min (block, frames - done);
            buffer.setSize (2, n, false, false, true);
            buffer.clear();
            juce::MidiBuffer midi;
            proc.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + n,
                           out.begin() + ch * frames + done);
        }
    };

    std::vector<float> outA (static_cast<size_t> (2 * total)), outB (static_cast<size_t> (2 * total));
    renderInto (a, blockA, outA, total);
    renderInto (b, 512, outB, total);

    double maxDiff = 0.0, sumA = 0.0, sumB = 0.0;
    for (size_t i = 0; i < outA.size(); ++i)
    {
        maxDiff = std::max (maxDiff, std::abs (static_cast<double> (outA[i] - outB[i])));
        sumA += static_cast<double> (outA[i]) * outA[i];
        sumB += static_cast<double> (outB[i]) * outB[i];
    }
    INFO ("block " << blockA << " vs 512: max sample difference " << maxDiff);
    CHECK (maxDiff < 1.0e-5);
    CHECK (sumA == Catch::Approx (sumB).epsilon (1.0e-6));
}

TEST_CASE ("processBlock allocates nothing", "[processor][realtime]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    PluginProcessor p (false);
    Renderer r (p, 48000.0, 512);
    r.run (2.0);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    const auto before = testing::allocationsOnThisThread();
    for (int b = 0; b < 200; ++b)
    {
        buffer.clear();
        midi.clear();
        p.processBlock (buffer, midi);
    }
    CHECK (testing::allocationsOnThisThread() == before);
}

TEST_CASE ("Extreme settings stay finite and under the ceiling", "[processor][render]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    PluginProcessor p (false);
    for (const char* id : { "eq1_drive", "eq2_drive", "eq3_drive", "eq4_drive" })   // T3.9
        setParam (p, id, 1.0f);
    setParam (p, "eq1_gain", 12.0f);
    setParam (p, "eq3_gain", 12.0f);
    setParam (p, "ph_mix", 1.0f);
    setParam (p, "ph_feedback", 0.8f);
    setParam (p, "ph_depth", 1.0f);
    setParam (p, "ch_mix", 1.0f);
    setParam (p, "dly_fb", 0.95f);
    setParam (p, "dly_mix", 1.0f);
    setParam (p, "rev_decay", 60.0f);
    setParam (p, "rev_shimmer", 1.0f);
    setParam (p, "rev_mix", 1.0f);
    setParam (p, "red_level", 1.0f);
    setParam (p, "violet_level", 1.0f);
    setParam (p, "macro_glow", 1.0f);
    setParam (p, "macro_space", 1.0f);
    setParam (p, "master", 0.0f);
    setParam (p, "lim_ceiling", -12.0f);

    Renderer r (p, 44100.0, 333);
    const auto s = r.run (8.0);
    CHECK (s.nonFinite == 0);
    CHECK (s.peak <= 0.2512f * 1.0005f);   // −12 dBFS
}

TEST_CASE ("Parameters change the sound", "[processor][params]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    const auto rmsWith = [] (const char* id, float value)
    {
        PluginProcessor p (false);
        setParam (p, id, value);
        Renderer r (p, 48000.0, 512);
        return r.run (4.0).rms;
    };

    const auto quiet = rmsWith ("master", -30.0f);
    const auto loud = rmsWith ("master", 0.0f);
    CHECK (loud > quiet * 8.0);

    const auto noStorm = rmsWith ("macro_storm", 0.0f);
    const auto storm = rmsWith ("macro_storm", 1.0f);
    CHECK (storm > noStorm * 1.2);
}

TEST_CASE ("State round-trips and carries stateVersion", "[state]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    PluginProcessor a (false);
    a.getValueTreeState().state.setProperty ("roundTripMarker", 42, nullptr);
    setParam (a, "rev_mix", 0.77f);
    setParam (a, "mood", 2.0f);

    juce::MemoryBlock blob;
    a.getStateInformation (blob);
    REQUIRE (blob.getSize() > 0);

    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), static_cast<int> (blob.getSize()));
    REQUIRE (xml != nullptr);
    const auto saved = juce::ValueTree::fromXml (*xml);
    CHECK (saved.hasType (PluginProcessor::kStateType));
    CHECK (static_cast<int> (saved.getProperty (PluginProcessor::kStateVersionId)) == PluginProcessor::kStateVersion);

    PluginProcessor b (false);
    loadState (b, blob);
    CHECK (static_cast<int> (b.getValueTreeState().state.getProperty ("roundTripMarker")) == 42);
    CHECK (b.getValueTreeState().getRawParameterValue ("rev_mix")->load() == Catch::Approx (0.77f).epsilon (0.001));
    CHECK (b.getValueTreeState().getRawParameterValue ("mood")->load() == Catch::Approx (2.0f));
}

TEST_CASE ("A POC (v1) state loads without its retired parameters", "[state][migration]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    PluginProcessor p (false);
    auto old = p.getValueTreeState().copyState();
    old.setProperty (PluginProcessor::kStateVersionId, 1, nullptr);
    old.setProperty ("trig_mode", 2, nullptr);     // a POC parameter that no longer exists
    old.setProperty ("flt_cutoff", 900.0, nullptr);

    loadState (p, toBinary (old));
    CHECK (p.getValueTreeState().getRawParameterValue ("progression")->load() == Catch::Approx (1.0f));
}

TEST_CASE ("Unusable state is ignored without crashing", "[state]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    PluginProcessor p (false);
    const auto before = p.getValueTreeState().copyState();

    SECTION ("empty")
    {
        p.setStateInformation (nullptr, 0);
    }

    SECTION ("random bytes")
    {
        juce::MemoryBlock junk (256);
        juce::Random (1234).fillBitsRandomly (junk.getData(), junk.getSize());
        loadState (p, junk);
    }

    SECTION ("newer stateVersion than this build")
    {
        auto future = before.createCopy();
        future.setProperty (PluginProcessor::kStateVersionId, PluginProcessor::kStateVersion + 1, nullptr);
        future.setProperty ("roundTripMarker", 7, nullptr);
        loadState (p, toBinary (future));
    }

    SECTION ("wrong root type")
    {
        juce::ValueTree other ("SomethingElse");
        other.setProperty (PluginProcessor::kStateVersionId, PluginProcessor::kStateVersion, nullptr);
        loadState (p, toBinary (other));
    }

    CHECK (p.getValueTreeState().copyState().isEquivalentTo (before));
}

TEST_CASE ("migrateState accepts only known versions", "[state]")
{
    juce::ValueTree current (PluginProcessor::kStateType);
    current.setProperty (PluginProcessor::kStateVersionId, PluginProcessor::kStateVersion, nullptr);

    CHECK (PluginProcessor::migrateState (current).isValid());
    CHECK_FALSE (PluginProcessor::migrateState ({}).isValid());
    CHECK_FALSE (PluginProcessor::migrateState (juce::ValueTree (PluginProcessor::kStateType)).isValid()); // no version
    CHECK_FALSE (PluginProcessor::migrateState (juce::ValueTree ("Other")).isValid());
}

//===========================================================================================}

// T6.9: the Journey transport — saved with the session, run/freeze, speeds, day, loop
namespace
{
constexpr double kStormStartUtc = 1715299200.0;   // 2024-05-10T00:00:00Z

JourneyChange change (std::optional<bool> frozen, std::optional<double> speed, std::optional<bool> loop,
                      std::optional<juce::String> day)
{
    return { frozen, speed, loop, day };
}

juce::ValueTree savedJourney (PluginProcessor& p)
{
    juce::MemoryBlock blob;
    p.getStateInformation (blob);
    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), static_cast<int> (blob.getSize()));
    REQUIRE (xml != nullptr);
    return juce::ValueTree::fromXml (*xml).getChildWithName (PluginProcessor::kJourneyType);
}
} // namespace

TEST_CASE ("The transport runs, freezes, snaps speeds, and runs again after the end", "[processor][journey]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    PluginProcessor p (false);
    Renderer r (p, 48000.0, 512);
    r.run (0.5);

    CHECK (p.getJourney().speed == 24.0);
    CHECK (p.setJourney (change ({}, 50.0, {}, {})).speed == 60.0);        // off the table: the nearest
    CHECK (p.setJourney (change ({}, 1e9, {}, {})).speed == 1440.0);
    p.setJourney (change ({}, 240.0, {}, {}));
    r.run (0.2);
    CHECK (p.getSky().model().speed() == 240.0);

    p.setJourney (change (true, {}, {}, {}));
    r.run (0.2);
    const double held = p.getSky().model().modelTimeUtc();
    r.run (0.5);
    CHECK (p.getSky().model().speed() == 0.0);
    CHECK_THAT (p.getSky().model().modelTimeUtc(), Catch::Matchers::WithinULP (held, 0));   // Freeze: model time stops
    CHECK (p.getJourney().speed == 240.0);                               // the running speed is kept
    p.setJourney (change (false, {}, {}, {}));
    r.run (0.2);
    CHECK (p.getSky().model().speed() == 240.0);

    // to the end of the recording: it holds and says so; Run starts it again from the beginning
    p.getSky().seek (p.getSkyFeed().dayEndUtc() - 30.0);
    p.setJourney (change ({}, 1440.0, false, {}));
    r.run (1.0);
    CHECK (p.journeyEnded());
    CHECK (p.getSky().model().modelTimeUtc() >= p.getSkyFeed().dayEndUtc() - 1.0);
    p.setJourney (change (false, {}, {}, {}));
    r.run (0.1);
    CHECK_FALSE (p.journeyEnded());
    CHECK (p.getSky().model().modelTimeUtc() < p.getSkyFeed().dayStartUtc() + 3600.0);

    // an unknown day changes nothing
    CHECK (p.setJourney (change ({}, {}, {}, juce::String ("1999-01-01"))).day == "2024-05-10");
}

TEST_CASE ("The Journey round-trips with the session: day, speed, frozen, loop, model time", "[state][journey]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    PluginProcessor a (false);
    {
        Renderer r (a, 48000.0, 512);
        r.run (0.5);
        a.setJourney (change ({}, 240.0, true, juce::String ("2025-01-25")));
        r.run (2.0);
        a.setJourney (change (true, {}, {}, {}));
        r.run (0.5);
    }
    const double utc = a.getSky().model().modelTimeUtc();
    CHECK (utc > a.getSkyFeed().dayStartUtc() + sky::SkyFeed::journeyStart ("2025-01-25"));   // it moved on

    juce::MemoryBlock blob;
    a.getStateInformation (blob);

    PluginProcessor b (false);
    loadState (b, blob);
    const auto j = b.getJourney();   // before any audio: the restored state
    CHECK (j.day == "2025-01-25");
    CHECK (j.speed == 240.0);
    CHECK (j.frozen);
    CHECK (j.loop);
    CHECK (std::abs (j.utc - utc) <= 1.0);
    CHECK (b.getSkyFeed().dayId() == "2025-01-25");

    Renderer r (b, 48000.0, 512);
    r.run (1.0);
    CHECK (std::abs (b.getSky().model().modelTimeUtc() - utc) <= 1.0);   // frozen there
    CHECK (b.getSky().model().speed() == 0.0);

    const auto saved = savedJourney (b);
    CHECK (saved.getProperty ("day").toString() == "2025-01-25");
    CHECK (static_cast<double> (saved.getProperty ("speed")) == 240.0);
    CHECK (static_cast<bool> (saved.getProperty ("frozen")));
    CHECK (static_cast<bool> (saved.getProperty ("loop")));
}

TEST_CASE ("An older state (no journey child) loads with the Journey defaults", "[state][migration][journey]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    const int version = GENERATE (3, 4);
    PluginProcessor p (false);
    p.setJourney (change (true, 1.0, true, juce::String ("2025-01-19")));

    auto old = p.getValueTreeState().copyState();
    old.setProperty (PluginProcessor::kStateVersionId, version, nullptr);
    REQUIRE_FALSE (old.getChildWithName (PluginProcessor::kJourneyType).isValid());
    loadState (p, toBinary (old));

    const auto j = p.getJourney();
    CHECK (j.day == "2024-05-10");
    CHECK (j.speed == 24.0);
    CHECK_FALSE (j.frozen);
    CHECK_FALSE (j.loop);
    CHECK_THAT (j.utc, Catch::Matchers::WithinULP (kStormStartUtc + sky::SkyFeed::kDefaultJourneyStart, 0));
    CHECK (p.getSkyFeed().dayId() == "2024-05-10");
}

TEST_CASE ("A day change and a loop wrap mid-play: no click, no swell, no allocation, the same at two block sizes", "[processor][journey][render]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    constexpr double kRate = 48000.0, kSeconds = 15.0;
    constexpr int kGrid = 4800;   // changes land on a 0.1 s grid, so both block sizes see them on the same sample
    constexpr int kDaySwitch = 30 * kGrid, kToEnd = 70 * kGrid;   // 3 s, 7 s

    struct Out { std::vector<float> audio; int wrapAt = -1; int64_t allocations = 0; };
    const auto run = [&] (int block, bool journey)
    {
        PluginProcessor p (false);
        p.setPlayConfigDetails (0, 2, kRate, block);
        p.prepareToPlay (kRate, block);
        const int total = static_cast<int> (kSeconds * kRate);
        Out out;
        out.audio.resize (static_cast<size_t> (2 * total));
        juce::AudioBuffer<float> buffer (2, block);
        double lastUtc = 0.0;
        for (int done = 0; done < total;)
        {
            if (journey && done == kDaySwitch)
                p.setJourney (change ({}, {}, {}, juce::String ("2025-01-19")));   // lands at 09:30 UT
            if (journey && done == kToEnd)
            {
                p.getSky().seek (p.getSkyFeed().dayEndUtc() - 7200.0);   // 2 h before the end at 1440×: wraps 5 s later
                p.setJourney (change ({}, 1440.0, true, {}));
            }
            const int n = std::min ({ block, total - done, kGrid - done % kGrid });
            buffer.setSize (2, n, false, false, true);
            buffer.clear();
            juce::MidiBuffer midi;
            const auto before = testing::allocationsOnThisThread();
            p.processBlock (buffer, midi);
            out.allocations += testing::allocationsOnThisThread() - before;
            for (int ch = 0; ch < 2; ++ch)
                std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + n, out.audio.begin() + ch * total + done);
            const double utc = p.getSky().model().modelTimeUtc();
            if (done > kToEnd && utc < lastUtc - 3600.0 && out.wrapAt < 0)
                out.wrapAt = done;
            lastUtc = utc;
            done += n;
        }
        return out;
    };

    const auto a = run (512, true);
    const auto b = run (333, true);
    const auto still = run (512, false);   // the default Journey, untouched
    REQUIRE (a.wrapAt > kToEnd);
    CHECK (std::abs (a.wrapAt - b.wrapAt) < 512);   // found after the block it happened in
    CHECK (a.allocations == 0);
    CHECK (b.allocations == 0);

    int nonFinite = 0;
    float peak = 0.0f;
    for (const float x : a.audio)
    {
        nonFinite += std::isfinite (x) ? 0 : 1;
        peak = std::max (peak, std::abs (x));
    }
    CHECK (nonFinite == 0);
    CHECK (peak <= kCeiling);

    double maxDiff = 0.0;
    for (size_t i = 0; i < a.audio.size(); ++i)
        maxDiff = std::max (maxDiff, static_cast<double> (std::abs (a.audio[i] - b.audio[i])));
    CHECK (maxDiff == 0.0);

    const size_t total = a.audio.size() / 2;
    const auto maxStepIn = [] (const std::vector<float>& z, size_t from, size_t to)
    {
        float m = 0.0f;
        for (size_t i = std::max<size_t> (from, 1); i < std::min (to, z.size()); ++i)
            m = std::max (m, std::abs (z[i] - z[i - 1]));
        return m;
    };
    const auto rmsIn = [] (const std::vector<float>& z, size_t from, size_t to)
    {
        double s = 0.0;
        for (size_t i = from; i < to; ++i)
            s += static_cast<double> (z[i]) * z[i];
        return std::sqrt (s / static_cast<double> (to - from));
    };
    const auto outlier = [&] (const std::vector<float>& z)   // the worst 10 ms window against the median of ±0.5 s
    {
        std::vector<float> windows;
        for (size_t w = 0; w + 480 <= total; w += 480)
            windows.push_back (maxStepIn (z, w, w + 481));
        float worst = 0.0f;
        for (size_t w = 50; w + 50 < windows.size(); ++w)
        {
            std::vector<float> around (windows.begin() + static_cast<long> (w - 50), windows.begin() + static_cast<long> (w + 50));
            std::nth_element (around.begin(), around.begin() + 50, around.end());
            worst = std::max (worst, windows[w] / std::max (around[50], 1.0e-6f));
        }
        return worst;
    };
    for (size_t ch = 0; ch < 2; ++ch)
    {
        const std::vector<float> x (a.audio.begin() + static_cast<long> (ch * total), a.audio.begin() + static_cast<long> ((ch + 1) * total));
        const std::vector<float> y (still.audio.begin() + static_cast<long> (ch * total), still.audio.begin() + static_cast<long> ((ch + 1) * total));
        const float stepped = maxStepIn (x, 0, total), reference = maxStepIn (y, 0, total);
        const float worstRatio = outlier (x), referenceRatio = outlier (y);
        // a swell: the half second after each jump against the half second before it
        const auto half = static_cast<size_t> (kRate / 2);
        const double daySwell = rmsIn (x, kDaySwitch, kDaySwitch + half) / rmsIn (x, kDaySwitch - half, kDaySwitch);
        const auto wrap = static_cast<size_t> (a.wrapAt);
        const double wrapSwell = rmsIn (x, wrap, wrap + half) / rmsIn (x, wrap - half, wrap);
        std::printf ("Journey day switch + loop wrap, channel %d: max step %.4f (untouched %.4f), worst 10 ms window %.2fx "
                     "its neighbourhood (untouched %.2fx), rms after/before: day %.2f, wrap %.2f (at %.2f s), peak %.2f dBFS\n",
                     static_cast<int> (ch), static_cast<double> (stepped), static_cast<double> (reference),
                     static_cast<double> (worstRatio), static_cast<double> (referenceRatio), daySwell, wrapSwell,
                     a.wrapAt / kRate, 20.0 * std::log10 (static_cast<double> (peak)));
        CHECK (stepped < 2.0f * reference);
        CHECK (worstRatio < std::max (3.0f, 1.25f * referenceRatio));   // a click would stand far out
        CHECK (daySwell < 1.5);
        CHECK (wrapSwell < 1.5);
    }
=======
TEST_CASE ("The editor's taps read the sound without changing it", "[processor][telemetry][T6.12]")
{
    // T6.12: the sources before the tone stage feed a second scope, the phaser's LFO phase a
    // telemetry atomic. Both only read: the output is bit-identical with the input tap detached,
    // whether or not anyone drains the scopes.
    juce::ScopedJuceInitialiser_GUI juceEnv;

    PluginProcessor a (false), b (false);
    for (auto* p : { &a, &b })
    {
        setParam (*p, "eq4_freq", 300.0f);     // a closed tone stage: the tap sees what it removes
        setParam (*p, "eq_sky", 0.0f);
        setParam (*p, "ph_rate", 0.25f);
        p->setPlayConfigDetails (0, 2, 48000.0, 512);
        p->prepareToPlay (48000.0, 512);
    }
    b.getInstrument().sources().setInputTap (nullptr);

    juce::AudioBuffer<float> bufA (2, 512), bufB (2, 512);
    juce::MidiBuffer midi;
    std::vector<float> tapped, out, chunk (8192);
    bool identical = true;
    const int blocks = static_cast<int> (4.0 * 48000.0 / 512);
    for (int k = 0; k < blocks; ++k)
    {
        bufA.clear();
        bufB.clear();
        a.processBlock (bufA, midi);
        b.processBlock (bufB, midi);
        for (int ch = 0; ch < 2; ++ch)
            identical = identical && std::equal (bufA.getReadPointer (ch), bufA.getReadPointer (ch) + 512, bufB.getReadPointer (ch));
        if (k % 4 == 0)   // a's scopes drained (the editor at ~23 Hz), b's left to fill up
        {
            for (int n; (n = a.getInstrument().getInputScope().pull (chunk.data(), 8192)) > 0;)
                tapped.insert (tapped.end(), chunk.begin(), chunk.begin() + n);
            for (int n; (n = a.getInstrument().getScope().pull (chunk.data(), 8192)) > 0;)
                out.insert (out.end(), chunk.begin(), chunk.begin() + n);
        }
    }
    CHECK (identical);
    CHECK (b.getInstrument().getInputScope().pull (chunk.data(), 8192) == 0);   // detached: nothing came

    // the tap carries the sources' mix before the 300 Hz high cut: far more top end than the output
    REQUIRE (tapped.size() > 3 * 48000);
    const auto brightness = [] (const std::vector<float>& x)
    {
        double e = 0.0, d = 0.0;
        for (size_t i = 1; i < x.size(); ++i)
        {
            e += static_cast<double> (x[i]) * x[i];
            d += static_cast<double> (x[i] - x[i - 1]) * (x[i] - x[i - 1]);
        }
        return d / std::max (e, 1.0e-30);
    };
    INFO ("brightness before the tone stage " << brightness (tapped) << ", after everything " << brightness (out));
    CHECK (std::all_of (tapped.begin(), tapped.end(), [] (float v) { return std::isfinite (v); }));
    CHECK (brightness (tapped) > 2.0 * brightness (out));   // 3.2× here, deterministic

    // the phaser's phase: 4 s at 0.25 Hz is one turn (the float accumulator drifts a little)
    const float phase = a.getInstrument().getTelemetry().phaserPhase.load();
    INFO ("phase after 4 s at 0.25 Hz: " << phase);
    CHECK ((phase < 0.04f || phase > 0.96f));
}
