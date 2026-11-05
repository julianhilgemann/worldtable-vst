#include "Exciter.h"

#include <algorithm>
#include <cmath>

// prototype/sky/sound/engine.js, the excitation part of tick(), line by line.

namespace worldtable::dsp
{

namespace
{
constexpr double kStrikeAttackSeconds = 0.08;
constexpr double kStrikeTailSeconds = 6.0;
constexpr double kRiseSmoothSeconds = 0.3;
constexpr double kStrikeWidthNodes = 12.0;
} // namespace

void Exciter::prepare (int voices, int voicesPerNode, const Settings& s) noexcept
{
    settings = s;
    numVoices = std::clamp (voices, 0, ResonatorBank::kMaxVoices);
    strikeWidth = kStrikeWidthNodes * voicesPerNode;
    strikeStart = 0.0;
    strikeAmp = 0.0;
    strikeVoice = 0.5 * numVoices;
    strikeEnv = 0.0;
    bed = 0.0;
    rise.fill (0.0);
    prevBrightness.fill (0.0);
}

void Exciter::onset (double realSeconds, double strength, int voice) noexcept
{
    strikeStart = realSeconds;
    strikeAmp = std::clamp (0.3 + 0.35 * strength, 0.3, 1.5);
    strikeVoice = voice;
}

void Exciter::tick (double realSeconds, double activity, const double* brightness) noexcept
{
    const double ts = realSeconds - strikeStart;
    strikeEnv = ts >= 0.0 ? strikeAmp * (1.0 - std::exp (-ts / kStrikeAttackSeconds)) * std::exp (-ts / kStrikeTailSeconds) : 0.0;
    bed = settings.bed * (0.5 + activity);

    const double riseK = 1.0 - std::exp (-kTickSeconds / kRiseSmoothSeconds);
    for (size_t v = 0; v < static_cast<size_t> (numVoices); ++v)
    {
        rise[v] += (std::max (0.0, (brightness[v] - prevBrightness[v]) / kTickSeconds) - rise[v]) * riseK;
        prevBrightness[v] = brightness[v];
    }
}

double Exciter::continuous (int voice) const noexcept
{
    return bed + settings.rise * rise[static_cast<size_t> (voice)];
}

double Exciter::strike (int voice) const noexcept
{
    const double d = (voice - strikeVoice) / strikeWidth;
    const double spatial = 0.5 + 0.5 * std::exp (-(d * d));
    return settings.strike * strikeEnv * spatial;
}

} // namespace worldtable::dsp
