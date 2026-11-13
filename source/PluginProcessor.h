#pragma once

#include "data/SkyDays.h"
#include "dsp/Instrument.h"
#include "params/Params.h"
#include "sky/SkyFeed.h"
#include "sky/SkyRuntime.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <limits>
#include <optional>

namespace worldtable
{

/** The Journey as the transport sets it (T6.9), saved with the session as the state's `journey`
    child (spec/03 §State). Not a parameter: hosts don't automate it. */
struct JourneyState
{
    /** The transport's speeds, model seconds per real second (spec/09 §9: the owner may change the
        set; this table is the only copy, the page gets it with the day). */
    static constexpr std::array<double, 5> kSpeeds { 1.0, 24.0, 60.0, 240.0, 1440.0 };

    juce::String day = data::kDefaultSkyDay;
    double speed = sky::SkyFeed::kJourneySpeed;   ///< the running speed, one of kSpeeds (kept while frozen)
    bool frozen = false;
    bool loop = false;
    double utc = std::numeric_limits<double>::quiet_NaN();   ///< model time; NaN = the day's Journey start

    /** The nearest of kSpeeds (on a log scale). */
    static double snapSpeed (double speed) noexcept;
    static bool knownDay (const juce::String& day) noexcept;

    juce::ValueTree toTree() const;
    /** Unknown days, speeds off the table and missing fields fall back to the defaults. */
    static JourneyState fromTree (const juce::ValueTree&);
};

/** What `journeySet` asks for; missing fields stay as they are. */
struct JourneyChange
{
    std::optional<bool> frozen;
    std::optional<double> speed;
    std::optional<bool> loop;
    std::optional<juce::String> day;
};

/** Worldtable: the Sky Model playing a magnetosphere drone (spec/05), with the chord hold, the
    spec's FX chain and the MIDI latch (T3.6). The model runs on the audio clock (T2.4). */
class PluginProcessor final : public juce::AudioProcessor,
                              private juce::AsyncUpdater
{
public:
    /** Bump when the saved-state layout changes; add a step to migrateState(). */
    static constexpr int kStateVersion = 4;

    static inline const juce::Identifier kStateType      { "Worldtable" };
    static inline const juce::Identifier kStateVersionId { "stateVersion" };
    static inline const juce::Identifier kJourneyType    { "journey" };

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

    //== The Journey transport (T6.9) ==========================================
    /** Message thread (the editor's `journeySet`): run or freeze, speed, loop, day. A new day loads
        here (SkyFeed::loadJourney) and lands at its Journey start; Run after the end of the
        recording starts it again from the beginning. Returns the new state. */
    JourneyState setJourney (const JourneyChange&);

    /** Any thread but the audio thread: the Journey now, `utc` the model time. */
    JourneyState getJourney() const;

    /** The recording has ended: model time holds at the track's end and Loop is off. */
    bool journeyEnded() const;

private:
    void applyJourney (const JourneyState&);
    void handleAsyncUpdate() override;

    juce::AudioProcessorValueTreeState apvts;
    params::ParamPointers paramPointers;
    sky::SkyRuntime skyRuntime;
    sky::SkyFeed skyFeed { skyRuntime };
    dsp::Instrument instrument;

    // The message thread sets the Journey; a host may save or restore from another thread. Never
    // touched by the audio thread, which hears the Journey through SkyRuntime's atomics.
    juce::CriticalSection journeyLock;
    JourneyState journey;
    bool restorePending = false;   ///< a restore from another thread waits for the message thread

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginProcessor)
};

} // namespace worldtable
