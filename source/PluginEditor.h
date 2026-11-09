#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "dsp/Instrument.h"
#include "sky/SkySnapshot.h"

#include <array>
#include <memory>
#include <vector>

namespace worldtable
{

class PluginProcessor;

/** WebView editor: the HTML/JS UI in source/ui/ is served from memory (or from
    $WORLDTABLE_UI_DIR on disk for live editing). Every APVTS parameter is bound
    through a WebSliderRelay named after its ID; telemetry (the sky, the chord,
    meters and the analyzer) is pushed as events at ~30 Hz, with the dome's node arrays; the
    loaded day's chart goes once per page load as a "day" event (T6.1). */
class PluginEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    static constexpr int kBaseWidth = 1350;   // T6.5: one page, 3:2 like before
    static constexpr int kBaseHeight = 900;

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
    juce::var makeChordTelemetry();
    juce::var makeDomeTelemetry() const;
    juce::var makeDayEvent() const;
    void updateAnalyzer();

    PluginProcessor& wtProcessor;

    std::vector<std::unique_ptr<juce::WebSliderRelay>> relays;
    std::unique_ptr<juce::WebBrowserComponent> browser;
    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> attachments;

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
    uint64_t daySent = 0;   ///< DayChart::sequence last sent to the page

    // the chord readout hand-off (single reader: this thread)
    dsp::ChordState chordFrame {};
    bool haveChord = false;

    // the played notes (T3.7, single reader: this thread)
    dsp::PlayState playFrame {};
    juce::var makePlayTelemetry();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace worldtable
