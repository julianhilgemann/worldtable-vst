#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace worldtable::dsp
{

/** A band-limited wavetable: up to kMaxFrames single-cycle frames, each stored at
    kNumMips mip levels (level m keeps harmonics 1 … kMaxHarmonic >> m).

    Storage is allocated once in the constructor and never resized, so a set can be
    rewritten by the builder thread and read by the audio thread without allocation.
    Each frame carries one guard sample (frame[kFrameSize] == frame[0]) for
    interpolation without wrapping. */
struct WavetableSet
{
    static constexpr int kFrameSize   = 2048;
    static constexpr int kStride      = kFrameSize + 1;
    static constexpr int kMaxFrames   = 64;
    static constexpr int kNumMips     = 10;
    static constexpr int kMaxHarmonic = kFrameSize / 2;

    WavetableSet() : samples (static_cast<size_t> (kNumMips) * kMaxFrames * kStride, 0.0f) {}

    float* frame (int mip, int index) noexcept
    {
        return samples.data() + (static_cast<size_t> (mip) * kMaxFrames + static_cast<size_t> (index)) * kStride;
    }

    const float* frame (int mip, int index) const noexcept
    {
        return samples.data() + (static_cast<size_t> (mip) * kMaxFrames + static_cast<size_t> (index)) * kStride;
    }

    static constexpr int harmonicsAtMip (int mip) noexcept { return kMaxHarmonic >> mip; }

    int numFrames = 0;          ///< frames in use, 1 … kMaxFrames
    uint64_t generation = 0;    ///< increases with every rebuild (for the UI)

private:
    std::vector<float> samples;
};

} // namespace worldtable::dsp
