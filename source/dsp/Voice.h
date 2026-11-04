#pragma once

#include "Adsr.h"
#include "DspCommon.h"
#include "Drive.h"
#include "Svf.h"
#include "WavetableSet.h"

#include <array>

namespace worldtable::dsp
{

/** Everything a voice needs for one control chunk (≤ kChunk samples), prepared by the Engine. */
struct ChunkContext
{
    int numSamples = 0;
    double sampleRate = 48000.0;

    const WavetableSet* table = nullptr;      // current table (never null while rendering)
    const WavetableSet* prevTable = nullptr;  // fading-out table, or null
    std::array<float, kChunk> tableFade {};   // weight of `table` while prevTable fades out

    std::array<float, kChunk> position {};    // 0..1, base + modulation
    std::array<float, kChunk> pitchRatio {};  // frequency multiplier from pitch modulation
    std::array<float, kChunk> cutoffOct {};   // cutoff offset in octaves from modulation
    std::array<float, kChunk> ampMod {};      // amplitude multiplier (tremolo)
    std::array<float, kChunk> panMod {};      // -1..1 added to voice pan

    int unison = 1;
    float detuneCents = 0.0f;   // total spread (outermost voices at ±detuneCents/2)
    float width = 0.0f;
    float drive = 0.0f;
    float level = 1.0f;
    int octaveShift = 0;

    int filterType = 0;
    float cutoffHz = 1000.0f;
    float damping = 2.0f;
    float envAmountOct = 0.0f;
    float keyTrack = 0.0f;

    Adsr::Settings ampEnv, filterEnv;
};

class Voice
{
public:
    static constexpr int kMaxUnison = 6;

    enum class Owner { none, drone, euclid, midi };

    void prepare (double sampleRate, uint32_t seed) noexcept;
    void reset() noexcept;

    /** gateSamples < 0 holds until release(). */
    void start (int note, float velocity, Owner owner, int64_t gateSamples, uint64_t age) noexcept;
    void release() noexcept;

    bool isActive() const noexcept       { return ampEnv.isActive(); }
    bool isReleasing() const noexcept    { return ampEnv.isReleasing(); }
    int getNote() const noexcept         { return note; }
    Owner getOwner() const noexcept      { return owner; }
    uint64_t getAge() const noexcept     { return age; }
    float getAmpLevel() const noexcept   { return ampEnv.getLevel(); }
    float getFilterLevel() const noexcept { return filterEnv.getLevel(); }
    float getLastCutoff() const noexcept { return lastCutoff; }

    /** Adds this voice's output to left/right. */
    void render (const ChunkContext&, float* left, float* right) noexcept;

private:
    void readTable (const WavetableSet& t, float position, float mipLevel,
                     const std::array<double, kMaxUnison>& phases, int unison,
                     float* outL, float* outR) const noexcept;

    int note = 60;
    float velocity = 1.0f;
    Owner owner = Owner::none;
    int64_t gateRemaining = -1;
    uint64_t age = 0;

    std::array<double, kMaxUnison> phase {};
    std::array<float, kMaxUnison> panL {}, panR {};
    Adsr ampEnv, filterEnv;
    Drive driveL, driveR;
    Svf svfL1, svfR1, svfL2, svfR2;
    FastRandom rng;
    float lastCutoff = 1000.0f;
    double sampleRate = 48000.0;
};

} // namespace worldtable::dsp
