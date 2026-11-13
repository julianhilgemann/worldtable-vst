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
    // so nothing has to be rewritten; unknown IDs simply don't bind.
    // v2 → v3 (T3.8): register_semis, spread and low_octave gave way to the band edges, tones and
    // shifts. The old three no longer bind; the new parameters are absent from a v2 state, so they
    // load at their defaults (the default bands).
    // v3 → v4 (T3.9): flt_type, flt_cutoff, flt_reso, flt_sky and drive gave way to the EQ (eq1_freq
    // … eq_sky). The old five no longer bind; the EQ's parameters are absent from a v3 state, so it
    // loads flat with the cut open and the default drives. Future upgrades go here.
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
