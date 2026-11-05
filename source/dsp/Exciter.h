#pragma once

#include "ResonatorBank.h"

#include <array>

namespace worldtable::dsp
{

/** The exciter (spec/05-sound-engine.md §2): prototype/sky/sound/engine.js, the excitation part of
    tick(). Per voice, once per 100 Hz control tick:

    - a continuous bed, bed × (0.5 + activity), so a quiet sky still sounds;
    - rising brightness excites: rise × max(0, db/dt), smoothed over 0.3 s;
    - a substorm onset strikes the bank: fast attack (80 ms), long tail (τ 6 s), level
      0.3 + 0.35 × strength (≤ 1.5), weighted 0.5 + 0.5 × a Gaussian around the onset node (σ 12 nodes).

    The bed and the rise are shared between the banks by the chord crossfade; the strike always goes
    to the newest bank, so it lands on the new chord. Real-time safe. */
class Exciter
{
public:
    static constexpr double kTickSeconds = 0.01;   ///< the Sky Model's tick (static_assert in SkySound.cpp)

    struct Settings
    {
        double bed = 0.3;      ///< continuous excitation, × (0.5 + activity)
        double rise = 0.4;     ///< excitation per unit of brightness rise per second
        double strike = 1.0;   ///< onset strike level
    };

    void prepare (int voices, int voicesPerNode, const Settings&) noexcept;

    /** A substorm onset, centred on `voice` (the onset node × voices per node). */
    void onset (double realSeconds, double strength, int voice) noexcept;

    /** One control tick. `brightness[v]`: the curtain's brightness at each voice. */
    void tick (double realSeconds, double activity, const double* brightness) noexcept;

    /** The bed and the rise of voice v this tick: split between the banks by the crossfade. */
    double continuous (int voice) const noexcept;

    /** The strike at voice v this tick: into the newest bank only. */
    double strike (int voice) const noexcept;

private:
    Settings settings;
    int numVoices = 0;
    double strikeWidth = 12.0;                  ///< voices
    double strikeStart = 0.0;
    double strikeAmp = 0.0;                     ///< 0 until the first onset
    double strikeVoice = 0.0;
    double strikeEnv = 0.0, bed = 0.0;          ///< this tick
    std::array<double, ResonatorBank::kMaxVoices> rise {}, prevBrightness {};
};

} // namespace worldtable::dsp
