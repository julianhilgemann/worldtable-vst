#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace worldtable
{

namespace
{
params::ParamPointers& bound (params::ParamPointers& pp, juce::AudioProcessorValueTreeState& apvts)
{
    pp.bind (apvts);
    return pp;
}

const juce::Identifier kDayId { "day" }, kSpeedId { "speed" }, kFrozenId { "frozen" }, kLoopId { "loop" }, kUtcId { "utc" };
} // namespace

//==============================================================================
double JourneyState::snapSpeed (double speed) noexcept
{
    if (! std::isfinite (speed) || speed <= 0.0)
        return sky::SkyFeed::kJourneySpeed;
    double best = kSpeeds[0];
    for (const double s : kSpeeds)
        if (std::abs (std::log (speed / s)) < std::abs (std::log (speed / best)))
            best = s;
    return best;
}

bool JourneyState::knownDay (const juce::String& day) noexcept
{
    for (const auto& d : data::kSkyDays)
        if (day == d.id)
            return true;
    return false;
}

juce::ValueTree JourneyState::toTree() const
{
    juce::ValueTree t (PluginProcessor::kJourneyType);
    t.setProperty (kDayId, day, nullptr);
    t.setProperty (kSpeedId, speed, nullptr);
    t.setProperty (kFrozenId, frozen, nullptr);
    t.setProperty (kLoopId, loop, nullptr);
    if (std::isfinite (utc))
        t.setProperty (kUtcId, utc, nullptr);
    return t;
}

JourneyState JourneyState::fromTree (const juce::ValueTree& t)
{
    JourneyState s;
    if (! t.isValid())
        return s;
    if (const auto day = t.getProperty (kDayId).toString(); knownDay (day))
    {
        s.day = day;
        if (t.hasProperty (kUtcId))
            if (const double utc = t.getProperty (kUtcId); std::isfinite (utc))
                s.utc = utc;   // a time on an unknown day means nothing: the default day starts at its start
    }
    if (t.hasProperty (kSpeedId))
        s.speed = snapSpeed (t.getProperty (kSpeedId));
    s.frozen = t.getProperty (kFrozenId, false);
    s.loop = t.getProperty (kLoopId, false);
    return s;
}

//==============================================================================

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
    skyRuntime.setLoop (journey.loop);
}

PluginProcessor::~PluginProcessor()
{
    cancelPendingUpdate();
}

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
    // loads flat with the cut open and the default drives.
    // v4 → v5 (T6.9): the `journey` child (day, speed, frozen, loop, model time). An older state
    // has none, so the Journey restores to its defaults (JourneyState::fromTree of nothing): the
    // storm day from 17:20 UT at 24×, running, no loop. Future upgrades go here.
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
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName (kJourneyType), nullptr);
    state.appendChild (getJourney().toTree(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PluginProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    auto migrated = migrateState (juce::ValueTree::fromXml (*xml));
    if (! migrated.isValid())
        return;

    const auto saved = migrated.getChildWithName (kJourneyType);
    const auto restored = JourneyState::fromTree (saved);
    migrated.removeChild (saved, nullptr);   // the APVTS keeps parameters only
    apvts.replaceState (migrated);

    // A day loads on the message thread (SkyFeed allocates and the editor reads its chart there).
    // From another thread the restore waits for it; until then getJourney() reports it.
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        applyJourney (restored);
        return;
    }
    {
        const juce::ScopedLock sl (journeyLock);
        journey = restored;
        restorePending = true;
    }
    triggerAsyncUpdate();
}

void PluginProcessor::handleAsyncUpdate()
{
    JourneyState restored;
    {
        const juce::ScopedLock sl (journeyLock);
        if (! restorePending)
            return;
        restored = journey;
    }
    applyJourney (restored);
}

//==============================================================================
void PluginProcessor::applyJourney (const JourneyState& target)
{
    jassert (juce::MessageManager::existsAndIsCurrentThread());
    JourneyState next = target;
    next.utc = std::numeric_limits<double>::quiet_NaN();   // the model holds the time from here

    // A new day lands with its track, as one seek; the same day just seeks. Both take the sound's
    // click-free seek path (T6.6). No time saved: the day's Journey start.
    if (next.day != skyFeed.dayId())
    {
        if (! skyFeed.loadJourney (next.day, target.utc))
        {
            next.day = data::kDefaultSkyDay;
            skyFeed.loadJourney (next.day);
        }
    }
    else
    {
        skyRuntime.seek (std::isfinite (target.utc) ? target.utc : skyFeed.dayStartUtc() + sky::SkyFeed::journeyStart (next.day));
    }
    skyRuntime.setSpeed (next.frozen ? 0.0 : next.speed);
    skyRuntime.setLoop (next.loop);

    const juce::ScopedLock sl (journeyLock);
    journey = next;
    restorePending = false;
}

JourneyState PluginProcessor::setJourney (const JourneyChange& change)
{
    jassert (juce::MessageManager::existsAndIsCurrentThread());
    handleAsyncUpdate();   // a restore still waiting comes first
    JourneyState next;
    {
        const juce::ScopedLock sl (journeyLock);
        next = journey;
    }

    const bool ended = journeyEnded();
    if (change.speed)
        next.speed = JourneyState::snapSpeed (*change.speed);
    if (change.loop)
        next.loop = *change.loop;
    if (change.frozen)
        next.frozen = *change.frozen;

    if (change.day && *change.day != next.day && JourneyState::knownDay (*change.day)
        && skyFeed.loadJourney (*change.day))   // lands at the new day's Journey start
    {
        next.day = *change.day;
    }
    else if (change.frozen && ! *change.frozen && ended)
    {
        skyRuntime.seek (skyFeed.dayStartUtc());   // Run after the end: the recording from its start
    }

    skyRuntime.setSpeed (next.frozen ? 0.0 : next.speed);
    skyRuntime.setLoop (next.loop);

    const juce::ScopedLock sl (journeyLock);
    journey = next;
    return getJourney();
}

JourneyState PluginProcessor::getJourney() const
{
    const juce::ScopedLock sl (journeyLock);
    auto s = journey;
    if (! restorePending)
        s.utc = skyRuntime.modelTimeNow();
    return s;
}

bool PluginProcessor::journeyEnded() const
{
    const juce::ScopedLock sl (journeyLock);
    return skyRuntime.atTrackEnd() && ! journey.loop && ! restorePending;
}

} // namespace worldtable

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new worldtable::PluginProcessor();
}
