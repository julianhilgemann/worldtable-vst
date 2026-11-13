// T4.2: the modulation grid, the slow LFOs and the free envelope (spec/tasks/T4.2-modulation-grid.md).
//
// The header's own tables (lists, keys, ranges, the default grid) and the engine's parts on their
// own. The engine in the instrument (state, determinism, clicks) is tested in ModEngineTests.cpp.

#include "AllocationCounter.h"
#include "PluginProcessor.h"
#include "dsp/mod/ModMatrix.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>
#include <string>
#include <vector>

using namespace worldtable;
using namespace worldtable::dsp::mod;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using Catch::Matchers::WithinULP;   // WithinULP (x, 0) = exactly equal, without -Wfloat-equal

namespace
{
constexpr double kPi = std::numbers::pi;

constexpr Target targetAt (int i) noexcept { return static_cast<Target> (i); }
constexpr Source sourceAt (int i) noexcept { return static_cast<Source> (i); }
constexpr Destination columnAt (int i) noexcept { return static_cast<Destination> (i); }

double dbOf (double ratio) { return 20.0 * std::log10 (std::max (ratio, 1.0e-30)); }

float defaultOf (PluginProcessor& p, const char* id)
{
    auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.getValueTreeState().getParameter (id));
    REQUIRE (param != nullptr);
    return param->convertFrom0to1 (param->getDefaultValue());
}

//== Today's hard-wired macros (Instrument.cpp, SkySound.cpp and Sources.cpp on main at 3fd1745) ===
double stormGain (double m) { return 0.4 + 1.2 * m; }
double macroGain (double m) { return 0.5 + m; }
double breathGain (double k, double c) { return 1.0 - 0.35 * k * c; }
double breathViolet (double k, double c) { return breathGain (k, c) * (1.0 - 0.15 * k * c); }

/** The knobs and the sky as the engine reads them. */
struct Knobs
{
    double storm = 0.5, breath = 0.35, glow = 0.5, space = 0.5, drift = 0.5;
    double cycle = 0.0;      ///< the breath cycle, 0 … 1
    double activity = 0.0;   ///< slewed, 0 … 1
};

SourceValues sourcesOf (const Knobs& k)
{
    SourceValues s;
    s[Source::storm] = static_cast<float> (2.0 * (k.storm - 0.5));
    s[Source::breath] = static_cast<float> (k.cycle * k.breath);
    s[Source::glow] = static_cast<float> (2.0 * (k.glow - 0.5));
    s[Source::space] = static_cast<float> (2.0 * (k.space - 0.5));
    s[Source::drift] = static_cast<float> (2.0 * (k.drift - 0.5));
    s[Source::activity] = static_cast<float> (std::min (k.activity, 1.0));
    s.breathKnob = static_cast<float> (k.breath);
    return s;
}

/** Every target's base, away from the clamps, so that a factor shows as it is. */
Bases plainBases()
{
    Bases b;
    b[Target::lowLevel] = 0.5f;
    b[Target::redLevel] = 0.5f;
    b[Target::greenLevel] = 0.5f;
    b[Target::violetLevel] = 0.3f;
    b[Target::detune] = 3.0f;
    b[Target::drift] = 4.0f;
    b[Target::width] = 0.9f;
    b[Target::excite] = 1.0f;   // fixed: no knob
    b[Target::eq1Gain] = 0.0f;
    b[Target::eq2Gain] = 0.0f;
    b[Target::eq3Gain] = 0.0f;
    b[Target::eq4Freq] = 2000.0f;
    b[Target::eq1Drive] = 0.0f;
    b[Target::eq2Drive] = 0.0f;
    b[Target::eq3Drive] = 0.0f;
    b[Target::eq4Drive] = 0.0f;
    b[Target::phMix] = 0.4f;
    b[Target::chMix] = 0.25f;
    b[Target::dlyFb] = 0.35f;
    b[Target::dlyMix] = 0.25f;
    b[Target::revDecay] = 9.0f;
    b[Target::revShimmer] = 0.2f;
    b[Target::revMix] = 0.35f;
    b[Target::progression] = 0.5f;
    return b;
}

/** What each target is today at these knobs (Strike: the exciter's factor). eq_sky = 0.3 was its
    default. */
double today (Target t, const Bases& bases, const Knobs& k)
{
    const double base = bases[t];
    switch (t)
    {
        case Target::excite:      return base * stormGain (k.storm);
        case Target::redLevel:
        case Target::greenLevel:  return base * breathGain (k.breath, k.cycle);
        case Target::violetLevel: return base * macroGain (k.glow) * breathViolet (k.breath, k.cycle);
        case Target::detune:
        case Target::drift:       return base * macroGain (k.drift);
        case Target::eq4Freq:     return std::min (20000.0, base * std::exp2 (0.3 * 3.0 * std::min (k.activity, 1.0)));
        case Target::dlyMix:
        case Target::revMix:      return std::clamp (base * macroGain (k.space), 0.0, 1.0);
        case Target::revShimmer:  return std::clamp (base * macroGain (k.glow), 0.0, 1.0);
        case Target::progression: return std::clamp (base * stormGain (k.storm), 0.0, 1.0);
        case Target::lowLevel:
        case Target::width:
        case Target::eq1Gain:
        case Target::eq2Gain:
        case Target::eq3Gain:
        case Target::eq1Drive:
        case Target::eq2Drive:
        case Target::eq3Drive:
        case Target::eq4Drive:
        case Target::phMix:
        case Target::chMix:
        case Target::dlyFb:
        case Target::revDecay:    return base;
    }
    return base;
}

Frame evaluated (const Depths& d, const SourceValues& s, const Bases& b)
{
    Frame f;
    ModMatrix::evaluate (d, s, b, f);
    return f;
}

//== LFO helpers ===================================================================================
SlowLfo makeLfo (SlowLfo::Shape shape, double hz, uint32_t seed = 1, uint32_t stream = 0)
{
    SlowLfo l;
    l.prepare (seed, stream);
    l.setShape (shape, false);
    l.setRate (hz);
    return l;
}

std::vector<float> run (SlowLfo& l, int ticks)
{
    std::vector<float> out;
    out.reserve (static_cast<size_t> (ticks));
    for (int i = 0; i < ticks; ++i)
        out.push_back (l.tick());
    return out;
}

double circular (double a, double b)
{
    const double d = std::abs (a - b);
    return std::min (d, 1.0 - d);
}

float peakOf (const std::vector<float>& v)
{
    float m = 0.0f;
    for (float x : v)
        m = std::max (m, std::abs (x));
    return m;
}

//== Envelope helpers ==============================================================================
Envelope::Settings envSettings (Envelope::Trigger trigger)
{
    Envelope::Settings s;
    s.attack = 0.1;    // 10 ticks
    s.decay = 0.2;     // 20 ticks
    s.sustain = 0.5;
    s.release = 0.3;   // 30 ticks
    s.trigger = trigger;
    return s;
}

Envelope makeEnv (Envelope::Trigger trigger)
{
    Envelope e;
    e.reset();
    e.setSettings (envSettings (trigger));
    return e;
}

double decayAt (double f, double sustain) { return 1.0 - (1.0 - sustain) * (1.0 - (1.0 - f) * (1.0 - f)); }
double releaseAt (double f, double from)  { return from * (1.0 - f) * (1.0 - f); }

/** The A 0.1 / D 0.2 / S 0.5 / R 0.3 envelope after tick t (1 = the first tick after the trigger)
    with the 1.2 s hold: attack 1–10, decay 11–30, hold 31–150, release 151–180, idle after. */
double firedAt (int t)
{
    if (t <= 10)  return t / 10.0;
    if (t <= 30)  return decayAt ((t - 10) / 20.0, 0.5);
    if (t <= 150) return 0.5;
    if (t <= 180) return releaseAt ((t - 150) / 30.0, 0.5);
    return 0.0;
}

bool firesOn (Envelope::Trigger mode, int event)
{
    auto e = makeEnv (mode);
    if (event == 0)
        e.onset();
    else if (event == 1)
        e.noteOn (60);
    else
        e.fire();
    for (int i = 0; i < 5; ++i)
        e.tick();
    return e.value() > 0.1f;
}
} // namespace

