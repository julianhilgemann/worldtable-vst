// T4.2: the modulation grid in the instrument (spec/tasks/T4.2-modulation-grid.md): the session
// state, the telemetry, a route heard, no clicks when depths step, the same output at two block
// sizes with both LFOs and the envelope running and routed.

#include "AllocationCounter.h"
#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

using namespace worldtable;
using namespace worldtable::dsp::mod;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinULP;

namespace
{
constexpr double kRate = 48000.0;
constexpr float kCeiling = 0.8913f * 1.0005f;   // −1 dBFS (+ rounding)

void setParam (PluginProcessor& p, const char* id, float realValue)
{
    auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.getValueTreeState().getParameter (id));
    REQUIRE (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

juce::MemoryBlock saveState (PluginProcessor& p)
{
    juce::MemoryBlock blob;
    p.getStateInformation (blob);
    return blob;
}

juce::MemoryBlock toBinary (const juce::ValueTree& t)
{
    juce::MemoryBlock blob;
    if (auto xml = t.createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, blob);
    return blob;
}

void loadState (PluginProcessor& p, const juce::MemoryBlock& blob)
{
    p.setStateInformation (blob.getData(), static_cast<int> (blob.getSize()));
}

juce::ValueTree savedTree (PluginProcessor& p)
{
    const auto blob = saveState (p);
    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), static_cast<int> (blob.getSize()));
    REQUIRE (xml != nullptr);
    return juce::ValueTree::fromXml (*xml);
}

struct Note { int at; int note; bool on; };

/** Renders `seconds` in blocks of `block`, cut at a 0.1 s grid where `atGrid(sample)` may change
    things; the notes land at their samples. Returns left then right; counts allocations. */
std::vector<float> render (PluginProcessor& p, int block, double seconds, const std::function<void (int)>& atGrid,
                           const std::vector<Note>& notes, int64_t& allocations)
{
    const int total = static_cast<int> (seconds * kRate), grid = 4800;
    std::vector<float> out (static_cast<size_t> (2 * total));
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    midi.ensureSize (1024);
    for (int done = 0; done < total;)
    {
        if (atGrid && done % grid == 0)
            atGrid (done);
        const int n = std::min ({ block, total - done, grid - done % grid });
        buffer.setSize (2, n, false, false, true);
        buffer.clear();
        midi.clear();
        for (const auto& e : notes)
            if (e.at >= done && e.at < done + n)
                midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, 0.8f) : juce::MidiMessage::noteOff (1, e.note), e.at - done);
        const auto before = testing::allocationsOnThisThread();
        p.processBlock (buffer, midi);
        allocations += testing::allocationsOnThisThread() - before;
        for (int ch = 0; ch < 2; ++ch)
            std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + n, out.begin() + ch * total + done);
        done += n;
    }
    return out;
}

float maxStep (const std::vector<float>& x, size_t from, size_t to)
{
    float m = 0.0f;
    for (size_t i = std::max<size_t> (from, 1); i < std::min (to, x.size()); ++i)
        m = std::max (m, std::abs (x[i] - x[i - 1]));
    return m;
}

/** The worst 10 ms window's largest step against the median of the ±0.5 s around it. */
float outlier (const std::vector<float>& z, size_t from, size_t to)
{
    std::vector<float> windows;
    for (size_t w = from; w + 480 <= to; w += 480)
        windows.push_back (maxStep (z, w, w + 481));
    float worst = 0.0f;
    for (size_t w = 50; w + 50 < windows.size(); ++w)
    {
        std::vector<float> around (windows.begin() + static_cast<long> (w - 50), windows.begin() + static_cast<long> (w + 50));
        std::nth_element (around.begin(), around.begin() + 50, around.end());
        worst = std::max (worst, windows[w] / std::max (around[50], 1.0e-6f));
    }
    return worst;
}

void prepare (PluginProcessor& p, int block)
{
    p.setPlayConfigDetails (0, 2, kRate, block);
    p.prepareToPlay (kRate, block);
}
} // namespace

