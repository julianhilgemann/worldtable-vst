#include "Voice.h"
#include "params/Params.h"

namespace worldtable::dsp
{

void Voice::prepare (double rate, uint32_t seed) noexcept
{
    sampleRate = rate;
    rng.seed (seed);
    ampEnv.prepare (rate);
    filterEnv.prepare (rate);
    driveL.prepare (rate);
    driveR.prepare (rate);
    reset();
}

void Voice::reset() noexcept
{
    ampEnv.reset();
    filterEnv.reset();
    driveL.reset();
    driveR.reset();
    svfL1.reset(); svfR1.reset(); svfL2.reset(); svfR2.reset();
    owner = Owner::none;
    gateRemaining = -1;
}

void Voice::start (int newNote, float vel, Owner newOwner, int64_t gateSamples, uint64_t newAge) noexcept
{
    const bool wasIdle = ! ampEnv.isActive();

    note = newNote;
    velocity = vel;
    owner = newOwner;
    gateRemaining = gateSamples;
    age = newAge;

    if (wasIdle)
    {
        // Fresh start: random unison phases avoid a phasey, identical attack.
        for (auto& p : phase)
            p = static_cast<double> (rng.nextFloat());
        svfL1.reset(); svfR1.reset(); svfL2.reset(); svfR2.reset();
        driveL.reset(); driveR.reset();
    }
    // Stolen voice: phases continue, envelopes restart from their current level,
    // so the steal never jumps in amplitude.
    ampEnv.noteOn();
    filterEnv.noteOn();
}

void Voice::release() noexcept
{
    ampEnv.noteOff();
    filterEnv.noteOff();
    gateRemaining = -1;
}

void Voice::readTable (const WavetableSet& t, float position, float mipLevel,
                        const std::array<double, kMaxUnison>& phases, int unison,
                        float* outL, float* outR) const noexcept
{
    const int frames = std::max (1, t.numFrames);
    const float fpos = std::clamp (position, 0.0f, 1.0f) * static_cast<float> (frames - 1);
    const int f0 = static_cast<int> (fpos);
    const int f1 = std::min (f0 + 1, frames - 1);
    const float ff = fpos - static_cast<float> (f0);

    const int m0 = std::clamp (static_cast<int> (mipLevel), 0, WavetableSet::kNumMips - 1);
    const int m1 = std::min (m0 + 1, WavetableSet::kNumMips - 1);
    const float mw = std::clamp (mipLevel - static_cast<float> (m0), 0.0f, 1.0f);

    const float* a0 = t.frame (m0, f0);
    const float* a1 = t.frame (m0, f1);
    const float* b0 = t.frame (m1, f0);
    const float* b1 = t.frame (m1, f1);

    float sumL = 0.0f, sumR = 0.0f;
    for (int u = 0; u < unison; ++u)
    {
        const float idx = static_cast<float> (phases[static_cast<size_t> (u)]) * WavetableSet::kFrameSize;
        const int i = std::min (static_cast<int> (idx), WavetableSet::kFrameSize - 1);
        const float fr = idx - static_cast<float> (i);

        const float sa0 = a0[i] + fr * (a0[i + 1] - a0[i]);
        const float sa1 = a1[i] + fr * (a1[i + 1] - a1[i]);
        float s = sa0 + ff * (sa1 - sa0);

        if (mw > 0.001f)
        {
            const float sb0 = b0[i] + fr * (b0[i + 1] - b0[i]);
            const float sb1 = b1[i] + fr * (b1[i + 1] - b1[i]);
            const float sb = sb0 + ff * (sb1 - sb0);
            s += mw * (sb - s);
        }

        sumL += s * panL[static_cast<size_t> (u)];
        sumR += s * panR[static_cast<size_t> (u)];
    }

    *outL = sumL;
    *outR = sumR;

}

void Voice::render (const ChunkContext& ctx, float* left, float* right) noexcept
{
    if (! ampEnv.isActive() || ctx.table == nullptr)
        return;

    ampEnv.setSettings (ctx.ampEnv);
    filterEnv.setSettings (ctx.filterEnv);
    driveL.setDrive (ctx.drive);
    driveR.setDrive (ctx.drive);

    const int unison = std::clamp (ctx.unison, 1, kMaxUnison);
    const float unisonNorm = 1.0f / std::sqrt (static_cast<float> (unison));

    // Per-chunk unison layout: detune and stereo position.
    std::array<double, kMaxUnison> baseInc {};
    const float baseNote = static_cast<float> (note + 12 * ctx.octaveShift);
    float maxHz = 0.0f;
    for (int u = 0; u < unison; ++u)
    {
        const float spread = unison > 1 ? (static_cast<float> (u) / static_cast<float> (unison - 1)) * 2.0f - 1.0f : 0.0f;
        const float hz = midiToHz (baseNote + 0.01f * ctx.detuneCents * 0.5f * spread);
        baseInc[static_cast<size_t> (u)] = static_cast<double> (hz) / sampleRate;
        maxHz = std::max (maxHz, hz);
        float l, r;
        panGains (spread * ctx.width, l, r);
        panL[static_cast<size_t> (u)] = l * unisonNorm;
        panR[static_cast<size_t> (u)] = r * unisonNorm;
    }

    // Mip level: level m keeps harmonics ≤ 1024 >> m; choose the blend that keeps the
    // highest harmonic around Nyquist for the highest (pitch-modulated) unison voice.
    const float nyquist = 0.5f * static_cast<float> (sampleRate);
    const float peakRatio = ctx.pitchRatio[0];
    const float mipLevel = std::max (0.0f, std::log2 (static_cast<float> (WavetableSet::kMaxHarmonic) * maxHz * peakRatio / nyquist));
    // Integer part m0 has more harmonics (may alias slightly), m0+1 fewer; blend weight
    // is the fractional part, so aliasing fades out as pitch rises within an octave.

    const float noteOffsetOct = ctx.keyTrack * (static_cast<float> (note) - 60.0f) / 12.0f;
    SvfCoefs coefs, coefsButter;

    for (int i = 0; i < ctx.numSamples; ++i)
    {
        // advance phases
        const double ratio = static_cast<double> (ctx.pitchRatio[static_cast<size_t> (i)]);
        for (int u = 0; u < unison; ++u)
        {
            auto& p = phase[static_cast<size_t> (u)];
            p += baseInc[static_cast<size_t> (u)] * ratio;
            if (p >= 1.0)
                p -= std::floor (p);
        }

        float l = 0.0f, r = 0.0f;
        readTable (*ctx.table, ctx.position[static_cast<size_t> (i)], mipLevel, phase, unison, &l, &r);

        if (ctx.prevTable != nullptr)
        {
            float pl = 0.0f, pr = 0.0f;
            readTable (*ctx.prevTable, ctx.position[static_cast<size_t> (i)], mipLevel, phase, unison, &pl, &pr);
            const float w = ctx.tableFade[static_cast<size_t> (i)];
            l = pl + w * (l - pl);
            r = pr + w * (r - pr);
        }

        if (! driveL.isBypassed())
        {
            l = driveL.process (l);
            r = driveR.process (r);
        }

        // filter
        const float fenv = filterEnv.next();
        const float cutoff = ctx.cutoffHz * std::exp2 (ctx.envAmountOct * fenv + noteOffsetOct + ctx.cutoffOct[static_cast<size_t> (i)]);
        coefs.set (cutoff, ctx.damping, sampleRate);

        float fl = 0.0f, fr = 0.0f;
        switch (ctx.filterType)
        {
            case params::FltType::kLP24:
            {
                coefsButter.set (cutoff, 1.4142f, sampleRate);
                fl = svfL2.process (svfL1.process (l, coefsButter).low, coefs).low;
                fr = svfR2.process (svfR1.process (r, coefsButter).low, coefs).low;
                break;
            }
            case params::FltType::kBP:
            {
                fl = svfL1.process (l, coefs).band;
                fr = svfR1.process (r, coefs).band;
                break;
            }
            case params::FltType::kHP:
            {
                fl = svfL1.process (l, coefs).high;
                fr = svfR1.process (r, coefs).high;
                break;
            }
            case params::FltType::kLP12:
            default:
            {
                fl = svfL1.process (l, coefs).low;
                fr = svfR1.process (r, coefs).low;
                break;
            }
        }

        const float amp = ampEnv.next() * velocity * ctx.level * ctx.ampMod[static_cast<size_t> (i)];

        float pl = 1.0f, pr = 1.0f;
        const float pan = ctx.panMod[static_cast<size_t> (i)];
        if (pan != 0.0f)
        {
            panGains (pan, pl, pr);
            pl *= 1.41421356f;
            pr *= 1.41421356f;
        }

        left[i]  += fl * amp * pl;
        right[i] += fr * amp * pr;

        if (gateRemaining > 0 && --gateRemaining == 0)
            release();
    }

    lastCutoff = ctx.cutoffHz * std::exp2 (ctx.envAmountOct * filterEnv.getLevel() + noteOffsetOct);

    if (! ampEnv.isActive())
        reset();
}

} // namespace worldtable::dsp