//==================================================================================================
// The header's tables (pass now)
//==================================================================================================
TEST_CASE ("Mod grid: the lists, their stable keys and the columns' targets", "[mod]")
{
    STATIC_REQUIRE (std::is_trivially_copyable_v<Depths>);
    STATIC_REQUIRE (sizeof (Depths) == sizeof (float) * kNumSources * kNumDestinations);
    STATIC_REQUIRE (indexOf (Source::envelope) + 1 == kNumSources);
    STATIC_REQUIRE (indexOf (Destination::progression) + 1 == kNumDestinations);
    STATIC_REQUIRE (indexOf (Target::progression) + 1 == kNumTargets);

    std::set<std::string> keys;
    for (const auto& s : kSources)
    {
        keys.insert (s.key);
        CHECK (s.lo < s.hi);
    }
    CHECK (keys.size() == static_cast<size_t> (kNumSources));
    keys.clear();
    for (const auto& d : kDestinations)
        keys.insert (d.key);
    CHECK (keys.size() == static_cast<size_t> (kNumDestinations));

    // the three groups, in order: 5 macros, 2 of the sky, 3 of motion
    for (int i = 0; i < kNumSources; ++i)
        CHECK (kSources[static_cast<size_t> (i)].group == (i < 5 ? Group::macros : i < 7 ? Group::sky : Group::motion));

    // every column moves a target; Drive moves the four band drives; a one-target column's key is
    // its parameter's ID, or "excite" for Strike (no parameter)
    std::array<int, kNumDestinations> moved {};
    for (const auto& t : kTargets)
        ++moved[indexOf (t.column)];
    for (int i = 0; i < kNumDestinations; ++i)
        CHECK (moved[static_cast<size_t> (i)] >= 1);
    CHECK (moved[indexOf (Destination::eqDrive)] == 4);
    for (const auto& t : kTargets)
        if (moved[indexOf (t.column)] == 1)
            CHECK (std::string (kDestinations[indexOf (t.column)].key) == (t.paramId[0] != '\0' ? t.paramId : "excite"));

    // the laws: the cut in octaves; the EQ gains and the drives add; everything else multiplies
    for (int i = 0; i < kNumTargets; ++i)
    {
        const auto t = targetAt (i);
        INFO (kTargets[static_cast<size_t> (i)].paramId);
        const bool adds = t == Target::eq1Gain || t == Target::eq2Gain || t == Target::eq3Gain || t == Target::eq1Drive
                       || t == Target::eq2Drive || t == Target::eq3Drive || t == Target::eq4Drive;
        CHECK (lawOf (t) == (t == Target::eq4Freq ? Law::oct : adds ? Law::add : Law::mul));
    }

    // the modulated range holds the knob's; it reaches past it only for the four levels (+6 dB),
    // Detune and Drift (today's × 1.5 at Drift 1) and Strike
    for (int i = 0; i < kNumTargets; ++i)
    {
        const auto t = targetAt (i);
        const auto& info = kTargets[static_cast<size_t> (i)];
        INFO (info.paramId);
        CHECK (info.modMin <= info.min);
        CHECK (info.modMax >= info.max);
        const bool wider = t == Target::lowLevel || t == Target::redLevel || t == Target::greenLevel || t == Target::violetLevel
                        || t == Target::detune || t == Target::drift || t == Target::excite;
        CHECK ((info.modMax > info.max) == wider);
        CHECK_THAT (info.modMin, WithinULP (info.min, 0) || WithinULP (0.0f, 0));
    }
    for (auto t : { Target::lowLevel, Target::redLevel, Target::greenLevel, Target::violetLevel })
        CHECK_THAT (infoOf (t).modMax, WithinULP (2.0f, 0));
    CHECK_THAT (infoOf (Target::detune).modMax, WithinULP (18.0f, 0));
    CHECK_THAT (infoOf (Target::drift).modMax, WithinULP (30.0f, 0));

    // one target without a knob: Strike, a fixed base of 1, modulated 0 … 2, by the mul law
    int internal = 0;
    for (int i = 0; i < kNumTargets; ++i)
        if (! hasParameter (targetAt (i)))
        {
            ++internal;
            CHECK (targetAt (i) == Target::excite);
        }
    CHECK (internal == 1);
    CHECK_THAT (infoOf (Target::excite).min, WithinULP (1.0f, 0));
    CHECK_THAT (infoOf (Target::excite).max, WithinULP (1.0f, 0));
    CHECK_THAT (infoOf (Target::excite).modMin, WithinULP (0.0f, 0));
    CHECK_THAT (infoOf (Target::excite).modMax, WithinULP (2.0f, 0));
    CHECK (std::string (kDestinations[indexOf (Destination::excite)].label) == "Strike");
    CHECK (std::string (kDestinations[indexOf (Destination::excite)].module) == "Resonators");
}

TEST_CASE ("Mod grid: every target with a knob is a parameter with the same range; the macros are parameters", "[mod][params]")
{
    juce::ScopedJuceInitialiser_GUI juceEnv;
    PluginProcessor p (false);
    for (const auto& t : kTargets)
    {
        if (t.paramId[0] == '\0')
        {
            // no knob: no parameter by its column's name either
            CHECK (p.getValueTreeState().getParameter (kDestinations[indexOf (t.column)].key) == nullptr);
            continue;
        }
        INFO (t.paramId);
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.getValueTreeState().getParameter (t.paramId));
        REQUIRE (param != nullptr);
        const auto& range = param->getNormalisableRange();
        CHECK_THAT (range.start, WithinAbs (t.min, 1.0e-6));
        CHECK_THAT (range.end, WithinAbs (t.max, 1.0e-6));
    }
    for (const auto& s : kSources)
        if (s.group == Group::macros)
        {
            INFO (s.key);
            CHECK (p.getValueTreeState().getParameter (s.key) != nullptr);
        }

    // parameters.yaml names the law of every target (mod_law), as kDestinations has it
    juce::File yaml (WORLDTABLE_FIXTURES_DIR "/../../spec/parameters.yaml");
    REQUIRE (yaml.existsAsFile());
    juce::StringArray lines;
    yaml.readLines (lines);
    int named = 0;
    for (const auto& line : lines)
    {
        const auto id = line.fromFirstOccurrenceOf ("id:", false, false).upToFirstOccurrenceOf (",", false, false).trim();
        const auto law = line.fromFirstOccurrenceOf ("mod_law:", false, false).upToFirstOccurrenceOf (",", false, false)
                             .upToFirstOccurrenceOf ("}", false, false).trim();
        if (id.isEmpty() || law.isEmpty())
            continue;
        ++named;
        bool found = false;
        for (int i = 0; i < kNumTargets; ++i)
            if (id == kTargets[static_cast<size_t> (i)].paramId)
            {
                found = true;
                const auto want = lawOf (targetAt (i));
                INFO (id);
                CHECK (law == (want == Law::mul ? "mul" : want == Law::add ? "add" : "oct"));
            }
        INFO (id << " has a mod_law but is not a target");
        CHECK (found);
    }
    CHECK (named == kNumTargets - 1);   // all but Strike

    // the LFOs' and the envelope's parameters, at the mockup's defaults (the headers' Settings)
    CHECK_THAT (defaultOf (p, "lfo1_rate"), WithinRel (0.12, 1.0e-4));
    CHECK_THAT (defaultOf (p, "lfo2_rate"), WithinRel (0.03, 1.0e-4));
    CHECK (static_cast<int> (defaultOf (p, "lfo1_shape")) == static_cast<int> (SlowLfo::Shape::sine));
    CHECK (static_cast<int> (defaultOf (p, "lfo2_shape")) == static_cast<int> (SlowLfo::Shape::smoothRandom));
    const Envelope::Settings env;
    CHECK_THAT (defaultOf (p, "env_attack"), WithinRel (env.attack, 1.0e-4));
    CHECK_THAT (defaultOf (p, "env_decay"), WithinRel (env.decay, 1.0e-4));
    CHECK_THAT (defaultOf (p, "env_sustain"), WithinRel (env.sustain, 1.0e-4));
    CHECK_THAT (defaultOf (p, "env_release"), WithinRel (env.release, 1.0e-4));
    CHECK (static_cast<int> (defaultOf (p, "env_trigger")) == static_cast<int> (env.trigger));
    for (const char* id : { "lfo1_rate", "lfo2_rate" })
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.getValueTreeState().getParameter (id));
        REQUIRE (param != nullptr);
        CHECK_THAT (param->getNormalisableRange().start, WithinRel (SlowLfo::kMinHz, 1.0e-6));
        CHECK_THAT (param->getNormalisableRange().end, WithinRel (SlowLfo::kMaxHz, 1.0e-6));
    }
    for (const char* id : { "env_attack", "env_decay", "env_release" })
    {
        auto* param = dynamic_cast<juce::RangedAudioParameter*> (p.getValueTreeState().getParameter (id));
        REQUIRE (param != nullptr);
        CHECK_THAT (param->getNormalisableRange().start, WithinRel (Envelope::kMinSeconds, 1.0e-6));
        CHECK_THAT (param->getNormalisableRange().end, WithinRel (Envelope::kMaxSeconds, 1.0e-6));
    }
}

