#pragma once

#include <array>
#include <cstdint>

namespace worldtable::dsp
{

/** The resonator bank (spec/05-sound-engine.md §1): prototype/sky/sound/bank.js, line by line.

    Complex one-pole resonators, z ← r·e^{iω}·z + a·x, output Im(z). Frequency and decay change
    smoothly on a ringing resonator (the phasor just turns at another rate), and the input is
    normalised by √(1−r²) (the caller folds it into the input amplitude), so loudness doesn't depend
    on decay. State and coefficients are double: at T60 = 20 s, 1 − r ≈ 7e-6, close to float's
    resolution.

    Layout: index k = (bank · 3 + layer) · V + voice. Two banks: a chord change retunes the silent
    one and the caller moves the excitation over to it (double-banking). Inactive banks cost nothing.
    Each voice has its own seeded white noise, shared by its layers and banks.

    Fixed capacity: nothing allocates after construction. Every method is real-time safe. */
class ResonatorBank
{
public:
    static constexpr int kMaxVoices = 128;
    static constexpr int kBanks = 2;
    static constexpr int kLayers = 3;
    static constexpr int kMaxResonators = kBanks * kLayers * kMaxVoices;

    /** Silences everything, sets V voices (≤ kMaxVoices), seeds the noise. Both banks inactive. */
    void prepare (int voices, double sampleRate, uint32_t seed) noexcept;

    int voices() const noexcept { return numVoices; }
    static int index (int bank, int layer, int voice, int voices) noexcept { return (bank * kLayers + layer) * voices + voice; }
    int index (int bank, int layer, int voice) const noexcept { return index (bank, layer, voice, numVoices); }

    /** Pole radius for a decay time (T60, s). */
    double radius (double t60Seconds) const noexcept;

    /** Sets resonator k to `hz` with pole radius r. */
    void tune (int k, double hz, double r) noexcept;

    /** Input amplitude of resonator k (× its voice's noise). */
    void setInput (int k, double amplitude) noexcept { inA[static_cast<size_t> (k)] = amplitude; }

    /** Ramps the output gains of resonator k to (left, right) over `samples`. As in the sketch, the
        ramp doesn't stop there: the caller ramps again every tick, for exactly the tick's length. */
    void ramp (int k, double left, double right, int samples) noexcept;

    void setActive (int bank, bool active) noexcept { activeBank[static_cast<size_t> (bank)] = active; }
    bool isActive (int bank) const noexcept         { return activeBank[static_cast<size_t> (bank)]; }

    /** Silences a bank (state and input), copying the output gains from the other bank so nothing
        jumps. */
    void clear (int bank) noexcept;

    /** NaN/inf guard (spec/05 §8): resets a resonator rather than let it spread. Returns resets. */
    int guard() noexcept;

    /** Renders n samples (overwrites). */
    void process (float* left, float* right, int numSamples) noexcept;

private:
    int    numVoices = 0;
    double sampleRate = 48000.0;
    std::array<double, kMaxResonators> re {}, im {};      ///< state
    std::array<double, kMaxResonators> c {}, s {};        ///< r·cos ω, r·sin ω
    std::array<double, kMaxResonators> inA {};            ///< input amplitude
    std::array<double, kMaxResonators> gL {}, gR {};      ///< output gains, ramped per sample
    std::array<double, kMaxResonators> dgL {}, dgR {};
    std::array<uint32_t, kMaxVoices> noise {};            ///< xorshift32 state per voice
    std::array<double, kMaxVoices> xn {};                 ///< this sample's noise per voice
    std::array<bool, kBanks> activeBank {};
};

} // namespace worldtable::dsp
