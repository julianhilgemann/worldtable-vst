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

/** Per-computer UI settings (T6.13), not session state: `Worldtable.settings` in the user's
    application-data folder (macOS: ~/Library/Application Support/Worldtable/), or the file named
    by $WORLDTABLE_SETTINGS_FILE (the UI check and anyone testing use that, so the real file keeps
    its state). Keys: `uiSize` (0 Small, 1 Medium, 2 Large) and `tourSeen`. Every call reads or
    writes the file, so each editor that opens sees the latest choice. Message thread only. */
class UiSettings
{
public:
    static constexpr int kSmall = 0, kMedium = 1, kLarge = 2, kNumSizes = 3;

    explicit UiSettings (juce::File settingsFile = defaultFile());
    static juce::File defaultFile();
    const juce::File& getFile() const noexcept { return file; }

    /** The saved size (0, 1, 2), or -1 when none was ever chosen. */
    int size() const;
    void setSize (int index);
    bool tourSeen() const;
    void setTourSeen (bool seen);

    /** The window at each size: the same 1350 × 900 page scaled 85 %, 100 %, 125 %. */
    static juce::Point<int> sizeOf (int index);
    /** The size an editor opens at: the saved one; else Medium, or Small when the display's usable
        area is smaller than Medium plus 80 px. */
    static int openingSize (int saved, juce::Rectangle<int> usableArea);

private:
    juce::File file;
};

/** WebView editor: the HTML/JS UI in source/ui/ is served from memory (or from
    $WORLDTABLE_UI_DIR on disk for live editing). Every APVTS parameter is bound
    through a WebSliderRelay named after its ID; telemetry (the sky, the chord,
    meters and the analyzer) is pushed as events at ~30 Hz, with the dome's node arrays; the
    loaded day's chart goes once per page load as a "day" event (T6.1). T6.13: three fixed
    sizes (not freely resizable), chosen in the page (`setUiSize`) and kept in UiSettings. */
class PluginEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    static constexpr int kBaseWidth = 1350;   // T6.5: one page, 3:2 like before (Medium, T6.13)
    static constexpr int kBaseHeight = 900;

    explicit PluginEditor (PluginProcessor&);
    ~PluginEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void onPageReady();
    void applyUiSize (int index);
    juce::var makeUiSettings() const;
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url) const;
    juce::var makeTelemetry();
    juce::var makeSkyTelemetry();
    juce::var makeChordTelemetry();
    juce::var makeDomeTelemetry() const;
    juce::var makeDayEvent() const;
    juce::var makeJourneyTelemetry() const;
    juce::var makeModTelemetry();
    juce::var makeModulationEvent() const;
    void updateAnalyzer();

    PluginProcessor& wtProcessor;

    std::vector<std::unique_ptr<juce::WebSliderRelay>> relays;
    std::unique_ptr<juce::WebBrowserComponent> browser;
    std::vector<std::unique_ptr<juce::WebSliderParameterAttachment>> attachments;

    std::atomic<bool> pageReady { false };

    UiSettings settings;   // T6.13 (message thread only)
    int uiSize = UiSettings::kMedium;

    /** One analyzer (message thread only): a ScopeFifo's mono mix → 120 log bands, 20 Hz … 20 kHz,
        in dB, fast attack and slow fall. The output has one, the sources before the tone stage the
        other (T6.12). */
    struct Analyzer
    {
        static constexpr int kFftOrder = 12;
        static constexpr int kFftSize = 1 << kFftOrder;
        static constexpr int kBands = 120;

        Analyzer();
        void update (dsp::ScopeFifo&, double sampleRate);
        juce::var toVar() const;

        juce::dsp::FFT fft { kFftOrder };
        std::vector<float> history, fftData, window;
        int historyPos = 0;
        std::array<float, kBands> bands {};
    };
    Analyzer outAnalyzer, inAnalyzer;
    float meterL = 0.0f, meterR = 0.0f;

    // the Sky Model's UI reader (message thread only)
    sky::SkySnapshot skyFrame {};
    bool haveSkyFrame = false;
    uint64_t daySent = 0;   ///< DayChart::sequence last sent to the page

    // the chord readout hand-off (single reader: this thread)
    dsp::ChordState chordFrame {};
    bool haveChord = false;

    // the modulation grid (T4.2, single reader: this thread)
    dsp::mod::ModTelemetry modFrame {};
    bool haveMod = false;
    uint32_t modRevisionSent = 0;
    bool modSent = false;   ///< the page has the current grid (cleared when it loads)

    // the played notes (T3.7, single reader: this thread)
    dsp::PlayState playFrame {};
    juce::var makePlayTelemetry();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace worldtable
