#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

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
} // namespace

TEST_CASE ("Skeleton outputs silence at common sample rates", "[processor]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    const double sampleRate = GENERATE (44100.0, 48000.0, 96000.0);
    constexpr int blockSize = 512;

    PluginProcessor p;
    p.setPlayConfigDetails (0, 2, sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);

    const int numBlocks = static_cast<int> (sampleRate / blockSize); // ~1 s
    for (int block = 0; block < numBlocks; ++block)
    {
        // Hosts may hand us garbage in the output buffer; we must overwrite it.
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 0.5f, blockSize);

        p.processBlock (buffer, midi);
        midi.clear();

        REQUIRE (buffer.getMagnitude (0, blockSize) == 0.0f);
    }

    p.releaseResources();
}

TEST_CASE ("State round-trips and carries stateVersion", "[state]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    PluginProcessor a;
    a.getValueTreeState().state.setProperty ("roundTripMarker", 42, nullptr);

    juce::MemoryBlock blob;
    a.getStateInformation (blob);
    REQUIRE (blob.getSize() > 0);

    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), static_cast<int> (blob.getSize()));
    REQUIRE (xml != nullptr);
    const auto saved = juce::ValueTree::fromXml (*xml);
    CHECK (saved.hasType (PluginProcessor::kStateType));
    CHECK (static_cast<int> (saved.getProperty (PluginProcessor::kStateVersionId)) == PluginProcessor::kStateVersion);

    PluginProcessor b;
    loadState (b, blob);
    CHECK (static_cast<int> (b.getValueTreeState().state.getProperty ("roundTripMarker")) == 42);
    CHECK (b.getValueTreeState().copyState().isEquivalentTo (a.getValueTreeState().copyState()));
}

TEST_CASE ("Unusable state is ignored without crashing", "[state]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    PluginProcessor p;
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