//==================================================================================================
// The session state
//==================================================================================================
TEST_CASE ("Mod state: the grid is saved by stable keys and restored", "[mod][state]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    PluginProcessor a (false);
    CHECK (a.getModulation().cells == defaultDepths().cells);   // a new instance: today's wiring

    const auto revision = a.modulationRevision();
    a.setRoute (Source::lfo1, Destination::width, 0.75f);
    a.setRoute (Source::envelope, Destination::reverbShimmer, -0.5f);
    a.setRoute (Source::storm, Destination::progression, 0.0f);   // cleared
    CHECK (a.modulationRevision() == revision + 3);

    const auto tree = savedTree (a);
    CHECK (static_cast<int> (tree.getProperty (PluginProcessor::kStateVersionId)) == 6);
    const auto grid = tree.getChildWithName (PluginProcessor::kModulationType);
    REQUIRE (grid.isValid());
    CHECK (grid.getNumChildren() == 13);   // the default grid's 12, − 1, + 2
    double saved = 0.0;
    for (const auto& r : grid)
        if (r.getProperty ("source").toString() == "lfo1" && r.getProperty ("destination").toString() == "width")
            saved = r.getProperty ("depth");
    CHECK_THAT (saved, WithinAbs (0.75, 1.0e-9));
    CHECK_FALSE (tree.getChildWithProperty ("id", "modulation").isValid());   // not a parameter

    PluginProcessor b (false);
    const auto before = b.modulationRevision();
    loadState (b, saveState (a));
    CHECK (b.getModulation().cells == a.getModulation().cells);
    CHECK (b.modulationRevision() != before);   // the editor re-sends the grid after a load

    // keys this build doesn't know are skipped, a depth out of range is clamped
    auto t = PluginProcessor::modulationToTree (Depths {});
    juce::ValueTree odd (PluginProcessor::kRouteType);
    odd.setProperty ("source", "lfo9", nullptr);
    odd.setProperty ("destination", "width", nullptr);
    odd.setProperty ("depth", 0.5, nullptr);
    t.appendChild (odd, nullptr);
    juce::ValueTree big (PluginProcessor::kRouteType);
    big.setProperty ("source", "sky_onset", nullptr);
    big.setProperty ("destination", "excite", nullptr);
    big.setProperty ("depth", 3.0, nullptr);
    t.appendChild (big, nullptr);
    const auto d = PluginProcessor::modulationFromTree (t);
    int routes = 0;
    for (const auto& row : d.cells)
        for (const float x : row)
            routes += std::abs (x) > 0.0f ? 1 : 0;
    CHECK (routes == 1);
    CHECK_THAT (d.get (Source::onset, Destination::excite), WithinULP (1.0f, 0));

    // a v6 state whose grid was cleared stays cleared
    auto cleared = savedTree (a);
    cleared.removeChild (cleared.getChildWithName (PluginProcessor::kModulationType), nullptr);
    cleared.appendChild (PluginProcessor::modulationToTree (Depths {}), nullptr);
    loadState (b, toBinary (cleared));
    CHECK (b.getModulation().cells == Depths {}.cells);
}

