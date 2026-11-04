#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace worldtable
{

class PluginProcessor;

/** M0 placeholder. Replaced by the WebView editor in T5.1 (see ADR-0004). */
class PluginEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace worldtable
