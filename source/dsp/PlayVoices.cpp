#include "PlayVoices.h"

#include "sky/SkyRandom.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace worldtable::dsp
{

namespace
{
constexpr double kPi = std::numbers::pi;
constexpr double kSilent = 1.0e-4;   ///< −80 dB: a released voice is free

double midiHz (double note) noexcept { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

double white (uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<double> (state >> 8) * (2.0 / 16777216.0) - 1.0;
}
} // namespace

void PlayVoices::prepare (double rate, uint32_t soundSeed) noexcept
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    seed = soundSeed;
    starts = 0;
    pedal = false;
    bowNow = 0.0;
    curtain = nullptr;
    for (size_t i = 0; i < voices.size(); ++i)
    {
        auto& v = voices[i];
        v = Voice {};
        for (size_t s = 0; s < v.noise.size(); ++s)
        {
            v.noise[s] = sky::rng::hash32 (seed ^ sky::rng::hash32 (0x9A1Du + static_cast<uint32_t> (i * kStrands + s)));
            if (v.noise[s] == 0)
                v.noise[s] = 0x9E3779B9u;
        }
    }
    setSettings (settings);
}

void PlayVoices::setSettings (const Settings& next) noexcept
{
    settings = next;
    settings.attack = std::clamp (next.attack, 0.001, 30.0);
    settings.release = std::clamp (next.release, 0.01, 60.0);
    settings.velocity = std::clamp (next.velocity, 0.0, 1.0);
    attackStep = 1.0 / (settings.attack * sampleRate);
    releaseCoef = std::pow (10.0, -3.0 / (settings.release * sampleRate));
    stealStep = 1.0 / (kStealSeconds * sampleRate);
    for (size_t l = 0; l < kNumLayers; ++l)
    {
        radius[l] = std::pow (10.0, -3.0 / (std::max (0.05, settings.t60[l]) * sampleRate));
        norm[l] = std::sqrt (1.0 - radius[l] * radius[l]);
    }
}

double PlayVoices::strikeShape() const noexcept
{
    // A short attack is struck, a slow one bowed in: full strike up to 20 ms, none from 320 ms.
    return std::clamp (1.0 - (settings.attack - 0.02) / 0.3, 0.0, 1.0);
}

double PlayVoices::velocityGain (float velocity) const noexcept
{
    const double v = std::clamp (static_cast<double> (velocity), 0.0, 1.0);
    return std::pow (10.0, -settings.velocity * 1.5 * (1.0 - v));   // up to 30 dB of dynamics
}

int PlayVoices::nodeOf (const Voice& v, int strand) const noexcept
{
    const int n = curtain != nullptr ? curtain->count : 0;
    if (n <= 0)
        return 0;
    return (v.home + strand * std::max (1, n / kStrands)) % n;
}

//==================================================================================================
void PlayVoices::start (Voice& v, int note, float velocity) noexcept
{
    const int n = curtain != nullptr && curtain->count > 0 ? curtain->count : 64;
    v.stage = Stage::attack;
    v.note = note;
    v.velocity = velocity;
    v.gate = true;
    v.sustained = false;
    v.env = 0.0;
    v.velGain = velocityGain (velocity);
    v.home = static_cast<int> ((starts * 23u + 7u) % static_cast<uint64_t> (n));
    v.age = ++starts;
    v.pendingNote = -1;
    v.re.fill (0.0);   // silent (the envelope is at 0), so clearing the old note can't click
    v.im.fill (0.0);
    retune (v, true, 0);

    const double strike = kStrikeScale * settings.strike * strikeShape() * (0.5 + 0.5 * velocity);
    for (auto& r : v.re)
        r += strike;
}

void PlayVoices::release (Voice& v) noexcept
{
    v.gate = false;
    v.sustained = false;
    if (v.stage == Stage::attack || v.stage == Stage::sustain)
        v.stage = Stage::release;
    v.inA.fill (0.0);   // the bow lifts; the resonators ring on under the envelope
}

void PlayVoices::steal (Voice& v, int note, float velocity) noexcept
{
    v.stage = Stage::steal;
    v.gate = false;
    v.sustained = false;
    v.pendingNote = note;
    v.pendingVelocity = velocity;
    v.inA.fill (0.0);
}

void PlayVoices::noteOn (int note, float velocity) noexcept
{
    if (velocity <= 0.0f)
    {
        noteOff (note);
        return;
    }
    note = std::clamp (note, 0, 127);
    velocity = std::min (velocity, 1.0f);

    // The same note again: retrigger its voice (the envelope climbs from where it is).
    for (size_t i = 0; i < voices.size(); ++i)
    {
        auto& v = voices[i];
        if (v.stage == Stage::idle || v.stage == Stage::steal || v.note != note)
            continue;
        v.stage = Stage::attack;
        v.gate = true;
        v.sustained = false;
        v.velocity = velocity;
        v.velGain = velocityGain (velocity);
        v.age = ++starts;
        retune (v, false, static_cast<int> (kStealSeconds * sampleRate));
        const double strike = kStrikeScale * settings.strike * strikeShape() * (0.5 + 0.5 * velocity);
        for (auto& r : v.re)
            r += strike;
        return;
    }

    // Too many held: the oldest held note gives way.
    if (heldCount() >= kMaxHeld)
    {
        Voice* oldest = nullptr;
        for (auto& v : voices)
            if (v.stage != Stage::idle && v.stage != Stage::steal && held (v) && (oldest == nullptr || v.age < oldest->age))
                oldest = &v;
        if (oldest != nullptr)
        {
            steal (*oldest, note, velocity);
            return;
        }
    }

    for (size_t i = 0; i < voices.size(); ++i)
    {
        if (voices[i].stage == Stage::idle)
        {
            start (voices[i], note, velocity);
            return;
        }
    }

    // Every slot busy: the quietest released voice gives way (else the oldest of all).
    Voice* victim = nullptr;
    for (auto& v : voices)
        if (v.stage == Stage::release && (victim == nullptr || v.env < victim->env))
            victim = &v;
    if (victim == nullptr)
        for (auto& v : voices)
            if (v.stage != Stage::steal && (victim == nullptr || v.age < victim->age))
                victim = &v;
    if (victim != nullptr)
        steal (*victim, note, velocity);
}

void PlayVoices::noteOff (int note) noexcept
{
    for (auto& v : voices)
    {
        if (v.note != note || ! v.gate || (v.stage != Stage::attack && v.stage != Stage::sustain))
            continue;
        if (pedal)
        {
            v.gate = false;
            v.sustained = true;
        }
        else
        {
            release (v);
        }
    }
    // A note-off for a note still waiting on a steal cancels it.
    for (auto& v : voices)
        if (v.stage == Stage::steal && v.pendingNote == note)
            v.pendingNote = -1;
}

void PlayVoices::sustain (bool down) noexcept
{
    pedal = down;
    if (down)
        return;
    for (auto& v : voices)
        if (v.sustained)
            release (v);
}

void PlayVoices::allNotesOff() noexcept
{
    pedal = false;
    for (auto& v : voices)
    {
        if (v.stage == Stage::attack || v.stage == Stage::sustain)
            release (v);
        else if (v.stage == Stage::steal)
            v.pendingNote = -1;
    }
}

void PlayVoices::allSoundOff() noexcept
{
    pedal = false;
    for (auto& v : voices)
        if (v.stage != Stage::idle)
            steal (v, -1, 0.0f);
}

//==================================================================================================
void PlayVoices::retune (Voice& v, bool snapGains, int rampSamples) noexcept
{
    const double nyquistGuard = 0.45 * sampleRate;
    const double bow = held (v) ? bowNow : 0.0;
    const double strands = std::sqrt (static_cast<double> (kStrands));
    const double brightness = 0.4 + 0.6 * static_cast<double> (v.velocity);   // velocity opens the violet

    for (int s = 0; s < kStrands; ++s)
    {
        const int node = nodeOf (v, s);
        const auto ni = static_cast<size_t> (node);
        const bool haveSky = curtain != nullptr && curtain->count > 0;
        const double pl = haveSky ? curtain->panL[ni] : std::sqrt (0.5);
        const double pr = haveSky ? curtain->panR[ni] : std::sqrt (0.5);
        const double presence = haveSky ? curtain->presence[ni] : 1.0;
        const double cents = haveSky ? curtain->cents[ni] : 0.0;

        for (int p = 0; p < kPartials; ++p)
        {
            const auto i = static_cast<size_t> (s * kPartials + p);
            const auto layer = static_cast<size_t> (kLayerOf[static_cast<size_t> (p)]);
            const double colour = haveSky ? curtain->colour[ni][layer] : 1.0;
            const double hz = midiHz (v.note + settings.transpose + kInterval[static_cast<size_t> (p)] + cents / 100.0);
            const bool audible = hz > 8.0 && hz < nyquistGuard;
            const double omega = 2.0 * kPi * std::min (hz, nyquistGuard) / sampleRate;
            v.c[i] = radius[layer] * std::cos (omega);
            v.s[i] = radius[layer] * std::sin (omega);
            v.inA[i] = audible ? bow * norm[layer] : 0.0;

            const double g = audible
                ? kOutputScale * v.velGain * kPartialGain[static_cast<size_t> (p)] * settings.level[layer] * colour * presence
                      * (p == kPartials - 1 ? brightness : 1.0) / strands
                : 0.0;
            if (snapGains || rampSamples <= 0)
            {
                v.gL[i] = g * pl;
                v.gR[i] = g * pr;
                v.dgL[i] = v.dgR[i] = 0.0;
            }
            else
            {
                v.dgL[i] = (g * pl - v.gL[i]) / rampSamples;
                v.dgR[i] = (g * pr - v.gR[i]) / rampSamples;
            }
        }
    }
    v.rampLeft = snapGains ? 0 : std::max (0, rampSamples);
}

void PlayVoices::tick (const SkyNodes& nodes, double activity, double onsetStrength, int tickSamples) noexcept
{
    curtain = &nodes;
    bowNow = settings.bow * (0.5 + std::clamp (activity, 0.0, 1.5));
    const double onsetStrike = onsetStrength > 0.0 ? settings.strike * 0.35 * std::min (onsetStrength, 1.5) : 0.0;

    for (size_t i = 0; i < voices.size(); ++i)
    {
        auto& v = voices[i];
        if (v.stage == Stage::idle)
            continue;
        if (v.stage != Stage::steal)
            retune (v, false, tickSamples);
        if (onsetStrike > 0.0 && held (v) && v.stage != Stage::steal)
            for (auto& r : v.re)
                r += onsetStrike;
    }
}

void PlayVoices::process (float* left, float* right, int numSamples) noexcept
{
    for (auto& v : voices)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            if (v.stage == Stage::idle)
                break;

            switch (v.stage)
            {
                case Stage::attack:
                    v.env += attackStep;
                    if (v.env >= 1.0)
                    {
                        v.env = 1.0;
                        v.stage = Stage::sustain;
                    }
                    break;
                case Stage::sustain:
                    break;
                case Stage::release:
                    v.env *= releaseCoef;
                    if (v.env < kSilent)
                    {
                        v.env = 0.0;
                        v.stage = Stage::idle;
                        v.note = -1;
                    }
                    break;
                case Stage::steal:
                    v.env -= stealStep;
                    if (v.env <= 0.0)
                    {
                        v.env = 0.0;
                        if (v.pendingNote >= 0)
                            start (v, v.pendingNote, v.pendingVelocity);
                        else
                        {
                            v.stage = Stage::idle;
                            v.note = -1;
                        }
                    }
                    break;
                case Stage::idle:
                    break;
            }
            if (v.stage == Stage::idle)
                break;

            double l = 0.0, r = 0.0;
            for (int s = 0; s < kStrands; ++s)
            {
                const double x = white (v.noise[static_cast<size_t> (s)]);
                for (int p = 0; p < kPartials; ++p)
                {
                    const auto i = static_cast<size_t> (s * kPartials + p);
                    const double zr = v.c[i] * v.re[i] - v.s[i] * v.im[i] + v.inA[i] * x;
                    const double zi = v.s[i] * v.re[i] + v.c[i] * v.im[i];
                    v.re[i] = zr;
                    v.im[i] = zi;
                    l += zi * v.gL[i];
                    r += zi * v.gR[i];
                }
            }
            if (v.rampLeft > 0)
            {
                for (size_t i = 0; i < static_cast<size_t> (kResonators); ++i)
                {
                    v.gL[i] += v.dgL[i];
                    v.gR[i] += v.dgR[i];
                }
                --v.rampLeft;
            }
            left[n] += static_cast<float> (l * v.env);
            right[n] += static_cast<float> (r * v.env);
        }
    }
}

