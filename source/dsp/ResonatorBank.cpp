#include "ResonatorBank.h"

#include "sky/SkyRandom.h"

#include <algorithm>
#include <cmath>
#include <numbers>

// prototype/sky/sound/bank.js, line by line.

namespace worldtable::dsp
{

namespace
{
const double kT60Ln = std::log (1000.0);   // −60 dB
} // namespace

void ResonatorBank::prepare (int voices, double rate, uint32_t seed) noexcept
{
    numVoices = std::clamp (voices, 0, kMaxVoices);
    sampleRate = rate;
    re.fill (0.0); im.fill (0.0);
    c.fill (0.0); s.fill (0.0);
    inA.fill (0.0);
    gL.fill (0.0); gR.fill (0.0);
    dgL.fill (0.0); dgR.fill (0.0);
    xn.fill (0.0);
    for (int v = 0; v < numVoices; ++v)
        noise[static_cast<size_t> (v)] = sky::rng::hash32 ((seed * 0x9e3779b1u) ^ sky::rng::hash32 (static_cast<uint32_t> (v + 1))) | 1u;
    activeBank.fill (false);
}

double ResonatorBank::radius (double t60Seconds) const noexcept
{
    return std::exp (-kT60Ln / (t60Seconds * sampleRate));
}

void ResonatorBank::tune (int k, double hz, double r) noexcept
{
    const double w = (2.0 * std::numbers::pi * hz) / sampleRate;
    c[static_cast<size_t> (k)] = r * std::cos (w);
    s[static_cast<size_t> (k)] = r * std::sin (w);
}

void ResonatorBank::ramp (int k, double left, double right, int samples) noexcept
{
    const auto i = static_cast<size_t> (k);
    dgL[i] = (left - gL[i]) / samples;
    dgR[i] = (right - gR[i]) / samples;
}

void ResonatorBank::clear (int bank) noexcept
{
    const int n = kLayers * numVoices, k0 = bank * n, o0 = (1 - bank) * n;
    for (int q = 0; q < n; ++q)
    {
        const auto k = static_cast<size_t> (k0 + q), o = static_cast<size_t> (o0 + q);
        re[k] = 0.0;
        im[k] = 0.0;
        inA[k] = 0.0;
        gL[k] = gL[o];
        gR[k] = gR[o];
        dgL[k] = 0.0;
        dgR[k] = 0.0;
    }
}

int ResonatorBank::guard() noexcept
{
    int resets = 0;
    const auto n = static_cast<size_t> (kBanks * kLayers * numVoices);
    for (size_t k = 0; k < n; ++k)
    {
        if (! std::isfinite (re[k]) || ! std::isfinite (im[k]))
        {
            re[k] = 0.0;
            im[k] = 0.0;
            ++resets;
        }
    }
    return resets;
}

void ResonatorBank::process (float* left, float* right, int numSamples) noexcept
{
    constexpr double kScale = 1.0 / 2147483648.0;
    const int V = numVoices;
    const auto voices = static_cast<size_t> (V);

    for (int j = 0; j < numSamples; ++j)
    {
        for (size_t v = 0; v < voices; ++v)   // xorshift32 white noise per voice
        {
            uint32_t x = noise[v];
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            noise[v] = x;
            xn[v] = static_cast<double> (static_cast<int32_t> (x)) * kScale;
        }

        double outL = 0.0, outR = 0.0;
        for (int b = 0; b < kBanks; ++b)
        {
            if (! activeBank[static_cast<size_t> (b)])
                continue;
            for (int l = 0; l < kLayers; ++l)
            {
                const auto k0 = static_cast<size_t> ((b * kLayers + l) * V);
                for (size_t v = 0; v < voices; ++v)
                {
                    const size_t k = k0 + v;
                    const double zr = re[k], zi = im[k];
                    re[k] = c[k] * zr - s[k] * zi + inA[k] * xn[v];
                    const double y = s[k] * zr + c[k] * zi;
                    im[k] = y;
                    outL += y * gL[k];
                    outR += y * gR[k];
                    gL[k] += dgL[k];
                    gR[k] += dgR[k];
                }
            }
        }
        left[j] = static_cast<float> (outL);
        right[j] = static_cast<float> (outR);
    }
}

} // namespace worldtable::dsp