TEST_CASE ("Mod grid: the default grid is today's wiring and nothing more", "[mod][params]")
{
    struct Route { Source source; Destination column; float depth; };
    const Route routes[] = {
        { Source::storm, Destination::excite, 0.6f },       { Source::storm, Destination::progression, 0.6f },
        { Source::breath, Destination::redLevel, -0.35f },  { Source::breath, Destination::greenLevel, -0.35f },
        { Source::breath, Destination::violetLevel, -0.5f },
        { Source::glow, Destination::violetLevel, 0.5f },   { Source::glow, Destination::reverbShimmer, 0.5f },
        { Source::space, Destination::delayMix, 0.5f },     { Source::space, Destination::reverbMix, 0.5f },
        { Source::drift, Destination::detune, 0.5f },       { Source::drift, Destination::drift, 0.5f },
        { Source::activity, Destination::eqCut, 0.3f },
    };
    Depths want;
    for (const auto& r : routes)
        want.set (r.source, r.column, r.depth);

    const auto grid = defaultDepths();
    int count = 0;
    for (int s = 0; s < kNumSources; ++s)
        for (int d = 0; d < kNumDestinations; ++d)
        {
            INFO (kSources[static_cast<size_t> (s)].key << " → " << kDestinations[static_cast<size_t> (d)].key);
            CHECK_THAT (grid.get (sourceAt (s), columnAt (d)), WithinULP (want.get (sourceAt (s), columnAt (d)), 0));
            count += std::abs (grid.get (sourceAt (s), columnAt (d))) > 0.0f ? 1 : 0;
        }
    CHECK (count == 12);
    for (auto level : { Destination::lowLevel, Destination::redLevel, Destination::greenLevel, Destination::violetLevel })
        CHECK_THAT (grid.get (Source::storm, level), WithinULP (0.0f, 0));   // Storm drives the exciter, not the levels
    for (auto s : { Source::onset, Source::lfo1, Source::lfo2, Source::envelope })   // the new sources: no routes
        for (int d = 0; d < kNumDestinations; ++d)
            CHECK_THAT (grid.get (s, columnAt (d)), WithinULP (0.0f, 0));

    // Activity → Cut is the old eq_sky at its default (the parameter went: state v6)
    juce::ScopedJuceInitialiser_GUI juceEnv;
    PluginProcessor p (false);
    CHECK (p.getValueTreeState().getParameter ("eq_sky") == nullptr);
    CHECK_THAT (grid.get (Source::activity, Destination::eqCut), WithinAbs (0.3, 1.0e-6));

    // a depth is clamped to −1 … +1; a non-finite one is no route
    Depths d;
    d.set (Source::lfo1, Destination::width, 2.0f);
    CHECK_THAT (d.get (Source::lfo1, Destination::width), WithinULP (1.0f, 0));
    d.set (Source::lfo1, Destination::width, -3.0f);
    CHECK_THAT (d.get (Source::lfo1, Destination::width), WithinULP (-1.0f, 0));
    d.set (Source::lfo1, Destination::width, std::numeric_limits<float>::quiet_NaN());
    CHECK_THAT (d.get (Source::lfo1, Destination::width), WithinULP (0.0f, 0));
}

//==================================================================================================
// ModMatrix (red until the engine PR)
//==================================================================================================
TEST_CASE ("ModMatrix: the mul law scales the base and clamps to the modulated range", "[mod]")
{
    CHECK_THAT (ModMatrix::apply (Target::redLevel, 0.5f, 0.5f), WithinAbs (0.75, 1.0e-6));
    CHECK_THAT (ModMatrix::apply (Target::redLevel, 0.5f, -0.5f), WithinAbs (0.25, 1.0e-6));
    CHECK_THAT (ModMatrix::apply (Target::redLevel, 0.8f, 0.5f), WithinAbs (1.2, 1.0e-6));     // a level may go past its knob …
    CHECK_THAT (ModMatrix::apply (Target::redLevel, 1.5f, 0.5f), WithinAbs (2.0, 1.0e-6));     // … up to 2 (+6 dB)
    CHECK_THAT (ModMatrix::apply (Target::redLevel, 0.5f, -1.5f), WithinAbs (0.0, 1.0e-6));    // and down to 0
    CHECK_THAT (ModMatrix::apply (Target::detune, 10.0f, 0.5f), WithinAbs (15.0, 1.0e-5));     // today's Drift 1
    CHECK_THAT (ModMatrix::apply (Target::detune, 15.0f, 0.5f), WithinAbs (18.0, 1.0e-5));
    CHECK_THAT (ModMatrix::apply (Target::drift, 20.0f, 1.0f), WithinAbs (30.0, 1.0e-5));
    CHECK_THAT (ModMatrix::apply (Target::excite, 1.0f, 0.6f), WithinAbs (1.6, 1.0e-6));       // Strike: Storm 1
    CHECK_THAT (ModMatrix::apply (Target::excite, 1.0f, 1.5f), WithinAbs (2.0, 1.0e-6));
    CHECK_THAT (ModMatrix::apply (Target::width, 0.8f, 0.5f), WithinAbs (1.0, 1.0e-6));        // the rest: the parameter's range
    CHECK_THAT (ModMatrix::apply (Target::dlyFb, 0.5f, 1.0f), WithinAbs (0.95, 1.0e-6));
    CHECK_THAT (ModMatrix::apply (Target::revDecay, 1.0f, -0.9f), WithinAbs (0.3, 1.0e-6));    // the decay's floor
    CHECK_THAT (ModMatrix::apply (Target::revDecay, 9.0f, 1.0f), WithinAbs (18.0, 1.0e-5));
    // no amount: untouched, bit for bit; a non-finite amount counts as none
    CHECK_THAT (ModMatrix::apply (Target::width, 0.123456f, 0.0f), WithinULP (0.123456f, 0));
    CHECK_THAT (ModMatrix::apply (Target::width, 0.5f, std::numeric_limits<float>::quiet_NaN()), WithinULP (0.5f, 0));
    CHECK_THAT (ModMatrix::apply (Target::width, 0.5f, std::numeric_limits<float>::infinity()), WithinULP (0.5f, 0));
}