//==================================================================================================
int PlayVoices::heldCount() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        if (v.stage != Stage::idle && v.stage != Stage::steal && held (v))
            ++n;
    return n;
}

int PlayVoices::activeCount() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        if (v.stage != Stage::idle)
            ++n;
    return n;
}

int PlayVoices::lowestHeld() const noexcept
{
    int lowest = -1;
    for (const auto& v : voices)
        if (v.stage != Stage::idle && v.stage != Stage::steal && held (v))
            if (lowest < 0 || v.note < lowest)
                lowest = v.note;
    return lowest < 0 ? kNoNote : lowest + settings.transpose;
}

int PlayVoices::view (std::array<View, kSlots>& out) const noexcept
{
    int n = 0;
    for (const auto& v : voices)
    {
        if (v.stage == Stage::idle || v.note < 0)
            continue;
        auto& o = out[static_cast<size_t> (n++)];
        o.note = v.note + settings.transpose;
        o.level = static_cast<float> (v.env * v.velGain);
        o.held = held (v) && v.stage != Stage::steal;
    }
    return n;
}

int PlayVoices::guard() noexcept
{
    int resets = 0;
    for (auto& v : voices)
    {
        if (v.stage == Stage::idle)
            continue;
        for (size_t i = 0; i < v.re.size(); ++i)
        {
            if (! std::isfinite (v.re[i]) || ! std::isfinite (v.im[i]))
            {
                v.re[i] = v.im[i] = 0.0;
                ++resets;
            }
        }
    }
    return resets;
}

} // namespace worldtable::dsp
