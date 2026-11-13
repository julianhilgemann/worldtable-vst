// T3.7: Play mode — PlayVoices on its own, then the instrument with MIDI notes.

#include "AllocationCounter.h"
#include "PluginProcessor.h"
#include "dsp/LowResonator.h"
#include "dsp/PlayVoices.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>
#include <vector>

using namespace worldtable;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kTick = 480;   // the sky's 10 ms

double rmsOf (const std::vector<float>& x, size_t from, size_t to)
{
    double s = 0.0;
    for (size_t i = from; i < std::min (to, x.size()); ++i)
        s += static_cast<double> (x[i]) * x[i];
    return std::sqrt (s / static_cast<double> (std::max<size_t> (1, to - from)));
}

double powerAt (const std::vector<float>& x, size_t from, size_t to, double hz)
{
    const double k = 2.0 * std::cos (2.0 * std::numbers::pi * hz / kRate);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        const double s0 = x[i] + k * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - k * s1 * s2;
}

/** PlayVoices on a still, centred sky; `events` are (tick, action). */
struct VoiceRig
{
    dsp::PlayVoices voices;
    dsp::SkyNodes sky;

    explicit VoiceRig (dsp::PlayVoices::Settings s = {})
    {
        voices.prepare (kRate, 1);
        voices.setSettings (s);
        sky.count = 64;
        for (size_t i = 0; i < 64; ++i)
        {
            sky.panL[i] = sky.panR[i] = std::sqrt (0.5);
            sky.presence[i] = 1.0;
            sky.colour[i] = { 1.0, 1.0, 1.0 };
        }
    }

    std::vector<float> run (double seconds, const std::function<void (int)>& atTick = {})
    {
        const int ticks = static_cast<int> (seconds * 100.0);
        std::vector<float> l (static_cast<size_t> (ticks * kTick)), r (l.size());
        for (int t = 0; t < ticks; ++t)
        {
            if (atTick)
                atTick (t);
            voices.tick (sky, 0.5, 0.0, kTick);
            voices.process (l.data() + t * kTick, r.data() + t * kTick, kTick);
        }
        return l;
    }
};
} // namespace

TEST_CASE ("a played note sounds at its pitch, within its attack, and is gone after its release", "[play]")
{
    dsp::PlayVoices::Settings s;
    s.attack = 0.01;
    s.release = 1.0;
    VoiceRig rig (s);
    const auto out = rig.run (4.0, [&rig] (int t)
    {
        if (t == 50)
            rig.voices.noteOn (69, 0.8f);   // A4 at 0.5 s
        if (t == 200)
            rig.voices.noteOff (69);        // released at 2 s
    });
    CHECK (rmsOf (out, 0, 24000) == 0.0);                                         // silent before
    CHECK (rmsOf (out, 24000 + 480, 24000 + 2400) > 0.01);                        // speaks within 10 ms
    const auto held = static_cast<size_t> (1.0 * kRate), heldEnd = static_cast<size_t> (2.0 * kRate);
    CHECK (powerAt (out, held, heldEnd, 440.0) > 100.0 * powerAt (out, held, heldEnd, 440.0 * std::pow (2.0, 1.0 / 12.0)));
    CHECK (rmsOf (out, static_cast<size_t> (3.1 * kRate), out.size()) < 1.0e-4);   // −60 dB within the release
    CHECK (rig.voices.activeCount() == 0);
}

TEST_CASE ("polyphony: 16 held at most, a retrigger keeps its voice, the pedal holds", "[play]")
{
    VoiceRig rig;
    for (int n = 0; n < 17; ++n)
        rig.voices.noteOn (40 + 2 * n, 0.7f);
    rig.run (0.05);
    CHECK (rig.voices.heldCount() == dsp::PlayVoices::kMaxHeld);
    CHECK (rig.voices.lowestHeld() == 42);   // the oldest (40) gave way
    rig.voices.allNotesOff();
    rig.run (0.05);
    CHECK (rig.voices.heldCount() == 0);
    CHECK (rig.voices.lowestHeld() == dsp::PlayVoices::kNoNote);

    VoiceRig again;
    again.voices.noteOn (60, 0.7f);
    again.run (0.1);
    again.voices.noteOn (60, 0.9f);
    again.run (0.1);
    CHECK (again.voices.activeCount() == 1);

    VoiceRig pedal;
    pedal.voices.sustain (true);
    pedal.voices.noteOn (62, 0.7f);
    pedal.run (0.1);
    pedal.voices.noteOff (62);
    pedal.run (0.1);
    CHECK (pedal.voices.heldCount() == 1);
    pedal.voices.sustain (false);
    pedal.run (0.01);
    CHECK (pedal.voices.heldCount() == 0);
}

