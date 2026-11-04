#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace worldtable
{

/** M0 skeleton: a MIDI-input synth that outputs silence.
    Parameters arrive with T0.2 (generated from spec/parameters.yaml). */
class PluginProcessor final : public juce::AudioProcessor
{
public:
    /** Bump when the saved-state layout changes; add a step to migrateState(). */
    static constexpr int kStateVersion = 1;

    static inline const juce::Identifier kStateType      { "Worldtable" };
    static inline const juce::Identifier kStateVersionId { "stateVersion" };

    /** Upgrades a saved state tree to kStateVersion.
        Returns an invalid tree if the state is unusable (wrong type, missing,
        or newer than this build) — callers must then keep their current state. */
    static juce::ValueTree migrateState (juce::ValueTree state);

    PluginProcessor();
    ~PluginProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return apvts; }

private:
    juce::AudioProcessorValueTreeState apvts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};

} // namespace worldtable