TEST_CASE ("ModMatrix: the add law moves the EQ gains and the drives by half their range per unit", "[mod]")
{
    // gains: ±12 dB at amount ±1, from any base, 0 dB included
    CHECK_THAT (ModMatrix::apply (Target::eq2Gain, 0.0f, 1.0f), WithinAbs (12.0, 1.0e-5));
    CHECK_THAT (ModMatrix::apply (Target::eq2Gain, 0.0f, -0.5f), WithinAbs (-6.0, 1.0e-5));
    CHECK_THAT (ModMatrix::apply (Target::eq1Gain, -6.0f, 0.25f), WithinAbs (-3.0, 1.0e-5));
    CHECK_THAT (ModMatrix::apply (Target::eq3Gain, 6.0f, 1.0f), WithinAbs (12.0, 1.0e-5));     // clamped to the range
    CHECK_THAT (ModMatrix::apply (Target::eq3Gain, -6.0f, -1.0f), WithinAbs (-12.0, 1.0e-5));
    // drives: +0.5 at amount 1
    CHECK_THAT (ModMatrix::apply (Target::eq1Drive, 0.0f, 1.0f), WithinAbs (0.5, 1.0e-6));
    CHECK_THAT (ModMatrix::apply (Target::eq4Drive, 0.8f, 1.0f), WithinAbs (1.0, 1.0e-6));
    CHECK_THAT (ModMatrix::apply (Target::eq2Drive, 0.2f, -1.0f), WithinAbs (0.0, 1.0e-6));
    CHECK_THAT (ModMatrix::apply (Target::eq3Drive, 0.3f, 0.0f), WithinULP (0.3f, 0));
}

TEST_CASE ("ModMatrix: the oct law moves the cut in octaves (3 per unit) and clamps", "[mod]")
{
    CHECK_THAT (ModMatrix::apply (Target::eq4Freq, 1000.0f, 1.0f / 3.0f), WithinRel (2000.0, 1.0e-5));
    CHECK_THAT (ModMatrix::apply (Target::eq4Freq, 1000.0f, -1.0f / 3.0f), WithinRel (500.0, 1.0e-5));
    CHECK_THAT (ModMatrix::apply (Target::eq4Freq, 1000.0f, 2.0f / 3.0f), WithinRel (4000.0, 1.0e-5));
    CHECK_THAT (ModMatrix::apply (Target::eq4Freq, 10000.0f, 1.0f), WithinRel (20000.0, 1.0e-6));   // open
    CHECK_THAT (ModMatrix::apply (Target::eq4Freq, 400.0f, -1.0f), WithinRel (200.0, 1.0e-6));      // the floor
    CHECK_THAT (ModMatrix::apply (Target::eq4Freq, 20000.0f, 0.0f), WithinULP (20000.0f, 0));
}

TEST_CASE ("ModMatrix: routes multiply on a mul column and add on the others; Drive moves the four drives", "[mod]")
{
    Depths d;
    d.set (Source::storm, Destination::delayMix, 0.5f);
    d.set (Source::lfo1, Destination::delayMix, 0.25f);
    d.set (Source::envelope, Destination::delayMix, -0.5f);
    d.set (Source::lfo2, Destination::eqDrive, 0.5f);
    d.set (Source::activity, Destination::eqCut, 0.3f);
    d.set (Source::envelope, Destination::eqCut, 0.2f);
    d.set (Source::storm, Destination::eqMidGain, 0.5f);
    d.set (Source::lfo1, Destination::eqMidGain, 0.5f);

    SourceValues s;
    s[Source::storm] = 0.5f;
    s[Source::lfo1] = -0.5f;
    s[Source::lfo2] = 1.0f;
    s[Source::envelope] = 0.2f;
    s[Source::activity] = 1.0f;

    auto b = plainBases();
    b[Target::dlyMix] = 0.4f;
    b[Target::eq1Drive] = 0.2f;
    b[Target::eq2Drive] = 0.4f;
    b[Target::eq3Drive] = 0.8f;
    b[Target::eq4Drive] = 0.0f;
    b[Target::eq4Freq] = 1000.0f;
    b[Target::eq2Gain] = 3.3f;
    const auto f = evaluated (d, s, b);

    // (1 + 0.5·0.5)(1 + 0.25·(−0.5))(1 − 0.5·0.2) = 1.25 · 0.875 · 0.9
    const double factor = 1.25 * 0.875 * 0.9;
    CHECK_THAT (f.amount[indexOf (Destination::delayMix)], WithinAbs (factor - 1.0, 1.0e-6));
    CHECK_THAT (f[Target::dlyMix], WithinAbs (0.4 * factor, 1.0e-6));
    // Drive: + 0.5 · 1 · ½ to each of the four
    CHECK_THAT (f[Target::eq1Drive], WithinAbs (0.45, 1.0e-6));
    CHECK_THAT (f[Target::eq2Drive], WithinAbs (0.65, 1.0e-6));
    CHECK_THAT (f[Target::eq3Drive], WithinAbs (1.0, 1.0e-6));   // clamped
    CHECK_THAT (f[Target::eq4Drive], WithinAbs (0.25, 1.0e-6));
    // 0.3·1 + 0.2·0.2 = 0.34 → 2^1.02
    CHECK_THAT (f[Target::eq4Freq], WithinRel (1000.0 * std::exp2 (3.0 * 0.34), 1.0e-5));
    // add routes that cancel exactly leave the base untouched
    CHECK_THAT (f[Target::eq2Gain], WithinULP (b[Target::eq2Gain], 0));
    // the sources used are in the frame
    CHECK_THAT (f.source[indexOf (Source::lfo2)], WithinULP (1.0f, 0));

    // a product never flips a sign: two routes pushing all the way down give 0, not less
    Depths down;
    down.set (Source::storm, Destination::reverbMix, -1.0f);
    down.set (Source::glow, Destination::reverbMix, -1.0f);
    SourceValues full;
    full[Source::storm] = 1.0f;
    full[Source::glow] = 1.0f;
    CHECK_THAT (evaluated (down, full, b)[Target::revMix], WithinAbs (0.0, 1.0e-9));
    full[Source::storm] = 0.5f;
    CHECK_THAT (evaluated (down, full, b)[Target::revMix], WithinAbs (0.0, 1.0e-9));
    full[Source::glow] = 0.5f;
    CHECK_THAT (evaluated (down, full, b)[Target::revMix], WithinAbs (0.35 * 0.25, 1.0e-6));
}

TEST_CASE ("ModMatrix: an all-zero grid leaves every target at its base, bit for bit", "[mod]")
{
    // bases anywhere, even outside the range: with no route nothing is touched, not even clamped
    const float odd[] = { 0.123456f, 1.0f, 0.0f, -12.0f, 19999.9f, 0.3f, 1.25f, -0.0f, 7.77e-3f, 0.95f, 60.0f };
    Bases b;
    for (int i = 0; i < kNumTargets; ++i)
        b.value[static_cast<size_t> (i)] = odd[static_cast<size_t> (i) % std::size (odd)];
    SourceValues s;
    for (int i = 0; i < kNumSources; ++i)
        s.value[static_cast<size_t> (i)] = (i % 2 == 0 ? 0.77f : -0.41f);

    const auto f = evaluated (Depths {}, s, b);
    for (int i = 0; i < kNumTargets; ++i)
    {
        INFO (kTargets[static_cast<size_t> (i)].paramId);
        const auto t = targetAt (i);
        CHECK_THAT (f[t], WithinULP (b[t], 0));
        CHECK_THAT (f.lo[indexOf (t)], WithinULP (b[t], 0));
        CHECK_THAT (f.hi[indexOf (t)], WithinULP (b[t], 0));
    }
    for (int d = 0; d < kNumDestinations; ++d)
        CHECK_THAT (f.amount[static_cast<size_t> (d)], WithinULP (0.0f, 0));
    REQUIRE_THAT (f.source[0], WithinULP (0.77f, 0));   // the frame was filled
}

TEST_CASE ("ModMatrix: the default grid at the macros' centres and no breath leaves the bases untouched", "[mod]")
{
    // What keeps the default patch where it is: every macro at 0.5 (x = 0) and no breath or
    // activity give amounts of exactly 0, so every target is its knob, bit for bit.
    Knobs k;
    k.breath = 0.35;
    k.cycle = 0.0;
    const auto b = plainBases();
    const auto f = evaluated (defaultDepths(), sourcesOf (k), b);
    for (int i = 0; i < kNumTargets; ++i)
    {
        INFO (kTargets[static_cast<size_t> (i)].paramId);
        CHECK_THAT (f[targetAt (i)], WithinULP (b[targetAt (i)], 0));
    }
    // the ranges are not empty though: the rings show where the macros could take them
    REQUIRE (f.hi[indexOf (Target::dlyMix)] > f.lo[indexOf (Target::dlyMix)]);
}