TEST_CASE ("all sound off silences within a few milliseconds", "[play]")
{
    VoiceRig rig;
    const auto out = rig.run (1.0, [&rig] (int t)
    {
        if (t == 0)
            for (int n : { 48, 52, 55 })
                rig.voices.noteOn (n, 1.0f);
        if (t == 50)
            rig.voices.allSoundOff();
    });
    CHECK (rmsOf (out, 24000 + 960, out.size()) == 0.0);
    CHECK (rig.voices.activeCount() == 0);
}

//==================================================================================================
namespace
{
void setParam (PluginProcessor& p, const char* id, float realValue)
{
    auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.getValueTreeState().getParameter (id));
    REQUIRE (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

/** MIDI at absolute sample positions, cut into whatever blocks the host uses. */
using Events = std::vector<std::pair<int, juce::MidiMessage>>;

std::vector<float> render (PluginProcessor& p, int block, int frames, const Events& events,
                           const std::function<void (int)>& atBlockStart = {})
{
    std::vector<float> out (static_cast<size_t> (2 * frames));
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    size_t next = 0;
    for (int done = 0; done < frames; done += block)
    {
        if (atBlockStart)
            atBlockStart (done);
        const int n = std::min (block, frames - done);
        buffer.setSize (2, n, false, false, true);
        buffer.clear();
        midi.clear();
        for (; next < events.size() && events[next].first < done + n; ++next)
            midi.addEvent (events[next].second, events[next].first - done);
        p.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            std::copy (buffer.getReadPointer (ch), buffer.getReadPointer (ch) + n, out.begin() + ch * frames + done);
    }
    return out;
}

Events chordAt (int at, std::initializer_list<int> notes, int offAt)
{
    Events e;
    for (const int n : notes)
        e.emplace_back (at, juce::MidiMessage::noteOn (1, n, 0.8f));
    for (const int n : notes)
        e.emplace_back (offAt, juce::MidiMessage::noteOff (1, n));
    std::stable_sort (e.begin(), e.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    return e;
}

constexpr float kCeiling = 0.8913f * 1.0005f;
} // namespace

TEST_CASE ("Play mode: silent without notes, the chord readout names what is held", "[processor][play]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    PluginProcessor p (false);
    setParam (p, "play", 1.0f);
    setParam (p, "rev_mix", 0.0f);
    setParam (p, "dly_mix", 0.0f);
    p.setPlayConfigDetails (0, 2, kRate, 512);
    p.prepareToPlay (kRate, 512);

    const int second = static_cast<int> (kRate);
    auto out = render (p, 512, 2 * second, {});
    std::vector<float> left (out.begin(), out.begin() + 2 * second);
    CHECK (rmsOf (left, static_cast<size_t> (second / 2), left.size()) < 1.0e-4);   // the drone is out

    out = render (p, 512, 2 * second, chordAt (100, { 60, 64, 67 }, second));
    left.assign (out.begin(), out.begin() + 2 * second);
    CHECK (rmsOf (left, 4800, static_cast<size_t> (second)) > 0.01);
    dsp::ChordState c;
    bool any = false;
    while (p.getInstrument().readChord (c))
        any = true;
    REQUIRE (any);
    CHECK (c.play);
    CHECK (c.held == 0);   // released at 1 s

    // while held, the readout names them
    render (p, 512, second / 2, { { 0, juce::MidiMessage::noteOn (1, 62, 0.8f) }, { 0, juce::MidiMessage::noteOn (1, 66, 0.8f) },
                                  { 0, juce::MidiMessage::noteOn (1, 69, 0.8f) } });
    while (p.getInstrument().readChord (c)) {}
    CHECK (c.held == 3);
    CHECK (juce::String::fromUTF8 (c.label.data()) == "Dmaj");

    // the low resonator follows the lowest held note (D) in the low band (from the floor, C1)
    CHECK (p.getInstrument().sources().lowResonator().note() == dsp::lowNote (2, 24));
}

TEST_CASE ("Play mode: the same MIDI gives the same sound at any block size", "[processor][play][determinism]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    const int frames = static_cast<int> (3.0 * kRate);
    Events events = chordAt (1000, { 48, 55, 64 }, 60000);
    for (int i = 0; i < 12; ++i)   // a run of short notes, off the block grid
    {
        events.emplace_back (70000 + 4011 * i, juce::MidiMessage::noteOn (1, 72 + (i % 5), 0.5f + 0.04f * i));
        events.emplace_back (70000 + 4011 * i + 1777, juce::MidiMessage::noteOff (1, 72 + (i % 5)));
    }
    std::stable_sort (events.begin(), events.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });

    const auto renderAt = [&] (int block)
    {
        PluginProcessor p (false);
        setParam (p, "play", 1.0f);
        setParam (p, "play_drone", 0.3f);
        setParam (p, "eq4_freq", 2000.0f);   // T3.9: a coloured EQ with a driven band
        setParam (p, "eq2_gain", 6.0f);
        setParam (p, "eq2_drive", 0.6f);
        p.setPlayConfigDetails (0, 2, kRate, block);
        p.prepareToPlay (kRate, block);
        return render (p, block, frames, events);
    };

    const auto ref = renderAt (512);
    const int block = GENERATE (256, 1000);
    const auto other = renderAt (block);
    double maxDiff = 0.0;
    for (size_t i = 0; i < ref.size(); ++i)
        maxDiff = std::max (maxDiff, std::abs (static_cast<double> (ref[i] - other[i])));
    INFO ("block " << block << " vs 512: max difference " << maxDiff);
    CHECK (maxDiff < 1.0e-5);
    CHECK (rmsOf (ref, 0, ref.size()) > 0.005);
}

TEST_CASE ("Play mode: entering and leaving glide, and nothing clicks", "[processor][play]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    PluginProcessor p (false);
    setParam (p, "rev_mix", 0.0f);
    p.setPlayConfigDetails (0, 2, kRate, 512);
    p.prepareToPlay (kRate, 512);

    const int second = static_cast<int> (kRate);
    const int frames = 12 * second;
    // drone 4 s, Play (with a held chord) 4 s, drone again
    const auto out = render (p, 512, frames, chordAt (4 * second + 2000, { 57, 61, 64 }, 7 * second),
                             [&p] (int at)
    {
        if (at == 4 * second)   // 4 s and 8 s are block starts (512 divides 192000)
            setParam (p, "play", 1.0f);
        if (at == 8 * second)
            setParam (p, "play", 0.0f);
    });
    std::vector<float> left (out.begin(), out.begin() + frames);
    float steady = 0.0f;
    for (int i = second; i < 4 * second - 1024; ++i)
        steady = std::max (steady, std::abs (left[static_cast<size_t> (i)] - left[static_cast<size_t> (i - 1)]));
    float worst = 0.0f;
    for (int i = 4 * second - 1024; i < frames; ++i)
        worst = std::max (worst, std::abs (left[static_cast<size_t> (i)] - left[static_cast<size_t> (i - 1)]));
    INFO ("steady " << steady << ", around the switches " << worst);
    CHECK (worst < 4.0f * steady + 0.02f);   // a played strike is louder than the drone, never a jump
    float peak = 0.0f;
    for (const float x : out)
    {
        REQUIRE (std::isfinite (x));
        peak = std::max (peak, std::abs (x));
    }
    CHECK (peak <= kCeiling);
}

TEST_CASE ("Play mode allocates nothing with MIDI flowing", "[processor][play][realtime]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    PluginProcessor p (false);
    setParam (p, "play", 1.0f);
    p.setPlayConfigDetails (0, 2, kRate, 512);
    p.prepareToPlay (kRate, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    midi.ensureSize (4096);
    // warm up (the MIDI buffer's storage, the first chord readout)
    for (int b = 0; b < 50; ++b)
    {
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 60 + b % 12, 0.7f), 7);
        midi.addEvent (juce::MidiMessage::noteOff (1, 60 + (b + 6) % 12), 300);
        buffer.clear();
        p.processBlock (buffer, midi);
    }
    const auto before = testing::allocationsOnThisThread();
    for (int b = 0; b < 300; ++b)
    {
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 48 + b % 24, 0.3f + 0.002f * b), b % 512);
        midi.addEvent (juce::MidiMessage::noteOff (1, 48 + (b + 11) % 24), (b * 7) % 512);
        if (b % 40 == 0)
            midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, b % 80 == 0 ? 127 : 0), 100);
        buffer.clear();
        p.processBlock (buffer, midi);
    }
    CHECK (testing::allocationsOnThisThread() == before);
}

