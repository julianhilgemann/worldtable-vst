#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace worldtable
{

namespace
{
params::ParamPointers& bound (params::ParamPointers& pp, juce::AudioProcessorValueTreeState& apvts)
{
    pp.bind (apvts);
    return pp;
}
} // namespace

PluginProcessor::PluginProcessor (bool networkEnabled)
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, kStateType, params::createLayout()),
      instrument (bound (paramPointers, apvts), skyRuntime)
{
    juce::ignoreUnused (networkEnabled);   // live data arrives in M5; the bundled Journey plays now

    apvts.state.setProperty (kStateVersionId, kStateVersion, nullptr);

    // The sky's default Journey (M2): the 10–11 May 2024 storm at 24×, built here like the table
    // was in the POC. The instrument picks the track up on prepareToPlay.
    skyFeed.startDefaultJourney();
}

PluginProcessor::~PluginProcessor() = default;

juce::ValueTree PluginProcessor::migrateState (juce::ValueTree state)
{
    if (! state.isValid() || ! state.hasType (kStateType))
        return {};

    const int version = state.getProperty (kStateVersionId, 0);

    if (version < 1 || version > kStateVersion)
        return {};

    // v1 → v2 (T3.6): the POC parameters are gone. Their properties are ignored by the new APVTS,
    // so nothing has to be rewritten; unknown IDs simply don't bind. Future upgrades go here.
    state.setProperty (kStateVersionId, kStateVersion, nullptr);
    return state;
}

//==============================================================================
void PluginProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    skyRuntime.prepare (sampleRate);
    instrument.prepare (sampleRate, maximumExpectedSamplesPerBlock);
    setLatencySamples (instrument.getLatencySamples());
}

void PluginProcessor::releaseResources() {}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet().isDisabled();
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    instrument.process (buffer, midi);
}

//==============================================================================
juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor (*this);
}

//==============================================================================
void PluginProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    if (auto migrated = migrateState (juce::ValueTree::fromXml (*xml)); migrated.isValid())
        apvts.replaceState (migrated);
}

} // namespace worldtable

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new worldtable::PluginProcessor();
}