TEST_CASE ("ModMatrix: the default grid plays today's macros at every setting, alone and together", "[mod]")
{
    const auto grid = defaultDepths();
    auto bases = plainBases();
    const auto check = [&] (const Knobs& k, const char* what)
    {
        const auto f = evaluated (grid, sourcesOf (k), bases);
        for (int i = 0; i < kNumTargets; ++i)
        {
            const auto t = targetAt (i);
            const double want = today (t, bases, k);
            INFO (what << " storm " << k.storm << " breath " << k.breath << " (cycle " << k.cycle << ") glow " << k.glow
                       << " space " << k.space << " drift " << k.drift << " activity " << k.activity << ": " << infoOf (t).paramId);
            const double y = k.breath * k.cycle;
            if (t == Target::violetLevel && y > 0.0)
            {
                // the one exception: violet's breath is quadratic, the route linear (slope matched)
                const double grid1 = bases[t] * macroGain (k.glow) * (1.0 - 0.5 * y);
                CHECK_THAT (static_cast<double> (f[t]), WithinRel (grid1, 1.0e-5));
                CHECK (std::abs (dbOf (f[t] / want)) <= (k.breath <= 0.35 ? 0.07 : 0.87));
            }
            else
            {
                CHECK_THAT (static_cast<double> (f[t]), WithinAbs (want, 1.0e-6) || WithinRel (want, 1.0e-5));
            }
        }
        return f;
    };

    for (double m : { 0.0, 0.5, 1.0 })
    {
        Knobs k;            // one macro at a time, the others at their centres, no breath
        k.breath = 0.0;
        k.storm = m;
        check (k, "Storm");
        k.storm = 0.5;
        k.glow = m;
        check (k, "Glow");
        k.glow = 0.5;
        k.space = m;
        check (k, "Space");
        k.space = 0.5;
        k.drift = m;
        check (k, "Drift");
        k.drift = 0.5;
        k.activity = m;
        check (k, "Activity");
        k.activity = 0.0;
        for (double c : { 0.0, 0.5, 1.0 })
        {
            k.breath = m;
            k.cycle = c;
            check (k, "Breath");
        }
    }

    // together (routes multiply, as the macros did), at the knobs' tops too: violet at 1 with Glow
    // 1 goes to 1.5, Detune 10 and Drift 16 at Drift 1 to 15 and 24 ct
    bases[Target::redLevel] = 1.0f;
    bases[Target::violetLevel] = 1.0f;
    bases[Target::detune] = 10.0f;
    bases[Target::drift] = 16.0f;
    bases[Target::revMix] = 0.9f;
    for (double storm : { 0.0, 0.3, 1.0 })
        for (double glow : { 0.0, 0.7, 1.0 })
            for (double space : { 0.0, 1.0 })
                for (double breath : { 0.0, 0.35, 1.0 })
                {
                    Knobs k;
                    k.storm = storm;
                    k.glow = glow;
                    k.space = space;
                    k.drift = 1.0 - glow;
                    k.breath = breath;
                    k.cycle = 1.0;
                    k.activity = storm;
                    check (k, "together");
                }

    // the default patch through a whole breath: what the goldens' patch hears
    bases = plainBases();
    for (double c : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        Knobs k;
        k.cycle = c;
        const auto f = check (k, "default patch");
        REQUIRE (f[Target::redLevel] > 0.0f);
    }
}

TEST_CASE ("ModMatrix: where the default grid would have departed from today, it doesn't", "[mod]")
{
    // The header's first draft summed the routes and clamped the levels to their knobs. With the
    // owner's product law, modulated ranges and Strike, these cases sound as today.
    const auto grid = defaultDepths();
    auto b = plainBases();
    b[Target::redLevel] = 1.0f;
    b[Target::violetLevel] = 1.0f;

    // Storm 1 strikes 1.6 × harder (the exciter), and leaves the levels alone
    Knobs k;
    k.breath = 0.0;
    k.storm = 1.0;
    auto f = evaluated (grid, sourcesOf (k), b);
    CHECK_THAT (f[Target::excite], WithinAbs (1.6, 1.0e-6));
    CHECK_THAT (f[Target::redLevel], WithinULP (1.0f, 0));
    k.storm = 0.0;
    CHECK_THAT (evaluated (grid, sourcesOf (k), b)[Target::excite], WithinAbs (0.4, 1.0e-6));
    // Glow 1 takes violet at its knob's top to 1.5
    k.storm = 0.5;
    k.glow = 1.0;
    CHECK_THAT (evaluated (grid, sourcesOf (k), b)[Target::violetLevel], WithinAbs (1.5, 1.0e-6));
    // Glow 0 with a full breath (knob 0.35): 0.5 · (1 − 0.175), never 0
    Knobs g0;
    g0.glow = 0.0;
    g0.cycle = 1.0;
    CHECK_THAT (evaluated (grid, sourcesOf (g0), b)[Target::violetLevel], WithinAbs (0.5 * (1.0 - 0.5 * 0.35), 1.0e-6));
}

TEST_CASE ("ModMatrix: the reachable range of every target, for the rings", "[mod]")
{
    auto b = plainBases();
    b[Target::redLevel] = 1.0f;
    Knobs k;   // the default knobs, Breath 0.35
    const auto f = evaluated (defaultDepths(), sourcesOf (k), b);

    // Space ± 1 at 0.5 on a delay mix of 0.25
    CHECK_THAT (f.amountLo[indexOf (Destination::delayMix)], WithinAbs (-0.5, 1.0e-6));
    CHECK_THAT (f.amountHi[indexOf (Destination::delayMix)], WithinAbs (0.5, 1.0e-6));
    CHECK_THAT (f.lo[indexOf (Target::dlyMix)], WithinAbs (0.125, 1.0e-6));
    CHECK_THAT (f.hi[indexOf (Target::dlyMix)], WithinAbs (0.375, 1.0e-6));
    // red: Breath 0 … −0.35·0.35
    CHECK_THAT (f.amountLo[indexOf (Destination::redLevel)], WithinAbs (-0.35 * 0.35, 1.0e-6));
    CHECK_THAT (f.amountHi[indexOf (Destination::redLevel)], WithinAbs (0.0, 1.0e-6));
    CHECK_THAT (f.lo[indexOf (Target::redLevel)], WithinAbs (1.0 - 0.35 * 0.35, 1.0e-6));
    CHECK_THAT (f.hi[indexOf (Target::redLevel)], WithinAbs (1.0, 1.0e-6));
    // violet: the product of each route's extremes, Glow (0.5 … 1.5) × Breath (1 − 0.5·0.35 … 1)
    CHECK_THAT (f.lo[indexOf (Target::violetLevel)], WithinAbs (0.3 * 0.5 * (1.0 - 0.5 * 0.35), 1.0e-6));
    CHECK_THAT (f.hi[indexOf (Target::violetLevel)], WithinAbs (0.3 * 1.5, 1.0e-6));
    // Strike: Storm ± 1 at 0.6
    CHECK_THAT (f.lo[indexOf (Target::excite)], WithinAbs (0.4, 1.0e-6));
    CHECK_THAT (f.hi[indexOf (Target::excite)], WithinAbs (1.6, 1.0e-6));
    // the cut: Activity 0 … 1 at 0.3 → up to 0.9 octaves
    CHECK_THAT (f.lo[indexOf (Target::eq4Freq)], WithinRel (2000.0, 1.0e-6));
    CHECK_THAT (f.hi[indexOf (Target::eq4Freq)], WithinRel (2000.0 * std::exp2 (0.9), 1.0e-5));
    // no route: the range is the base
    CHECK_THAT (f.lo[indexOf (Target::width)], WithinULP (b[Target::width], 0));
    CHECK_THAT (f.hi[indexOf (Target::width)], WithinULP (b[Target::width], 0));

    // Breath's range follows its knob
    k.breath = 1.0;
    const auto full = evaluated (defaultDepths(), sourcesOf (k), b);
    CHECK_THAT (full.amountLo[indexOf (Destination::redLevel)], WithinAbs (-0.35, 1.0e-6));

    // an add route around a negative gain: lo ≤ hi
    Depths d;
    d.set (Source::lfo1, Destination::eqLowGain, 0.5f);
    b[Target::eq1Gain] = -6.0f;
    const auto g = evaluated (d, SourceValues {}, b);
    CHECK_THAT (g.lo[indexOf (Target::eq1Gain)], WithinAbs (-12.0, 1.0e-5));
    CHECK_THAT (g.hi[indexOf (Target::eq1Gain)], WithinAbs (0.0, 1.0e-5));
    CHECK_THAT (g[Target::eq1Gain], WithinULP (-6.0f, 0));   // LFO at 0 now

    // a mul route pushing down: the range turns around (lo from the source's top)
    Depths m;
    m.set (Source::envelope, Destination::chorusMix, -1.0f);
    const auto h = evaluated (m, SourceValues {}, b);
    CHECK_THAT (h.lo[indexOf (Target::chMix)], WithinAbs (0.0, 1.0e-9));
    CHECK_THAT (h.hi[indexOf (Target::chMix)], WithinAbs (0.25, 1.0e-6));
}