TEST_CASE ("Extreme Play settings stay finite and under the ceiling", "[processor][play][render]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    PluginProcessor p (false);
    setParam (p, "play", 1.0f);
    setParam (p, "play_attack", 0.001f);
    setParam (p, "play_release", 20.0f);
    setParam (p, "play_velocity", 0.0f);
    setParam (p, "play_drone", 1.0f);
    setParam (p, "low_level", 1.0f);
    setParam (p, "low_decay", 30.0f);
    setParam (p, "eq1_gain", 12.0f);   // T3.9: every band up and driven, the cut low and opened by the sky
    setParam (p, "eq2_gain", 12.0f);
    setParam (p, "eq2_q", 6.0f);
    setParam (p, "eq3_gain", 12.0f);
    setParam (p, "eq4_freq", 200.0f);
    setParam (p, "eq_sky", 1.0f);
    for (const char* id : { "eq1_drive", "eq2_drive", "eq3_drive", "eq4_drive" })
        setParam (p, id, 1.0f);
    setParam (p, "red_level", 1.0f);
    setParam (p, "violet_level", 1.0f);
    setParam (p, "rev_mix", 1.0f);
    p.setPlayConfigDetails (0, 2, 44100.0, 333);
    p.prepareToPlay (44100.0, 333);

    Events events;
    for (int i = 0; i < 40; ++i)
        events.emplace_back (i * 3001, juce::MidiMessage::noteOn (1, 24 + (i * 7) % 80, 1.0f));
    const auto out = render (p, 333, static_cast<int> (8 * 44100.0), events);
    float peak = 0.0f;
    for (const float x : out)
    {
        REQUIRE (std::isfinite (x));
        peak = std::max (peak, std::abs (x));
    }
    CHECK (peak <= kCeiling);
}

