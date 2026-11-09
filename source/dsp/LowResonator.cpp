#include "LowResonator.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace worldtable::dsp
{

namespace
{
constexpr double kPi = std::numbers::pi;

double midiHz (double note) noexcept { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

double white (uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<double> (state >> 8) * (2.0 / 16777216.0) - 1.0;
}
} // namespace

int LowResonator::noteFor (int pitchClass, int octave) noexcept
{
    return 12 * (std::clamp (octave, 1, 2) + 1) + ((pitchClass % 12) + 12) % 12;
}

void LowResonator::prepare (double rate, uint32_t seed) noexcept
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    cur = 0;
    currentNote = currentPc = -1;
    currentOctave = settings.octave;
    xf = 1.0;
    re.fill (0.0);
    im.fill (0.0);
    c.fill (0.0);
    s.fill (0.0);
    inA.fill (0.0);
    hz.fill (0.0);
    bankT60.fill (settings.t60);
    for (size_t k = 0; k < noise.size(); ++k)
        noise[k] = (seed * 2654435761u) ^ (0x51ED27u + static_cast<uint32_t> (k) * 0x9E3779B9u) ^ 0xB0A5u;
    for (auto& n : noise)
        if (n == 0)
            n = 0x9E3779B9u;
    outGain = 0.0;
    outStep = 0.0;
    rampLeft = 0;
}

void LowResonator::setSettings (const Settings& next) noexcept
{
    settings = next;
    settings.t60 = std::clamp (next.t60, 0.1, 60.0);
    settings.octave = std::clamp (next.octave, 1, 2);
}

double LowResonator::radius (double t60Seconds) const noexcept
{
    return std::pow (10.0, -3.0 / (std::max (0.01, t60Seconds) * sampleRate));
}

void LowResonator::tune (int bank, int note) noexcept
{
    const auto b = static_cast<size_t> (bank * kPerBank);
    hz[b + 0] = midiHz (note - kDetuneCents / 100.0);
    hz[b + 1] = midiHz (note + kDetuneCents / 100.0);
    hz[b + 2] = midiHz (note + 12.0);
}

void LowResonator::setNote (int pitchClass, double crossfadeSeconds) noexcept
{
    if (pitchClass < 0)
    {
        if (currentPc < 0 || settings.octave == currentOctave)
            return;
        pitchClass = currentPc;   // the octave moved: same note, new place
    }
    if (pitchClass == currentPc && settings.octave == currentOctave)
        return;

    const int note = noteFor (pitchClass, settings.octave);
    if (currentNote < 0)
    {
        tune (cur, note);   // the first note: no crossfade from silence
        xf = 1.0;
    }
    else
    {
        // The other bank may still ring the note before last: retune it in place (the phasor
        // turns on at the new rate, nothing jumps) and damp what is left of the old note.
        cur = 1 - cur;
        tune (cur, note);
        xf = 0.0;
        xfSeconds = std::max (1.0e-3, crossfadeSeconds);
        bankT60[static_cast<size_t> (1 - cur)] = std::min (settings.t60, std::max (0.3, 2.0 * xfSeconds));
    }
    currentNote = note;
    currentPc = pitchClass;
    currentOctave = settings.octave;
}

void LowResonator::tick (double excitation, double gain, int tickSamples) noexcept
{
    const int samples = std::max (1, tickSamples);
    const double tickS = samples / sampleRate;

    bankT60[static_cast<size_t> (cur)] = settings.t60;
    if (xf < 1.0)
        xf = std::min (1.0, xf + tickS / std::max (xfSeconds, tickS));

    const double wCur = std::sin (0.5 * kPi * xf);
    const double wOld = std::cos (0.5 * kPi * xf);
    const double input = currentNote < 0 ? 0.0 : std::max (0.0, excitation);
    for (int b = 0; b < kBanks; ++b)
    {
        const double r = radius (bankT60[static_cast<size_t> (b)]);
        const double norm = std::sqrt (1.0 - r * r);
        const double w = b == cur ? wCur : wOld;
        for (int k = 0; k < kPerBank; ++k)
        {
            const auto i = static_cast<size_t> (b * kPerBank + k);
            const double omega = 2.0 * kPi * hz[i] / sampleRate;
            c[i] = r * std::cos (omega);
            s[i] = r * std::sin (omega);
            inA[i] = input * w * norm * (k == 2 ? kBodyGain : 1.0);
        }
    }

    outStep = (std::max (0.0, gain) - outGain) / samples;
    rampLeft = samples;
}

void LowResonator::strike (double amount) noexcept
{
    if (currentNote < 0 || ! (amount > 0.0))
        return;
    for (int k = 0; k < kPerBank; ++k)
        re[static_cast<size_t> (cur * kPerBank + k)] += amount * (k == 2 ? kBodyGain : 1.0);
}

void LowResonator::process (float* left, float* right, int numSamples) noexcept
{
    for (int n = 0; n < numSamples; ++n)
    {
        std::array<double, kPerBank> x {};
        for (size_t k = 0; k < noise.size(); ++k)
            x[k] = white (noise[k]);

        double sum = 0.0;
        for (size_t i = 0; i < re.size(); ++i)
        {
            const double zr = c[i] * re[i] - s[i] * im[i] + inA[i] * x[i % kPerBank];
            const double zi = s[i] * re[i] + c[i] * im[i];
            re[i] = zr;
            im[i] = zi;
            sum += zi;
        }
        if (rampLeft > 0)
        {
            outGain += outStep;
            --rampLeft;
        }
        const auto out = static_cast<float> (sum * outGain * kOutputScale);
        left[n] += out;
        right[n] += out;
    }
}

int LowResonator::guard() noexcept
{
    int resets = 0;
    for (size_t i = 0; i < re.size(); ++i)
    {
        if (! std::isfinite (re[i]) || ! std::isfinite (im[i]))
        {
            re[i] = im[i] = 0.0;
            ++resets;
        }
    }
    if (! std::isfinite (outGain))
    {
        outGain = 0.0;
        ++resets;
    }
    return resets;
}

} // namespace worldtable::dsp
