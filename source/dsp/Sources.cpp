#include "Sources.h"

#include "Instrument.h"   // ScopeFifo
#include "sky/SkyRandom.h"

#include <algorithm>
#include <cmath>

namespace worldtable::dsp
{

namespace
{
constexpr double kTickSeconds = Exciter::kTickSeconds;
constexpr double kSwellAttack = 0.08;   ///< the onset swell, as the exciter's strike (spec/05 §12)
constexpr double kSwellTau = 6.0;
} // namespace

void Sources::prepare (double rate, uint32_t soundSeed, const Settings& s) noexcept
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    seed = soundSeed;
    voices.prepare (sampleRate, seed);
    low.prepare (sampleRate, seed ^ 0x10Bu);
    flt.prepare (sampleRate);
    curtain = {};
    setSettings (s);

    mix = s.play ? 1.0 : 0.0;
    drone = 1.0 - mix * (1.0 - std::clamp (s.droneUnder, 0.0, 1.0));
    droneStep = 0.0;
    droneRampLeft = 0;
    activity = 0.0;
    primed = false;
    redWeight = 1.0;
    lowEnv = 0.0;
    lowSwellStart = -1.0;
    lowSwellAmp = 0.0;
    ticks = 0;
    flt.reset();
}

void Sources::setSettings (const Settings& s) noexcept
{
    settings = s;
    settings.droneUnder = std::clamp (s.droneUnder, 0.0, 1.0);
    settings.lowLevel = std::clamp (s.lowLevel, 0.0, 1.0);
    settings.filterSky = std::clamp (s.filterSky, 0.0, 1.0);

    auto v = s.voices;
    v.bow = kBowPerBed * s.bed;
    v.strike = s.strike;
    voices.setSettings (v);
    low.setSettings (s.low);
    flt.setSettings (s.filter);
}

//==================================================================================================
void Sources::noteOn (int note, float velocity) noexcept
{
    const int before = voices.lowestHeld();
    voices.noteOn (note, velocity);
    const int after = voices.lowestHeld();
    if (after == PlayVoices::kNoNote)
        return;

    // A new lowest note moves the low resonator now, so the strike lands on it.
    const int played = std::clamp (note, 0, 127) + settings.voices.transpose;
    if (after != before)
        low.setNote (((after % 12) + 12) % 12, std::max (0.02, settings.voices.attack));
    if (after == played && velocity > 0.0f)
    {
        const double shape = std::clamp (1.0 - (settings.voices.attack - 0.02) / 0.3, 0.0, 1.0);
        low.strike (settings.strike * shape * (0.5 + 0.5 * static_cast<double> (std::min (velocity, 1.0f))));
    }
}

void Sources::noteOff (int note) noexcept
{
    voices.noteOff (note);
}

//==================================================================================================
void Sources::readCurtain (const sky::SkyModel& model, const SkySound& sound) noexcept
{
    const auto& s = model.current();
    const int n = std::min ({ sound.voices(), SkyNodes::kMaxNodes, static_cast<int> (s.nodes.size()) });
    const double realS = static_cast<double> (sound.ticks()) * kTickSeconds;
    curtain.count = n;
    double red = 0.0;
    for (int v = 0; v < n; ++v)
    {
        const auto i = static_cast<size_t> (v);
        const auto& node = s.nodes[i];
        curtain.panL[i] = sound.voicePanL (v);
        curtain.panR[i] = sound.voicePanR (v);
        curtain.presence[i] = sound.voicePresence (v);
        curtain.cents[i] = settings.detuneCents * (static_cast<double> (node.displacementKm) / kDisplacementForFullDetuneKm)
                         + settings.driftCents * sky::rng::noise1 (seed, 91u, realS / kDriftKnotSeconds + v * 3.7);

        // The node's emission balance: a red-heavy node favours the octave below, a violet one the
        // twelfth. 1 when balanced or dark.
        const double c[kNumLayers] = { node.red, node.green, node.violet };
        const double sum = c[0] + c[1] + c[2];
        for (size_t l = 0; l < kNumLayers; ++l)
            curtain.colour[i][l] = sum > 1.0e-6 ? 0.5 + 0.5 * std::clamp (3.0 * c[l] / sum, 0.0, 2.0) : 1.0;
        red += curtain.colour[i][0];
    }
    redWeight = n > 0 ? red / n : 1.0;
}

