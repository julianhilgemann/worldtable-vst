#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "sky/SkySnapshot.h"

#include <array>
#include <memory>
#include <vector>

namespace worldtable
{

class PluginProcessor;

/** WebView editor: the HTML/JS UI in source/ui/ is served from memory (or from
    $WORLDTABLE_UI_DIR on disk for live editing). Every APVTS parameter is bound
    through a WebSliderRelay named after its ID; telemetry, analyzer and table data
    are pushed as events at ~30 Hz. */
class PluginEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    static constexpr int kBaseWidth = 1200;
    static constexpr int kBaseHeight = 800;

    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void onPageReady();
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url) const;
    juce::var makeTelemetry();
    juce::var makeSkyTelemetry();
    juce::var makeTableMessage() const;
    juce::var makeStatus() const;
    void updateAnalyzer();

    PluginProcessor& wtProcessor;

    std::vector<std::unique_ptr<juce::WebSliderRelay>> relays;
    std::unique_ptr<juce::WebBrowserComponent> browser;
    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> attachments;

    uint64_t sentGeneration = 0;
    int statusCountdown = 0;
    std::atomic<bool> tableRequested { true };
    std::atomic<bool> pageReady { false };

    // analyzer (message thread only)
    static constexpr int kFftOrder = 12;
    static constexpr int kFftSize = 1 << kFftOrder;
    static constexpr int kBands = 120;
    juce::dsp::FFT fft { kFftOrder };
    std::vector<float> history, fftData, window;
    int historyPos = 0;
    std::array<float, kBands> bands {};
    float meterL = 0.0f, meterR = 0.0f;

    // the Sky Model's UI reader (message thread only)
    sky::SkySnapshot skyFrame {};
    bool haveSkyFrame = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace worldtable
