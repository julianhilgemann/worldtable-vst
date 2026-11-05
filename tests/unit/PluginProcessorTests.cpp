#include "PluginProcessor.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

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

/** Renders `seconds` of audio; `midiAt` optionally injects MIDI into the first block. */
RenderStats render (PluginProcessor& p, double sampleRate, double seconds,
                    const juce::MidiBuffer& firstBlockMidi = {}, int blockSize = 512)
{
    p.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi (firstBlockMidi);
    RenderStats s;
    double sumSq = 0.0;
    long count = 0;
    float prev[2] {};

    const auto blocks = static_cast<int> (seconds * sampleRate / blockSize);
    for (int b = 0; b < blocks; ++b)
    {
        // Hosts may hand us garbage in the output buffer; we must overwrite it.
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.5f, blockSize);

        p.processBlock (buffer, midi);
        midi.clear();

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* d = buffer.getReadPointer (ch);
            for (int i = 0; i < blockSize; ++i)
            {
                if (! std::isfinite (d[i]))
                {
                    ++s.nonFinite;
                    continue;
                }
                s.peak = std::max (s.peak, std::abs (d[i]));
                s.maxStep = std::max (s.maxStep, std::abs (d[i] - prev[ch]));
                prev[ch] = d[i];
                sumSq += static_cast<double> (d[i]) * d[i];
                ++count;
            }
        }
    }

    p.releaseResources();
    s.rms = count > 0 ? std::sqrt (sumSq / static_cast<double> (count)) : 0.0;
    return s;
}

constexpr float kCeiling = 0.8913f * 1.0005f;   // −1 dBFS default ceiling (+ rounding)
} // namespace

TEST_CASE ("Default drone plays the factory ocean without MIDI", "[processor][render]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    const double sampleRate = GENERATE (44100.0, 48000.0, 96000.0);
    PluginProcessor p (false);
    const auto s = render (p, sampleRate, 6.0);

    INFO ("sample rate " << sampleRate << ", peak " << s.peak << ", rms " << s.rms);
    CHECK (s.nonFinite == 0);
    CHECK (s.peak <= kCeiling);
    CHECK (s.rms > 0.01);           // clearly audible after the 1.5 s attack
    CHECK (s.maxStep < 0.25f);      // NoClicks: no sample-to-sample jumps
}

TEST_CASE ("Euclid mode triggers notes; MIDI mode is silent without MIDI", "[processor][render]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    SECTION ("euclid")
    {
        PluginProcessor p (false);
        setParam (p, "trig_mode", 1.0f);
        setParam (p, "amp_a", 0.005f);
        const auto s = render (p, 48000.0, 4.0);
        CHECK (s.nonFinite == 0);
        CHECK (s.rms > 0.005);
        CHECK (s.peak <= kCeiling);
        CHECK (p.getEngine().getTelemetry().euclidHits.load() >= 5);
    }

    SECTION ("midi mode, no notes")
    {
        PluginProcessor p (false);
        setParam (p, "trig_mode", 2.0f);
        const auto s = render (p, 48000.0, 2.0);
        INFO ("peak " << std::to_string (static_cast<double> (s.peak)));
        CHECK (s.peak < 1.0e-30f);
    }

    SECTION ("midi mode, one note")
    {
        PluginProcessor p (false);
        setParam (p, "trig_mode", 2.0f);
        setParam (p, "amp_a", 0.01f);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 0);
        const auto s = render (p, 48000.0, 2.0, midi);
        CHECK (s.rms > 0.01);
        CHECK (s.peak <= kCeiling);
    }
}

TEST_CASE ("Every source renders cleanly", "[processor][render]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    const int source = GENERATE (range (0, 6));
    PluginProcessor p (false);
    setParam (p, "wt_source", static_cast<float> (source));
    setParam (p, "amp_a", 0.2f);
    const auto s = render (p, 48000.0, 3.0);

    INFO ("source " << source);
    CHECK (s.nonFinite == 0);
    CHECK (s.rms > 0.01);
    CHECK (s.peak <= kCeiling);
    REQUIRE (p.getWorldFeed().getTableInfo() != nullptr);
    CHECK (p.getWorldFeed().getTableInfo()->sourceIndex == source);
}

TEST_CASE ("Extreme settings stay finite and under the ceiling", "[processor][render]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    PluginProcessor p (false);
    setParam (p, "trig_mode", 1.0f);
    setParam (p, "eu_pulses", 16.0f);
    setParam (p, "eu_rate", 5.0f);          // 1/32
    setParam (p, "osc_drive", 1.0f);
    setParam (p, "osc_unison", 6.0f);
    setParam (p, "osc_level", 6.0f);
    setParam (p, "flt_reso", 1.0f);
    setParam (p, "flt_cutoff", 20000.0f);
    setParam (p, "flt_env", 1.0f);
    setParam (p, "amp_a", 0.001f);
    setParam (p, "amp_r", 30.0f);
    setParam (p, "lfo1_dest", 2.0f);        // pitch
    setParam (p, "lfo1_amt", 1.0f);
    setParam (p, "lfo1_rate", 20.0f);
    setParam (p, "dly_fb", 0.95f);
    setParam (p, "dly_mix", 1.0f);
    setParam (p, "rev_decay", 30.0f);
    setParam (p, "rev_shimmer", 1.0f);
    setParam (p, "rev_mix", 1.0f);
    setParam (p, "lim_gain", 18.0f);

    const auto s = render (p, 44100.0, 12.0, {}, 333);
    CHECK (s.nonFinite == 0);
    CHECK (s.peak <= kCeiling);
}

TEST_CASE ("State round-trips and carries stateVersion", "[state]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    PluginProcessor a (false);
    a.getValueTreeState().state.setProperty ("roundTripMarker", 42, nullptr);
    setParam (a, "flt_cutoff", 777.0f);

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
    CHECK (b.getValueTreeState().getRawParameterValue ("flt_cutoff")->load() == Catch::Approx (777.0f).epsilon (0.001));
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