TEST_CASE ("ModMatrix: a new table glides, 1 per second, and never jumps", "[mod]")
{
    ModMatrix m;
    m.reset (Depths {});
    Depths one;
    one.set (Source::lfo1, Destination::width, 1.0f);
    m.setDepths (one);

    SourceValues s;
    s[Source::lfo1] = 1.0f;
    Bases b;
    b[Target::width] = 0.25f;
    Frame f;
    for (int k = 1; k <= 100; ++k)
    {
        m.tick (s, b, f);
        INFO ("tick " << k);
        CHECK_THAT (m.depths().get (Source::lfo1, Destination::width), WithinAbs (0.01 * k, 1.0e-5));
        CHECK_THAT (f[Target::width], WithinAbs (0.25 * (1.0 + 0.01 * k), 1.0e-5));
    }
    CHECK_THAT (m.depths().get (Source::lfo1, Destination::width), WithinULP (1.0f, 0));   // arrives exactly
    CHECK_FALSE (m.gliding());

    // +1 → −1 takes 2 s, a step of at most 0.01 per tick
    Depths minus;
    minus.set (Source::lfo1, Destination::width, -1.0f);
    m.setDepths (minus);
    REQUIRE (m.gliding());
    float was = m.depths().get (Source::lfo1, Destination::width);
    int ticks = 0;
    while (m.gliding() && ticks < 1000)
    {
        m.tick (s, b, f);
        const float now = m.depths().get (Source::lfo1, Destination::width);
        CHECK (std::abs (now - was) <= ModMatrix::kDepthPerTick + 1.0e-6f);
        was = now;
        ++ticks;
    }
    CHECK (ticks >= 199);
    CHECK (ticks <= 201);
    CHECK_THAT (f[Target::width], WithinAbs (0.0, 1.0e-6));

    // reset() takes a table at once
    m.reset (defaultDepths());
    CHECK_FALSE (m.gliding());
    CHECK_THAT (m.depths().get (Source::storm, Destination::progression), WithinULP (0.6f, 0));
}

TEST_CASE ("ModMatrix: a non-finite source is ignored; a tick allocates nothing", "[mod]")
{
    Depths d = defaultDepths();
    d.set (Source::lfo1, Destination::width, 0.5f);
    auto b = plainBases();

    SourceValues s = sourcesOf (Knobs {});
    s[Source::space] = 1.0f;
    s[Source::lfo1] = std::numeric_limits<float>::quiet_NaN();
    ModMatrix m;
    m.reset (d);
    Frame f;

    const auto before = testing::allocationsOnThisThread();
    for (int i = 0; i < 1000; ++i)
        m.tick (s, b, f);
    CHECK (testing::allocationsOnThisThread() - before == 0);

    CHECK_THAT (f[Target::width], WithinULP (b[Target::width], 0));
    CHECK_THAT (f.source[indexOf (Source::lfo1)], WithinULP (0.0f, 0));
    REQUIRE_THAT (f[Target::dlyMix], WithinAbs (0.375, 1.0e-6));   // Space at 1, 0.25 × 1.5
}

//==================================================================================================
// SlowLfo (red until the engine PR)
//==================================================================================================
TEST_CASE ("SlowLfo: sine and triangle, as the mockup draws them", "[mod][lfo]")
{
    for (auto shape : { SlowLfo::Shape::sine, SlowLfo::Shape::triangle })
    {
        auto l = makeLfo (shape, 1.0);
        float peak = 0.0f;
        for (int k = 1; k <= 100; ++k)
        {
            l.tick();
            const double p = std::fmod (k / 100.0, 1.0);
            const double want = shape == SlowLfo::Shape::sine ? std::sin (2.0 * kPi * p) : 1.0 - 4.0 * std::abs (p - 0.5);
            INFO ((shape == SlowLfo::Shape::sine ? "sine" : "triangle") << " tick " << k);
            CHECK (circular (l.phase(), p) < 1.0e-9);
            CHECK_THAT (l.value(), WithinAbs (want, 1.0e-5));
            CHECK_THAT (l.value(), WithinAbs (SlowLfo::shapeAt (shape, 1.0, 1, 0, l.cycle(), l.phase()), 1.0e-6));
            peak = std::max (peak, std::abs (l.value()));
        }
        CHECK (l.cycle() == 1u);
        REQUIRE (peak > 0.99f);
    }
}

TEST_CASE ("SlowLfo: the rate is clamped to 0.01 … 2 Hz; cycles are counted", "[mod][lfo]")
{
    SlowLfo l;
    l.prepare (1, 0);
    l.setRate (5.0);
    CHECK_THAT (l.rate(), WithinULP (SlowLfo::kMaxHz, 0));
    l.setRate (0.001);
    CHECK_THAT (l.rate(), WithinULP (SlowLfo::kMinHz, 0));

    auto m = makeLfo (SlowLfo::Shape::sine, 0.37);
    run (m, 10050);   // 100.5 s: 37.185 cycles
    CHECK (m.cycle() == 37u);
    CHECK_THAT (m.phase(), WithinAbs (0.185, 1.0e-9));
}

TEST_CASE ("SlowLfo: a rate change keeps the phase, so the output never jumps", "[mod][lfo]")
{
    auto l = makeLfo (SlowLfo::Shape::sine, 0.1);
    run (l, 300);
    CHECK_THAT (l.phase(), WithinAbs (0.3, 1.0e-9));
    l.setRate (2.0);
    float was = l.value(), peak = std::abs (was), worst = 0.0f;
    l.tick();
    CHECK_THAT (l.phase(), WithinAbs (0.32, 1.0e-9));
    worst = std::abs (l.value() - was);
    was = l.value();
    for (int i = 0; i < 300; ++i)
    {
        l.tick();
        worst = std::max (worst, std::abs (l.value() - was));
        peak = std::max (peak, std::abs (l.value()));
        was = l.value();
    }
    CHECK (worst <= static_cast<float> (2.0 * kPi * 2.0 * SlowLfo::kTickSeconds) + 1.0e-5f);
    REQUIRE (peak > 0.99f);
}