TEST_CASE ("Mod state: a v5 state gets the default grid with its eq_sky as Activity → Cut", "[mod][state][migration]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    CHECK (PluginProcessor::kStateVersion == 6);

    for (const bool withSky : { true, false })
    {
        PluginProcessor p (false);
        p.setModulation (Depths {});   // not the default: the old state must bring its own
        auto old = p.getValueTreeState().copyState();
        old.setProperty (PluginProcessor::kStateVersionId, 5, nullptr);
        if (withSky)
        {
            juce::ValueTree sky ("PARAM");
            sky.setProperty ("id", "eq_sky", nullptr);
            sky.setProperty ("value", 0.7, nullptr);
            old.appendChild (sky, nullptr);
        }
        loadState (p, toBinary (old));

        INFO ((withSky ? "eq_sky 0.7" : "no eq_sky"));
        auto want = defaultDepths();
        if (withSky)
            want.set (Source::activity, Destination::eqCut, 0.7f);
        CHECK (p.getModulation().cells == want.cells);
        const auto tree = savedTree (p);
        CHECK (static_cast<int> (tree.getProperty (PluginProcessor::kStateVersionId)) == PluginProcessor::kStateVersion);
        CHECK_FALSE (tree.getChildWithProperty ("id", "eq_sky").isValid());
    }

    // migrateState itself: v2 … v5 all get a grid
    for (int version = 2; version <= 5; ++version)
    {
        juce::ValueTree old (PluginProcessor::kStateType);
        old.setProperty (PluginProcessor::kStateVersionId, version, nullptr);
        const auto migrated = PluginProcessor::migrateState (old);
        REQUIRE (migrated.isValid());
        CHECK (PluginProcessor::modulationFromTree (migrated.getChildWithName (PluginProcessor::kModulationType)).cells
               == defaultDepths().cells);
    }
}

//==================================================================================================
// The engine
//==================================================================================================
TEST_CASE ("Mod engine: the default patch plays its knobs; a route is heard and the ring holds the live value", "[mod][processor]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    PluginProcessor p (false);
    prepare (p, 512);
    int64_t allocations = 0;
    render (p, 512, 1.0, {}, {}, allocations);

    ModTelemetry t;
    REQUIRE (p.getInstrument().readModulation (t));
    CHECK (t.tick >= 99);
    CHECK_THAT (t.frame[Target::width], WithinULP (0.9f, 0));
    CHECK_THAT (t.frame[Target::excite], WithinULP (1.0f, 0));   // Storm at its centre: the exciter as it was
    CHECK_THAT (t.frame[Target::dlyMix], WithinULP (0.25f, 0));
    CHECK (t.frame.hi[indexOf (Target::dlyMix)] > t.frame.lo[indexOf (Target::dlyMix)]);   // Space's ring
    CHECK (t.frame.lo[indexOf (Target::excite)] < 1.0f);

    // LFO 1 at 2 Hz on Width, full depth: within 1 s it moves Width all the way (0 … 1, clamped)
    setParam (p, "lfo1_rate", 2.0f);
    p.setRoute (Source::lfo1, Destination::width, 1.0f);
    render (p, 512, 1.5, {}, {}, allocations);
    REQUIRE (p.getInstrument().readModulation (t));
    const float lfo = t.frame.source[indexOf (Source::lfo1)];
    CHECK_THAT (t.frame[Target::width], WithinAbs (std::clamp (0.9f * (1.0f + lfo), 0.0f, 1.0f), 1.0e-6));
    CHECK_THAT (t.frame.lo[indexOf (Target::width)], WithinAbs (0.0, 1.0e-6));
    CHECK_THAT (t.frame.hi[indexOf (Target::width)], WithinAbs (1.0, 1.0e-6));
    CHECK (t.frame[Target::width] >= t.frame.lo[indexOf (Target::width)]);
    CHECK (t.frame[Target::width] <= t.frame.hi[indexOf (Target::width)]);
    CHECK_THAT (t.lfo[0].value, WithinULP (lfo, 0));
    CHECK_THAT (p.getInstrument().sound().getSettings().width, WithinAbs (t.frame[Target::width], 1.0e-6));

    // Fire: the envelope runs on the next ticks
    p.getInstrument().fireEnvelope();
    render (p, 512, 0.05, {}, {}, allocations);
    REQUIRE (p.getInstrument().readModulation (t));
    CHECK (t.envelope.stage == Envelope::Stage::attack);
    CHECK (t.envelope.value > 0.0f);
    CHECK (allocations == 0);
}

