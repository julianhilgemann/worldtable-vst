#pragma once

#include "DspCommon.h"

namespace worldtable::dsp
{

/** "Fatten" stage: asymmetric tanh saturation with first-order antiderivative
    anti-aliasing (ADAA), so drive adds warmth without fizzy aliasing.
    The asymmetry (bias) adds even harmonics; its DC is removed afterwards.
    drive = 0 bypasses exactly; small drive values blend in the shaper. */
class Drive
{
public:
    void prepare (double rate) noexcept { dc.prepare (rate); reset(); }
    void reset() noexcept               { xPrev = 0.0f; dc.reset(); }

    /** Call once per chunk. */
    void setDrive (float amount) noexcept
    {
        amount = std::clamp (amount, 0.0f, 1.0f);
        wet    = std::min (1.0f, amount * 6.0f);
        gain   = 1.0f + 22.0f * amount * std::sqrt (amount);
        bias   = 0.35f * amount;
        biasOut = std::tanh (bias);
        makeup = 0.5f / std::tanh (0.5f * gain);
    }

    bool isBypassed() const noexcept { return wet <= 0.0f; }

    float process (float in) noexcept
    {
        const float x = in * gain + bias;
        const float dx = x - xPrev;

        float y;
        if (std::abs (dx) > 1.0e-4f)
            y = (logCosh (x) - logCosh (xPrev)) / dx;
        else
            y = std::tanh (0.5f * (x + xPrev));

        xPrev = x;
        const float shaped = dc.process ((y - biasOut) * makeup);
        return in + wet * (shaped - in);
    }

private:
    static float logCosh (float x) noexcept
    {
        const float a = std::abs (x);
        // log(cosh x) = |x| - log 2 + log(1 + e^(-2|x|)), stable for large |x|
        return a - 0.69314718f + std::log1p (std::exp (-2.0f * a));
    }

    float gain = 1.0f, bias = 0.0f, biasOut = 0.0f, makeup = 1.0f, wet = 0.0f;
    float xPrev = 0.0f;
    DcBlocker dc;
};

} // namespace worldtable::dsp