void Sources::skyTicked (const sky::SkyModel& model, std::span<const sky::SkyEvent> events, const SkySound& sound, int tickSamples) noexcept
{
    const auto& s = model.current();
    const int samples = std::max (1, tickSamples);
    const double tickS = static_cast<double> (samples) / sampleRate;
    const double realS = static_cast<double> (sound.ticks()) * kTickSeconds;
    ++ticks;

    // the mode, gliding (rule 4): the bank's gain ramps over this tick to its new value
    const double modeTarget = settings.play ? 1.0 : 0.0;
    if (mix < modeTarget)
        mix = std::min (modeTarget, mix + tickS / kModeSeconds);
    else if (mix > modeTarget)
        mix = std::max (modeTarget, mix - tickS / kModeSeconds);
    const double droneTarget = 1.0 - mix * (1.0 - settings.droneUnder);
    droneStep = (droneTarget - drone) / samples;
    droneRampLeft = (droneTarget < drone || droneTarget > drone) ? samples : 0;

    // the sky's activity, slewed ≥ 2 s (rule 4): the filter opens with it, the bow follows it
    const double act = std::clamp (static_cast<double> (s.activity), 0.0, 1.5);
    activity = primed ? activity + (act - activity) * (1.0 - std::exp (-tickS / kSkySlewSeconds)) : act;
    primed = true;
    const auto type = flt.getSettings().type;
    flt.setSkyOctaves (static_cast<float> (settings.filterSky * kFilterSkyOctaves * std::min (activity, 1.0)
                                           * Filter::openDirection (type)));

    double onset = 0.0;
    for (const auto& e : events)
        if (e.type == sky::SkyEvent::Type::onset)
            onset = std::max (onset, static_cast<double> (e.strength));

    // the played notes: only when there is something to play
    if (settings.play || voices.activeCount() > 0)
    {
        readCurtain (model, sound);
        voices.tick (curtain, activity, onset, samples);
    }
    else
    {
        double red = 0.0;
        const int n = std::min (sound.voices(), static_cast<int> (s.nodes.size()));
        for (int v = 0; v < n; ++v)
        {
            const auto& node = s.nodes[static_cast<size_t> (v)];
            const double sum = static_cast<double> (node.red) + node.green + node.violet;
            red += sum > 1.0e-6 ? 0.5 + 0.5 * std::clamp (3.0 * node.red / sum, 0.0, 2.0) : 1.0;
        }
        redWeight = n > 0 ? red / n : 1.0;
    }

    // the low resonator: the chord's root (drone) or the lowest held note (Play), in the low band
    // as SkySound applies it (a floor move waits for the same 300 ms as the other edges)
    low.setFloor (sound.bands().edges.floor);
    const int held = voices.lowestHeld();
    if (settings.play)
    {
        if (held != PlayVoices::kNoNote)
            low.setNote (((held % 12) + 12) % 12, std::max (0.02, settings.voices.attack));
        else
            low.setNote (-1, kLowDroneCrossfade);   // keeps the note; applies a floor change
    }
    else
    {
        const auto& chord = sound.chord();
        low.setNote (((chord.key + chord.degree) % 12 + 12) % 12, kLowDroneCrossfade);
    }

    if (onset > 0.0)
    {
        lowSwellStart = realS;
        lowSwellAmp = 0.3 + 0.35 * std::min (onset, 1.5);
    }
    double swell = 0.0;
    if (lowSwellStart >= 0.0)
    {
        const double t = realS - lowSwellStart;
        swell = lowSwellAmp * std::min (1.0, t / kSwellAttack) * std::exp (-std::max (0.0, t - kSwellAttack) / kSwellTau);
    }

    const bool gate = held != PlayVoices::kNoNote;
    if (gate)
        lowEnv = std::min (1.0, lowEnv + tickS / std::max (0.001, settings.voices.attack));
    else
        lowEnv *= std::pow (10.0, -3.0 * tickS / std::max (0.02, settings.voices.release));

    const double droneExc = settings.bed * (0.5 + activity) + settings.strike * swell;
    const double playExc = gate ? kLowBowPerBed * settings.bed * (0.5 + activity) : 0.0;
    const double droneLow = settings.lowLevel * (0.7 + 0.3 * redWeight);
    const double playLow = settings.lowLevel * lowEnv;
    low.tick (droneExc * (1.0 - mix) + playExc * mix, droneLow * (1.0 - mix) + playLow * mix, samples);

    if (ticks % 100 == 0)
        guard();
}

void Sources::renderSamples (float* left, float* right, int numSamples) noexcept
{
    // the curtain, at the drone's level (exactly 1 outside Play mode: left untouched)
    if (droneRampLeft > 0 || drone < 1.0)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            if (droneRampLeft > 0)
            {
                drone += droneStep;
                if (--droneRampLeft == 0)
                    drone = 1.0 - mix * (1.0 - settings.droneUnder);
            }
            const auto g = static_cast<float> (drone);
            left[i] *= g;
            right[i] *= g;
        }
    }

    voices.process (left, right, numSamples);
    low.process (left, right, numSamples);
    if (inputTap != nullptr)   // T6.12: the mix before the tone stage, for the editor (read-only)
        inputTap->push (left, right, numSamples);
    flt.process (left, right, numSamples);
}

} // namespace worldtable::dsp