TEST_CASE ("Mod engine: depths stepped during a render never click", "[mod][processor][render][NoClicks]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    constexpr double kSeconds = 14.0;
    const auto run = [] (bool step, int64_t& allocations)
    {
        PluginProcessor p (false);
        setParam (p, "lfo1_rate", 2.0f);
        setParam (p, "lfo2_rate", 1.5f);
        setParam (p, "lfo2_shape", 3.0f);   // stepped random
        setParam (p, "env_trigger", 2.0f);  // Fire only
        setParam (p, "env_attack", 0.01f);  // the fastest: a full step in one tick
        prepare (p, 512);
        return render (p, 512, kSeconds, [&p, step] (int at)
        {
            if (! step)
                return;
            const double t = at / kRate;
            const auto is = [t] (double s) { return std::abs (t - s) < 0.01; };
            if (is (2.0))
            {
                p.setRoute (Source::lfo1, Destination::delayMix, 1.0f);    // 0 → full in one step
                p.setRoute (Source::lfo2, Destination::phaserMix, -1.0f);
                p.setRoute (Source::lfo1, Destination::redLevel, 1.0f);
            }
            if (is (4.0))
            {
                p.setRoute (Source::envelope, Destination::reverbMix, 1.0f);
                p.setRoute (Source::envelope, Destination::eqMidGain, 1.0f);   // +12 dB
                p.setRoute (Source::envelope, Destination::excite, 1.0f);
                p.getInstrument().fireEnvelope();
            }
            if (is (6.0))
                p.setRoute (Source::lfo1, Destination::delayMix, -1.0f);   // +1 → −1
            if (is (7.0))
                p.getInstrument().fireEnvelope();
            if (is (8.0))
                p.setRoute (Source::storm, Destination::excite, -1.0f);   // the exciter's level 0.6 → −1
            if (is (10.0))
                p.setModulation (Depths {});   // everything off at once
        }, {}, allocations);
    };
    int64_t allocations = 0;
    const auto a = run (true, allocations);
    const auto still = run (false, allocations);
    CHECK (allocations == 0);

    int nonFinite = 0;
    float peak = 0.0f;
    for (const float x : a)
    {
        nonFinite += std::isfinite (x) ? 0 : 1;
        peak = std::max (peak, std::abs (x));
    }
    CHECK (nonFinite == 0);
    CHECK (peak <= kCeiling);

    const size_t total = a.size() / 2;
    for (size_t ch = 0; ch < 2; ++ch)
    {
        const std::vector<float> x (a.begin() + static_cast<long> (ch * total), a.begin() + static_cast<long> ((ch + 1) * total));
        const std::vector<float> y (still.begin() + static_cast<long> (ch * total), still.begin() + static_cast<long> ((ch + 1) * total));
        const size_t from = static_cast<size_t> (1.0 * kRate);
        const float stepped = maxStep (x, from, total), reference = maxStep (y, from, total);
        const float worst = outlier (x, from, total), referenceWorst = outlier (y, from, total);
        std::printf ("Depths stepped, channel %d: max step %.4f (still %.4f), worst 10 ms window %.2fx its neighbourhood's "
                     "median (still %.2fx), peak %.2f dBFS\n",
                     static_cast<int> (ch), static_cast<double> (stepped), static_cast<double> (reference), static_cast<double> (worst),
                     static_cast<double> (referenceWorst), 20.0 * std::log10 (static_cast<double> (peak)));
        CHECK (stepped < 2.0f * reference);
        CHECK (worst < std::max (3.0f, 1.25f * referenceWorst));
    }
}

