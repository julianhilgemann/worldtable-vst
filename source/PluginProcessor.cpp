#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace worldtable
{

PluginProcessor::PluginProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, kStateType, juce::AudioProcessorValueTreeState::ParameterLayout {})
{
    apvts.state.setProperty (kStateVersionId, kStateVersion, nullptr);
}

juce::ValueTree PluginProcessor::migrateState (juce::ValueTree state)
{
    if (! state.isValid() || ! state.hasType (kStateType))
        return {};

    const int version = state.getProperty (kStateVersionId, 0);

    if (version < 1 || version > kStateVersion)
        return {};

    // Future upgrades go here, one step per version, e.g.
    //   if (version == 1) { ...rewrite tree to v2...; version = 2; }

    state.setProperty (kStateVersionId, kStateVersion, nullptr);
    return state;
}

//==============================================================================
void PluginProcessor::prepareToPlay (double, int) {}

void PluginProcessor::releaseResources() {}

bool PluginProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet().isDisabled();
}

void PluginProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midi);

    buffer.clear(); // M0: silence
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