TEST_CASE ("SlowLfo: the random shapes are seeded; smooth random is smooth, stepped random holds", "[mod][lfo]")
{
    // the knots: −1 … 1, spread, the same for the same seed and stream
    float lo = 1.0f, hi = -1.0f;
    for (uint64_t i = 0; i < 32; ++i)
    {
        const float k = SlowLfo::knot (1, 0, i);
        CHECK (std::abs (k) <= 1.0f);
        CHECK_THAT (SlowLfo::knot (1, 0, i), WithinULP (k, 0));
        CHECK_THAT (SlowLfo::shapeAt (SlowLfo::Shape::smoothRandom, 0.5, 1, 0, i, 0.0), WithinULP (k, 0));   // passes each knot
        lo = std::min (lo, k);
        hi = std::max (hi, k);
    }
    REQUIRE (hi - lo > 0.5f);

    // same seed and stream → the same output; another stream (LFO 2) or seed → another
    auto a = makeLfo (SlowLfo::Shape::smoothRandom, 0.7, 1, 0);
    auto b = makeLfo (SlowLfo::Shape::smoothRandom, 0.7, 1, 0);
    auto c = makeLfo (SlowLfo::Shape::smoothRandom, 0.7, 1, 1);
    auto d = makeLfo (SlowLfo::Shape::smoothRandom, 0.7, 2, 0);
    const auto va = run (a, 3000), vb = run (b, 3000), vc = run (c, 3000), vd = run (d, 3000);
    float diffC = 0.0f, diffD = 0.0f;
    for (size_t i = 0; i < va.size(); ++i)
    {
        CHECK_THAT (va[i], WithinULP (vb[i], 0));
        diffC = std::max (diffC, std::abs (va[i] - vc[i]));
        diffD = std::max (diffD, std::abs (va[i] - vd[i]));
    }
    CHECK (diffC > 0.1f);
    CHECK (diffD > 0.1f);

    // smooth random at 2 Hz: a step per tick at most 2 (knots) × 1.5 (the smoothstep's slope) × 0.02
    auto s = makeLfo (SlowLfo::Shape::smoothRandom, 2.0);
    const auto vs = run (s, 2000);
    for (size_t i = 1; i < vs.size(); ++i)
        CHECK (std::abs (vs[i] - vs[i - 1]) <= 0.0605f);
    REQUIRE (peakOf (vs) > 0.3f);

    // stepped random at 0.5 Hz (200 ticks a cycle): k_i through cycle i once its 50 ms glide is done
    auto st = makeLfo (SlowLfo::Shape::steppedRandom, 0.5);
    int held = 0;
    for (int t = 0; t < 2000; ++t)
    {
        st.tick();
        const auto i = st.cycle();
        if (i < 1)
            continue;
        const float k = SlowLfo::knot (1, 0, i), prev = SlowLfo::knot (1, 0, i - 1);
        if (st.phase() >= 0.05)
        {
            CHECK_THAT (st.value(), WithinAbs (k, 1.0e-6));
            ++held;
        }
        else
        {
            CHECK (st.value() >= std::min (k, prev) - 1.0e-6f);
            CHECK (st.value() <= std::max (k, prev) + 1.0e-6f);
        }
    }
    REQUIRE (held > 1500);
}

TEST_CASE ("SlowLfo: every shape stays within −1 … 1; a shape change crossfades", "[mod][lfo]")
{
    for (int sh = 0; sh < SlowLfo::kNumShapes; ++sh)
        for (double hz : { 0.01, 0.3, 2.0 })
        {
            auto l = makeLfo (static_cast<SlowLfo::Shape> (sh), hz, 9, 1);
            const auto v = run (l, 3000);
            INFO ("shape " << sh << " at " << hz << " Hz");
            CHECK (peakOf (v) <= 1.0f + 1.0e-6f);
            if (hz > 1.0)
                REQUIRE (peakOf (v) > 0.3f);
        }

    auto l = makeLfo (SlowLfo::Shape::sine, 0.5);
    run (l, 30);
    REQUIRE (l.value() > 0.5f);   // sin(2π·0.15)
    l.setShape (SlowLfo::Shape::steppedRandom);
    // per tick at most: the fade's share of a full swing (2) plus the sine's own slope
    const auto bound = static_cast<float> (2.0 * SlowLfo::kTickSeconds / SlowLfo::kShapeFadeSeconds
                                           + 2.0 * kPi * 0.5 * SlowLfo::kTickSeconds) + 1.0e-5f;
    float was = l.value();
    for (int i = 0; i < 25; ++i)   // kShapeFadeSeconds
    {
        l.tick();
        CHECK (std::abs (l.value() - was) <= bound);
        was = l.value();
    }
    run (l, 5);
    CHECK (l.shape() == SlowLfo::Shape::steppedRandom);
    CHECK_THAT (l.value(), WithinAbs (SlowLfo::shapeAt (SlowLfo::Shape::steppedRandom, 0.5, 1, 0, l.cycle(), l.phase()), 1.0e-6));
}

TEST_CASE ("SlowLfo: the phase is double precision (a day at 0.0123 Hz)", "[mod][lfo]")
{
    auto l = makeLfo (SlowLfo::Shape::triangle, 0.0123);
    for (int i = 0; i < 8640000; ++i)   // 24 h of ticks: 1062.72 cycles
        l.tick();
    CHECK (l.cycle() == 1062u);
    CHECK_THAT (l.phase(), WithinAbs (0.72, 1.0e-6));
}

TEST_CASE ("SlowLfo: set every block, ticked on the grid, it is the same at any block size", "[mod][lfo]")
{
    // The engine sets the rate and the shape every block (the same values) and ticks on the sky's
    // absolute grid (480 samples at 48 kHz). Neither may restart the phase or the fade.
    const auto runInBlocks = [] (int block)
    {
        SlowLfo l;
        l.prepare (5, 1);
        l.setShape (SlowLfo::Shape::smoothRandom, false);
        l.setRate (0.9);
        std::vector<float> out;
        constexpr int kPerTick = 480, kTotal = kPerTick * 3000;
        for (int start = 0; start < kTotal; start += block)
        {
            l.setRate (0.9);
            l.setShape (SlowLfo::Shape::smoothRandom);
            const int end = std::min (kTotal, start + block);
            for (int smp = start; smp < end; ++smp)
                if (smp % kPerTick == 0)
                    out.push_back (l.tick());
        }
        return out;
    };
    const auto a = runInBlocks (64), b = runInBlocks (997), c = runInBlocks (4096);
    REQUIRE (a.size() == b.size());
    REQUIRE (a.size() == c.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        CHECK_THAT (a[i], WithinULP (b[i], 0));
        CHECK_THAT (a[i], WithinULP (c[i], 0));
    }
    REQUIRE (peakOf (a) > 0.3f);
}

//==================================================================================================
// Envelope (red until the engine PR)
//==================================================================================================
TEST_CASE ("Envelope: attack, decay, a 1.2 s hold, release (Fire)", "[mod][env]")
{
    auto e = makeEnv (Envelope::Trigger::fire);
    e.fire();
    for (int t = 1; t <= 200; ++t)
    {
        e.tick();
        INFO ("tick " << t);
        CHECK_THAT (e.value(), WithinAbs (firedAt (t), 1.0e-5));
        if (t == 5)
        {
            CHECK (e.stage() == Envelope::Stage::attack);
            CHECK_THAT (e.progress(), WithinAbs (0.5, 1.0e-5));
        }
        if (t == 20)
        {
            CHECK (e.stage() == Envelope::Stage::decay);
            CHECK_THAT (e.progress(), WithinAbs (0.5, 1.0e-5));
        }
        if (t == 90)
        {
            CHECK (e.stage() == Envelope::Stage::sustain);
            CHECK_THAT (e.progress(), WithinAbs (0.5, 1.0e-5));   // 60 of the hold's 120 ticks
            CHECK_FALSE (e.view().held);
        }
        if (t == 165)
        {
            CHECK (e.stage() == Envelope::Stage::release);
            CHECK_THAT (e.progress(), WithinAbs (0.5, 1.0e-5));
        }
    }
    CHECK (e.stage() == Envelope::Stage::idle);
    CHECK_THAT (e.progress(), WithinAbs (0.0, 1.0e-9));
    REQUIRE_THAT (firedAt (5), WithinAbs (0.5, 1.0e-12));
}