TEST_CASE ("Mod engine: the same output at two block sizes with both LFOs and the envelope running and routed",
           "[mod][processor][render][determinism]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    constexpr double kSeconds = 8.0;
    // notes (drone mode: they latch the chord and fire the envelope in Note mode), one shorter
    // than a tick, on samples that are no block's edge
    const std::vector<Note> notes { { 12345, 62, true }, { 61111, 62, false }, { 150001, 69, true }, { 150101, 69, false },
                                    { 200017, 66, true }, { 290003, 66, false } };
    struct Out { std::vector<float> audio; float envPeak = 0.0f; float lfoSpread = 0.0f; uint64_t ticks = 0; };
    const auto run = [&notes] (int block, int64_t& allocations)
    {
        PluginProcessor p (false);
        setParam (p, "lfo1_rate", 1.3f);
        setParam (p, "lfo1_shape", 1.0f);   // triangle
        setParam (p, "lfo2_rate", 0.9f);
        setParam (p, "lfo2_shape", 2.0f);   // smooth random
        setParam (p, "env_trigger", 1.0f);  // Note
        setParam (p, "env_attack", 0.05f);
        setParam (p, "env_decay", 0.3f);
        setParam (p, "env_release", 0.4f);
        p.setRoute (Source::lfo1, Destination::delayMix, 0.8f);
        p.setRoute (Source::lfo2, Destination::eqMidGain, 0.7f);
        p.setRoute (Source::lfo2, Destination::detune, 0.5f);
        p.setRoute (Source::envelope, Destination::reverbMix, 1.0f);
        p.setRoute (Source::envelope, Destination::violetLevel, 1.0f);
        p.setRoute (Source::onset, Destination::chorusMix, 1.0f);
        prepare (p, block);
        Out o;
        float lfoLo = 1.0f, lfoHi = -1.0f;
        o.audio = render (p, block, kSeconds, [&] (int)
        {
            o.envPeak = std::max (o.envPeak, p.getInstrument().envelope().value());
            lfoLo = std::min (lfoLo, p.getInstrument().lfo (0).value());
            lfoHi = std::max (lfoHi, p.getInstrument().lfo (0).value());
        }, notes, allocations);
        o.lfoSpread = lfoHi - lfoLo;
        o.ticks = p.getInstrument().sound().ticks();
        return o;
    };
    int64_t allocations = 0;
    const auto a = run (512, allocations);
    const auto b = run (333, allocations);
    const auto c = run (97, allocations);
    CHECK (allocations == 0);
    CHECK (a.envPeak > 0.5f);     // the envelope fired
    CHECK (a.lfoSpread > 1.5f);   // the LFO ran
    CHECK (a.ticks == b.ticks);

    double ab = 0.0, ac = 0.0;
    for (size_t i = 0; i < a.audio.size(); ++i)
    {
        ab = std::max (ab, static_cast<double> (std::abs (a.audio[i] - b.audio[i])));
        ac = std::max (ac, static_cast<double> (std::abs (a.audio[i] - c.audio[i])));
    }
    std::printf ("Mod engine, block 512 vs 333: max difference %g; vs 97: %g\n", ab, ac);
    CHECK (ab == 0.0);
    CHECK (ac == 0.0);
}

TEST_CASE ("Mod engine: the envelope fires on the same tick for the same note at any block size", "[mod][processor][env]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    // sample by sample: the first tick with a value is the one at sample 24480, the 52nd since
    // prepare (ticks at 0, 480, …)
    const auto byTick = [] (int block)
    {
        PluginProcessor p (false);
        setParam (p, "env_trigger", 1.0f);
        prepare (p, block);
        juce::AudioBuffer<float> buffer (2, 1);
        juce::MidiBuffer midi;
        for (int s = 0; s < 30000; ++s)   // sample by sample: the tick the value first rises on
        {
            midi.clear();
            if (s == 24321)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            buffer.clear();
            p.processBlock (buffer, midi);
            if (p.getInstrument().envelope().value() > 0.0f)
                return p.getInstrument().sound().ticks();
        }
        return uint64_t { 0 };
    };
    CHECK (byTick (1) == 52u);
    // and in blocks: by the end of the block holding sample 24480 the envelope has risen, never before
    for (const int block : { 64, 333, 512, 4096 })
    {
        PluginProcessor p (false);
        setParam (p, "env_trigger", 1.0f);
        prepare (p, block);
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        for (int done = 0; done < 30000; done += block)
        {
            midi.clear();
            if (24321 >= done && 24321 < done + block)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 24321 - done);
            buffer.clear();
            p.processBlock (buffer, midi);
            INFO ("block " << block << " ending at " << done + block);
            CHECK ((p.getInstrument().envelope().value() > 0.0f) == (done + block > 24480));
        }
    }
}
