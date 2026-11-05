#include "ShimmerReverb.h"

namespace worldtable::dsp
{

namespace
{
// Mutually prime-ish line lengths in ms at size = 1.
constexpr std::array<float, 8> kLineMs { 59.3f, 67.9f, 79.1f, 89.3f, 101.7f, 113.9f, 127.3f, 142.1f };
constexpr std::array<float, 4> kDiffMsL { 4.71f, 6.13f, 9.27f, 12.83f };
constexpr std::array<float, 4> kDiffMsR { 5.03f, 6.89f, 8.71f, 13.57f };

constexpr float kMinScale = 0.25f;   // size 0 → quarter-length lines
constexpr float kMaxModMs = 1.6f;

/** In-place 8-point fast Walsh–Hadamard transform, normalised (orthogonal). */
inline void hadamard8 (std::array<float, 8>& x) noexcept
{
    for (int h = 1; h < 8; h <<= 1)
        for (int i = 0; i < 8; i += h << 1)
            for (int j = i; j < i + h; ++j)
            {
                const float a = x[static_cast<size_t> (j)];
                const float b = x[static_cast<size_t> (j + h)];
                x[static_cast<size_t> (j)] = a + b;
                x[static_cast<size_t> (j + h)] = a - b;
            }

    constexpr float norm = 0.35355339f; // 1/sqrt(8)
    for (auto& v : x)
        v *= norm;
}
} // namespace

void ShimmerReverb::prepare (double rate)
{
    sampleRate = rate;
    const auto ms = [rate] (float m) { return static_cast<size_t> (std::ceil (m * 0.001 * rate)); };

    predelayL.allocate (ms (260.0f) + 4);
    predelayR.allocate (ms (260.0f) + 4);

    for (size_t i = 0; i < kDiffusers; ++i)
    {
        diffL[i].line.allocate (ms (kDiffMsL[i]) + 2);
        diffR[i].line.allocate (ms (kDiffMsR[i]) + 2);
        diffL[i].delay = static_cast<int> (ms (kDiffMsL[i]));
        diffR[i].delay = static_cast<int> (ms (kDiffMsR[i]));
    }

    for (size_t i = 0; i < kLines; ++i)
    {
        lines[i].allocate (ms (kLineMs[i] + kMaxModMs * 2.0f) + 8);
        modInc[i] = (0.07 + 0.053 * static_cast<double> (i)) / rate;
        modPhase[i] = 0.137 * static_cast<double> (i);
    }

    shifter.line.allocate (ms (90.0f) + 8);
    shifter.windowSamples = static_cast<float> (0.075 * rate);
    shifterFifth.line.allocate (ms (90.0f) + 8);
    shifterFifth.windowSamples = static_cast<float> (0.075 * rate);
    shifterFifth.ratio = 3.0f;   // an octave and a fifth

    shimmerHpCoef = 1.0f - std::exp (-kTwoPi * 350.0f / static_cast<float> (rate));
    shimmerLpCoef = 1.0f - std::exp (-kTwoPi * 7000.0f / static_cast<float> (rate));

    sizeCoef = onePoleCoef (0.5f, rate);   // per sample: 0.5 s glide
    targetSize = currentSize = settings.size;
    predelayCoef = onePoleCoef (0.15f, rate);
    predelayTarget = predelayCurrent = settings.predelayMs * 0.001f * static_cast<float> (rate);
    coefCount = 0;

    reset();
    setSettings (settings);
    updateLineCoefficients();
}

void ShimmerReverb::reset() noexcept
{
    predelayL.clear();
    predelayR.clear();
    for (auto& d : diffL) d.line.clear();
    for (auto& d : diffR) d.line.clear();
    for (auto& l : lines) l.clear();
    lineLp.fill (0.0f);
    shifter.line.clear();
    shifter.phase = 0.0f;
    shifterFifth.line.clear();
    shifterFifth.phase = 0.0f;
    shimmerHp = shimmerLp = shimmerFeedback = 0.0f;
    wetLevel = 0.0f;
}

void ShimmerReverb::setSettings (const Settings& s) noexcept
{
    settings = s;
    targetSize = std::clamp (s.size, 0.0f, 1.0f);

    // damping 0 → 18 kHz, 1 → 1.2 kHz one-pole low-pass inside each line
    const float dampHz = 1200.0f * std::pow (15.0f, 1.0f - std::clamp (s.damping, 0.0f, 1.0f));
    lpCoef = 1.0f - std::exp (-kTwoPi * dampHz / static_cast<float> (sampleRate));

    modDepth = std::clamp (s.modulation, 0.0f, 1.0f) * kMaxModMs * 0.001f * static_cast<float> (sampleRate);
    predelayTarget = std::clamp (s.predelayMs, 0.0f, 250.0f) * 0.001f * static_cast<float> (sampleRate);
    shimmerGain = 0.55f * std::clamp (s.shimmer, 0.0f, 1.0f);
}

/** Line lengths and decay gains for the current (glided) size. Called on a fixed 64-sample grid
    inside process(), so it is independent of the host's block size. */
void ShimmerReverb::updateLineCoefficients() noexcept
{
    const float scale = kMinScale + (1.0f - kMinScale) * currentSize;
    const float rt60 = std::max (0.1f, settings.decaySeconds);
    for (size_t i = 0; i < kLines; ++i)
    {
        lineDelay[i] = kLineMs[i] * scale * 0.001f * static_cast<float> (sampleRate);
        lineGain[i] = std::pow (10.0f, -3.0f * lineDelay[i] / (rt60 * static_cast<float> (sampleRate)));
    }
}

void ShimmerReverb::process (float* left, float* right, int numSamples) noexcept
{
    if (lines[0].buf.empty())
        return;

    const float mix = std::clamp (settings.mix, 0.0f, 1.0f);
    const float dryGain = std::min (1.0f, 2.0f * (1.0f - mix));
    const float wetGain = std::min (1.0f, 2.0f * mix);
    float energy = 0.0f;

    std::array<float, kLines> state {};

    for (int n = 0; n < numSamples; ++n)
    {
        // The size glides per sample; the coefficients follow on an absolute 64-sample grid.
        currentSize += (targetSize - currentSize) * sizeCoef;
        predelayCurrent += (predelayTarget - predelayCurrent) * predelayCoef;
        if (++coefCount >= kChunk)
        {
            coefCount = 0;
            updateLineCoefficients();
        }

        const float inL = left[n], inR = right[n];

        predelayL.write (inL);
        predelayR.write (inR);
        float xL = predelayCurrent > 0.5f ? predelayL.read (predelayCurrent) : inL;
        float xR = predelayCurrent > 0.5f ? predelayR.read (predelayCurrent) : inR;

        for (size_t i = 0; i < kDiffusers; ++i)
        {
            xL = diffL[i].process (xL);
            xR = diffR[i].process (xR);
        }

        // Shimmer re-enters with the dry input.
        xL += shimmerFeedback;
        xR += shimmerFeedback;

        // Read the network (modulated)
        for (size_t i = 0; i < kLines; ++i)
        {
            modPhase[i] += modInc[i];
            if (modPhase[i] >= 1.0)
                modPhase[i] -= 1.0;
            const float mod = modDepth * (1.0f + std::sin (kTwoPi * static_cast<float> (modPhase[i])));
            float v = lines[i].read (lineDelay[i] + mod);
            lineLp[i] += lpCoef * (v - lineLp[i]);
            state[i] = lineLp[i] * lineGain[i];
        }

        const float outL = state[0] - state[2] + state[4] - state[6] + 0.5f * (state[1] - state[7]);
        const float outR = state[1] - state[3] + state[5] - state[7] + 0.5f * (state[2] - state[4]);

        hadamard8 (state);

        // Inject input: left into even lines, right into odd ones.
        for (size_t i = 0; i < kLines; ++i)
            lines[i].write (state[i] + ((i & 1u) == 0 ? xL : xR) * 0.5f);

        // Shimmer path: octave up, band-limited, soft-limited.
        if (shimmerGain > 0.0f)
        {
            const float mono = 0.5f * (outL + outR);
            const float shifted = 0.75f * (shifter.process (mono) + 0.5f * shifterFifth.process (mono));
            shimmerLp += shimmerLpCoef * (shifted - shimmerLp);
            shimmerHp += shimmerHpCoef * (shimmerLp - shimmerHp);
            shimmerFeedback = std::tanh ((shimmerLp - shimmerHp) * shimmerGain * 1.5f) * 0.66f;
        }
        else
        {
            shimmerFeedback *= 0.99f;
        }

        const float wL = outL * 0.6f;
        const float wR = outR * 0.6f;
        energy += wL * wL + wR * wR;

        left[n]  = inL * dryGain + wL * wetGain;
        right[n] = inR * dryGain + wR * wetGain;
    }

    const float rms = std::sqrt (energy / static_cast<float> (std::max (1, numSamples * 2)));
    wetLevel += 0.2f * (rms - wetLevel);
}

} // namespace worldtable::dsp