TEST_CASE ("Envelope: a note holds sustain while down; let go early, it releases from where it is", "[mod][env]")
{
    auto e = makeEnv (Envelope::Trigger::note);
    e.noteOn (60);
    for (int t = 1; t <= 300; ++t)
        e.tick();
    CHECK (e.stage() == Envelope::Stage::sustain);
    CHECK_THAT (e.value(), WithinAbs (0.5, 1.0e-6));
    CHECK_THAT (e.progress(), WithinAbs (0.0, 1.0e-9));
    CHECK (e.view().held);
    e.noteOff (60);
    for (int t = 1; t <= 30; ++t)
    {
        e.tick();
        INFO ("release tick " << t);
        CHECK (e.stage() == Envelope::Stage::release);
        CHECK_THAT (e.value(), WithinAbs (releaseAt (t / 30.0, 0.5), 1.0e-5));
    }
    e.tick();
    CHECK (e.stage() == Envelope::Stage::idle);

    // let go during the attack: the release starts on the next tick, from 0.5
    auto early = makeEnv (Envelope::Trigger::note);
    early.noteOn (60);
    for (int t = 0; t < 5; ++t)
        early.tick();
    REQUIRE_THAT (early.value(), WithinAbs (0.5, 1.0e-5));
    early.noteOff (60);
    early.tick();
    CHECK (early.stage() == Envelope::Stage::release);
    CHECK_THAT (early.value(), WithinAbs (releaseAt (1.0 / 30.0, 0.5), 1.0e-5));
    for (int t = 0; t < 14; ++t)
        early.tick();
    CHECK_THAT (early.value(), WithinAbs (0.125, 1.0e-5));
}

TEST_CASE ("Envelope: Onset, Note or Fire only; Fire fires in every mode; an onset holds 1.2 s", "[mod][env]")
{
    using T = Envelope::Trigger;
    constexpr int kOnset = 0, kNote = 1, kFire = 2;
    CHECK (firesOn (T::onset, kOnset));
    CHECK_FALSE (firesOn (T::onset, kNote));
    CHECK (firesOn (T::onset, kFire));
    CHECK_FALSE (firesOn (T::note, kOnset));
    CHECK (firesOn (T::note, kNote));
    CHECK (firesOn (T::note, kFire));
    CHECK_FALSE (firesOn (T::fire, kOnset));
    CHECK_FALSE (firesOn (T::fire, kNote));
    REQUIRE (firesOn (T::fire, kFire));

    auto e = makeEnv (T::onset);
    e.onset();
    for (int t = 1; t <= 150; ++t)
        e.tick();
    CHECK (e.stage() == Envelope::Stage::sustain);
    e.tick();
    CHECK (e.stage() == Envelope::Stage::release);

    // leaving Note mode lets go of a held envelope
    auto n = makeEnv (T::note);
    n.noteOn (60);
    for (int t = 0; t < 100; ++t)
        n.tick();
    REQUIRE (n.stage() == Envelope::Stage::sustain);
    n.setSettings (envSettings (T::onset));
    n.tick();
    CHECK (n.stage() == Envelope::Stage::release);
}

TEST_CASE ("Envelope: a retrigger rises from where it is and restarts the hold; notes overlap", "[mod][env]")
{
    // in the release (0.125 at tick 165): the attack starts from there, never from 0
    auto e = makeEnv (Envelope::Trigger::fire);
    e.fire();
    for (int t = 1; t <= 165; ++t)
        e.tick();
    REQUIRE_THAT (e.value(), WithinAbs (0.125, 1.0e-5));
    e.fire();
    float was = e.value();
    for (int t = 166; t <= 174; ++t)
    {
        e.tick();
        INFO ("tick " << t);
        CHECK (e.value() >= was);
        CHECK (e.stage() == Envelope::Stage::attack);
        was = e.value();
    }
    CHECK_THAT (e.value(), WithinAbs (1.0, 1.0e-5));   // 0.125 + 9 × 0.1, capped at 1
    CHECK_THAT (e.progress(), WithinAbs (1.0, 1.0e-5));

    // in the hold: the hold starts again (it would have ended at tick 150)
    auto h = makeEnv (Envelope::Trigger::fire);
    h.fire();
    for (int t = 1; t <= 90; ++t)
        h.tick();
    h.fire();
    h.tick();
    CHECK_THAT (h.value(), WithinAbs (0.6, 1.0e-5));
    for (int t = 92; t <= 200; ++t)
        h.tick();
    CHECK (h.stage() == Envelope::Stage::sustain);

    // two keys: each note-on fires; the envelope releases with the last key
    auto n = makeEnv (Envelope::Trigger::note);
    n.noteOn (60);
    for (int t = 0; t < 50; ++t)
        n.tick();
    n.noteOn (64);
    n.tick();
    CHECK_THAT (n.value(), WithinAbs (0.6, 1.0e-5));
    n.noteOff (60);
    for (int t = 0; t < 150; ++t)
        n.tick();
    CHECK (n.stage() == Envelope::Stage::sustain);
    n.noteOff (64);
    n.tick();
    CHECK (n.stage() == Envelope::Stage::release);
}

TEST_CASE ("Envelope: events wait for the next tick; the same onsets and notes give the same output at any block size", "[mod][env]")
{
    auto e = makeEnv (Envelope::Trigger::fire);
    e.fire();
    CHECK (e.stage() == Envelope::Stage::idle);
    CHECK_THAT (e.value(), WithinULP (0.0f, 0));
    e.tick();
    CHECK_THAT (e.value(), WithinAbs (0.1, 1.0e-6));

    // Notes at their samples (the block split there, as Play mode does), settings set every
    // block, ticks on the absolute grid (480 samples at 48 kHz). The last note is shorter than
    // a tick.
    const auto runInBlocks = [] (int block)
    {
        auto env = makeEnv (Envelope::Trigger::note);
        struct Event { int at; bool on; };
        constexpr Event kEvents[] = { { 12345, true }, { 98765, false }, { 150000, true }, { 150100, false } };
        size_t next = 0;
        std::vector<float> out;
        constexpr int kPerTick = 480, kTotal = kPerTick * 400;
        for (int start = 0; start < kTotal; start += block)
        {
            env.setSettings (envSettings (Envelope::Trigger::note));
            const int end = std::min (kTotal, start + block);
            for (int smp = start; smp < end; ++smp)
            {
                while (next < std::size (kEvents) && kEvents[next].at == smp)
                {
                    if (kEvents[next].on)
                        env.noteOn (60);
                    else
                        env.noteOff (60);
                    ++next;
                }
                if (smp % kPerTick == 0)
                    out.push_back (env.tick());
            }
        }
        return out;
    };
    const auto a = runInBlocks (64), b = runInBlocks (997), c = runInBlocks (4096);
    REQUIRE (a.size() == b.size());
    REQUIRE (a.size() == c.size());
    for (size_t i = 0; i < a.size(); ++i)
    {
        CHECK_THAT (a[i], WithinULP (b[i], 0));
        CHECK_THAT (a[i], WithinULP (c[i], 0));
    }
    // the first note fires on the first tick after sample 12345: the tick at 12480 (index 26)
    CHECK_THAT (a[25], WithinULP (0.0f, 0));
    REQUIRE (a[26] > 0.0f);
    // the short note: one tick of attack (the tick at 150240, index 313), then the release
    CHECK (a[313] > 0.0f);
    CHECK (a[314] < a[313]);
}

TEST_CASE ("Envelope: times clamp to 10 ms … 10 s, the sustain to 0 … 1", "[mod][env]")
{
    Envelope e;
    e.reset();
    Envelope::Settings s = envSettings (Envelope::Trigger::fire);
    s.attack = 0.0;
    s.decay = 100.0;
    s.sustain = 1.5;
    s.release = -1.0;
    e.setSettings (s);
    CHECK_THAT (e.getSettings().attack, WithinULP (Envelope::kMinSeconds, 0));
    CHECK_THAT (e.getSettings().decay, WithinULP (Envelope::kMaxSeconds, 0));
    CHECK_THAT (e.getSettings().sustain, WithinULP (1.0, 0));
    CHECK_THAT (e.getSettings().release, WithinULP (Envelope::kMinSeconds, 0));

    e.fire();
    e.tick();
    CHECK_THAT (e.value(), WithinAbs (1.0, 1.0e-6));   // a 10 ms attack is one tick

    auto slow = makeEnv (Envelope::Trigger::fire);
    s = envSettings (Envelope::Trigger::fire);
    s.attack = 100.0;
    slow.setSettings (s);
    slow.fire();
    for (int t = 0; t < 100; ++t)
        slow.tick();
    REQUIRE_THAT (slow.value(), WithinAbs (0.1, 1.0e-5));   // 1 s of a 10 s attack
}
