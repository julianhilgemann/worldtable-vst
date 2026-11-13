#include "AllocationCounter.h"
#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <functional>
#include <string>

using worldtable::PluginProcessor;

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