//==================================================================================================
// T6.6: a click on the magnetogram seeks the Journey mid-play.
TEST_CASE ("A seek mid-play neither clicks nor swells, and the chords follow the sky there", "[processor][seek]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;

    PluginProcessor p (false);
    setParam (p, "rev_mix", 0.0f);   // the dry sound: a swell would not hide in the tail
    setParam (p, "dly_mix", 0.0f);
    p.setPlayConfigDetails (0, 2, kRate, 512);
    p.prepareToPlay (kRate, 512);

    // a landing minute whose phase differs from the one playing now, a few hours ahead
    const auto& chart = p.getSkyFeed().dayChart();
    REQUIRE (! chart.phase.empty());
    const auto nowPhase = static_cast<int> (p.getSky().model().current().phase);
    const auto startMinute = static_cast<size_t> ((p.getSky().model().modelTimeUtc() - chart.startUtc) / chart.stepSeconds);
    size_t landing = 0;
    for (size_t m = startMinute + 120; m < chart.phase.size() && landing == 0; ++m)
        if (chart.phase[m] != nowPhase && chart.phase[m] == chart.phase[m - 1] && chart.phase[m] == chart.phase[std::min (m + 30, chart.phase.size() - 1)])
            landing = m;
    REQUIRE (landing > 0);
    const double target = chart.startUtc + static_cast<double> (landing) * chart.stepSeconds + 30.0;

    const int second = static_cast<int> (kRate);
    const int frames = 14 * second;
    const int seekBlock = (6 * second / 512) * 512;   // seeks land at a block start
    dsp::ChordState c;
    uint32_t changesBefore = 0;
    const auto out = render (p, 512, frames, {}, [&] (int at)
    {
        if (at == seekBlock)
        {
            while (p.getInstrument().readChord (c)) {}
            changesBefore = c.changes;
            p.getSky().seek (target);
        }
    });
    std::vector<float> left (out.begin(), out.begin() + frames);
    const auto seekAt = static_cast<size_t> (seekBlock);

    float steady = 0.0f, across = 0.0f;
    for (size_t i = seekAt - static_cast<size_t> (3 * second); i < seekAt; ++i)
        steady = std::max (steady, std::abs (left[i] - left[i - 1]));
    for (size_t i = seekAt; i < seekAt + static_cast<size_t> (2 * second); ++i)
        across = std::max (across, std::abs (left[i] - left[i - 1]));
    const double before = rmsOf (left, seekAt - static_cast<size_t> (2 * second), seekAt);
    const double after = rmsOf (left, seekAt, seekAt + static_cast<size_t> (2 * second));
    INFO ("max step " << steady << " → " << across << ", rms " << before << " → " << after
          << ", landing minute " << landing << " phase " << int (chart.phase[landing]) << " (now " << nowPhase << ")"
          << ", start minute " << startMinute);
    CHECK (across < 2.0f * steady + 0.01f);
    CHECK (after < 2.0 * before);   // a jump into a storm brightens over seconds, it doesn't burst

    CHECK (p.getSky().model().modelTimeUtc() > target);                  // it jumped there
    CHECK (p.getSky().model().modelTimeUtc() < target + 8.0 * 24.0 + 1.0);  // and played on at 24×
    CHECK (static_cast<int> (p.getSky().model().current().phase) != nowPhase);
    while (p.getInstrument().readChord (c)) {}
    CHECK (c.changes > changesBefore);   // the harmony heard where it landed
}
