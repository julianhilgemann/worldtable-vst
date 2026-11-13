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
const juce::Identifier kSourceId { "source" }, kDestinationId { "destination" }, kDepthId { "depth" };
const juce::Identifier kParamType { "PARAM" }, kParamIdId { "id" }, kParamValueId { "value" };

int sourceIndexOf (const juce::String& key) noexcept
{
    for (size_t i = 0; i < dsp::mod::kSources.size(); ++i)
        if (key == dsp::mod::kSources[i].key)
            return static_cast<int> (i);
    return -1;
}

int destinationIndexOf (const juce::String& key) noexcept
{
    for (size_t i = 0; i < dsp::mod::kDestinations.size(); ++i)
        if (key == dsp::mod::kDestinations[i].key)
            return static_cast<int> (i);
    return -1;
}
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
juce::ValueTree PluginProcessor::modulationToTree (const dsp::mod::Depths& d)
{
    juce::ValueTree t (kModulationType);
    for (size_t s = 0; s < dsp::mod::kSources.size(); ++s)
        for (size_t c = 0; c < dsp::mod::kDestinations.size(); ++c)
        {
            const float depth = d.cells[s][c];
            if (! (std::abs (depth) > 0.0f))
                continue;
            juce::ValueTree r (kRouteType);
            r.setProperty (kSourceId, juce::String (dsp::mod::kSources[s].key), nullptr);
            r.setProperty (kDestinationId, juce::String (dsp::mod::kDestinations[c].key), nullptr);
            r.setProperty (kDepthId, static_cast<double> (depth), nullptr);
            t.appendChild (r, nullptr);
        }
    return t;
}

dsp::mod::Depths PluginProcessor::modulationFromTree (const juce::ValueTree& t)
{
    dsp::mod::Depths d;
    for (const auto& r : t)
    {
        if (! r.hasType (kRouteType))
            continue;
        const int s = sourceIndexOf (r.getProperty (kSourceId).toString());
        const int c = destinationIndexOf (r.getProperty (kDestinationId).toString());
        if (s < 0 || c < 0)
            continue;   // a key this build doesn't know: skipped
        d.set (static_cast<dsp::mod::Source> (s), static_cast<dsp::mod::Destination> (c),
               static_cast<float> (static_cast<double> (r.getProperty (kDepthId, 0.0))));
    }
    return d;
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
    // storm day from 17:20 UT at 24×, running, no loop.
    // v5 → v6 (T4.2): the modulation grid. The macros' hard-wiring became the default grid (the
    // `modulation` child) and eq_sky became its route Activity → Cut: an older state gets the
    // default grid with its eq_sky as that depth (0.3 if it has none), and eq_sky goes. Future
    // upgrades go here.
    if (version < 6 && ! state.getChildWithName (kModulationType).isValid())
    {
        auto grid = dsp::mod::defaultDepths();
        const auto sky = state.getChildWithProperty (kParamIdId, "eq_sky");
        if (sky.isValid() && sky.hasType (kParamType))
            grid.set (dsp::mod::Source::activity, dsp::mod::Destination::eqCut,
                      static_cast<float> (static_cast<double> (sky.getProperty (kParamValueId, 0.3))));
        state.removeChild (sky, nullptr);
        state.appendChild (modulationToTree (grid), nullptr);
    }
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
    state.removeChild (state.getChildWithName (kModulationType), nullptr);
    state.appendChild (modulationToTree (getModulation()), nullptr);
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
    const auto grid = migrated.getChildWithName (kModulationType);
    const auto depths = grid.isValid() ? modulationFromTree (grid) : dsp::mod::defaultDepths();
    migrated.removeChild (grid, nullptr);
    apvts.replaceState (migrated);
    storeModulation (depths, false);   // the host set it: nothing to tell it

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

//==============================================================================
dsp::mod::Depths PluginProcessor::getModulation() const
{
    const juce::ScopedLock sl (modLock);
    return modDepths;
}

void PluginProcessor::setModulation (const dsp::mod::Depths& depths)
{
    storeModulation (depths, true);
}

void PluginProcessor::setRoute (dsp::mod::Source source, dsp::mod::Destination destination, float depth)
{
    auto d = getModulation();
    d.set (source, destination, depth);
    storeModulation (d, true);
}

void PluginProcessor::storeModulation (const dsp::mod::Depths& depths, bool notifyHost)
{
    {
        const juce::ScopedLock sl (modLock);
        modDepths = depths;
        // The audio thread holds a slot only while it copies one (Instrument::process), and an
        // unconsumed pending slot is reclaimed, so a free slot is there at once.
        auto& exchange = instrument.depthsExchange();
        for (int tries = 0; tries < 1000; ++tries)
        {
            const int slot = exchange.acquireForWriting();
            if (slot >= 0)
            {
                exchange.slot (slot) = depths;
                exchange.publish (slot);
                break;
            }
            juce::Thread::yield();
        }
        modRevision.fetch_add (1, std::memory_order_acq_rel);
    }
    if (notifyHost)
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
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
