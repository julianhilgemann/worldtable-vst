#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace worldtable
{

namespace
{
const params::ParamPointers& bound (params::ParamPointers& pp, juce::AudioProcessorValueTreeState& apvts)
{
    pp.bind (apvts);
    return pp;
}
} // namespace

PluginProcessor::PluginProcessor (bool networkEnabled)
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, kStateType, params::createLayout()),
      feed (tables, bound (paramPointers, apvts), networkEnabled),
      engine (paramPointers, tables)
{
    apvts.state.setProperty (kStateVersionId, kStateVersion, nullptr);

    // A table exists before the first block: built synchronously from cache/factory data.
    feed.prime();
}

PluginProcessor::~PluginProcessor() = default;

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
void PluginProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    // Rebuilds synchronously if restored state changed the source or transform,
    // then lets the data thread run (fetching, live rebuilds).
    feed.start();

    engine.prepare (sampleRate, maximumExpectedSamplesPerBlock);
    setLatencySamples (engine.getLatencySamples());
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

    dsp::HostTime host;
    if (auto* playHead = getPlayHead())
    {
        if (const auto pos = playHead->getPosition())
        {
            if (const auto bpm = pos->getBpm())
                host.bpm = *bpm;
            host.isPlaying = pos->getIsPlaying();
            if (const auto ppq = pos->getPpqPosition())
            {
                host.hasPpq = true;
                host.ppq = *ppq;
            }
        }
    }

    engine.process (buffer, midi, host);
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
