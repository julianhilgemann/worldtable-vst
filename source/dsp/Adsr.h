#pragma once

#include "DspCommon.h"

namespace worldtable::dsp
{

/** ADSR with analog-style curves.
    Attack approaches an overshoot target (concave rise); decay and release are
    exponential, with times measured to −60 dB. A retrigger starts the attack from
    the current level, so a stolen or re-struck voice never jumps. */
class Adsr
{
public:
    enum class Stage { idle, attack, decay, sustain, release };

    struct Settings
    {
        float attack = 0.01f, decay = 0.1f, sustain = 1.0f, release = 0.1f; // seconds / level
    };

    void prepare (double rate) noexcept { sampleRate = rate; reset(); }
    void reset() noexcept               { stage = Stage::idle; level = 0.0f; }

    void setSettings (const Settings& s) noexcept
    {
        settings = s;
        decayCoef   = timeToCoef (s.decay);
        releaseCoef = timeToCoef (s.release);
        if (stage == Stage::attack)
            computeAttack();
    }

    void noteOn() noexcept   { stage = Stage::attack; computeAttack(); }
    void noteOff() noexcept  { if (stage != Stage::idle) stage = Stage::release; }

    bool isActive() const noexcept   { return stage != Stage::idle; }
    bool isReleasing() const noexcept { return stage == Stage::release; }
    float getLevel() const noexcept  { return level; }
    Stage getStage() const noexcept  { return stage; }

    float next() noexcept
    {
        switch (stage)
        {
            case Stage::idle:
                return 0.0f;

            case Stage::attack:
                level = kAttackTarget + (level - kAttackTarget) * attackCoef;
                if (level >= 1.0f)
                {
                    level = 1.0f;
                    stage = Stage::decay;
                }
                return level;

            case Stage::decay:
                level = settings.sustain + (level - settings.sustain) * decayCoef;
                if (level - settings.sustain < 1.0e-4f)
                {
                    level = settings.sustain;
                    stage = Stage::sustain;
                }
                return level;

            case Stage::sustain:
                // Sustain can be moved while held: glide there instead of jumping.
                level = settings.sustain + (level - settings.sustain) * decayCoef;
                return level;

            case Stage::release:
                level *= releaseCoef;
                if (level < 1.0e-4f)
                {
                    level = 0.0f;
                    stage = Stage::idle;
                }
                return level;
        }
        return level;
    }

private:
    static constexpr float kAttackTarget = 1.3f;

    float timeToCoef (float seconds) const noexcept
    {
        const auto samples = std::max (1.0f, seconds * static_cast<float> (sampleRate));
        return std::exp (std::log (0.001f) / samples);   // reach −60 dB after `seconds`
    }

    void computeAttack() noexcept
    {
        // Choose the coefficient so the curve reaches 1.0 after `attack` seconds
        // starting from the current level.
        const float start = std::min (level, 0.999f);
        const float ratio = (kAttackTarget - 1.0f) / (kAttackTarget - start);
        const float samples = std::max (1.0f, settings.attack * static_cast<float> (sampleRate));
        attackCoef = std::exp (std::log (ratio) / samples);
    }

    double sampleRate = 48000.0;
    Settings settings;
    Stage stage = Stage::idle;
    float level = 0.0f;
    float attackCoef = 0.0f, decayCoef = 0.0f, releaseCoef = 0.0f;
};

} // namespace worldtable::dsp
