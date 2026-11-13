#pragma once

#include "dsp/Instrument.h"
#include "params/Params.h"
#include "sky/SkyFeed.h"
#include "sky/SkyRuntime.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace worldtable
{

/** Worldtable: the Sky Model playing a magnetosphere drone (spec/05), with the chord hold, the
    spec's FX chain and the MIDI latch (T3.6). The model runs on the audio clock (T2.4). */
class PluginProcessor final : public juce::AudioProcessor
{
public:
    /** Bump when the saved-state layout changes; add a step to migrateState(). */
    static constexpr int kStateVersion = 4;

    static inline const juce::Identifier kStateType      { "Worldtable" };
    static inline const juce::Identifier kStateVersionId { "stateVersion" };

    /** Upgrades a saved state tree to kStateVersion.
        Returns an invalid tree if the state is unusable (wrong type, missing,
        or newer than this build) — callers must then keep their current state. */
    static juce::ValueTree migrateState (juce::ValueTree state);

    /** networkEnabled is unused until M5 (live data); the bundled Journey is the only source now. */
    explicit PluginProcessor (bool networkEnabled = true);
    ~PluginProcessor() override;

    //== AudioProcessor ========================================================
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Worldtable"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }
    dsp::Instrument& getInstrument() noexcept                        { return instrument; }
    sky::SkyRuntime& getSky() noexcept                               { return skyRuntime; }
    const sky::SkyFeed& getSkyFeed() const noexcept                  { return skyFeed; }

private:
    juce::AudioProcessorValueTreeState apvts;
    params::ParamPointers paramPointers;
    sky::SkyRuntime skyRuntime;
    sky::SkyFeed skyFeed { skyRuntime };
    dsp::Instrument instrument;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};

} // namespace worldtable
