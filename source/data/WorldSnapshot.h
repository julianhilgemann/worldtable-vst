#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// PROTECTED INTERFACE (CLAUDE.md rule 6): ask the owner before changing it.

namespace worldtable::data
{

/** Magnetometer stations the sky is seen from (ADR-0006). v1: CMO only. */
enum class StationId : uint8_t
{
    CMO = 0,
};

struct StationInfo
{
    const char* code;
    const char* name;
    double latDeg;   ///< geographic
    double lonDeg;   ///< geographic, east positive
    double elevKm;
};

constexpr StationInfo stationInfo (StationId id) noexcept
{
    switch (id)
    {
        case StationId::CMO:
        default:
            return { "CMO", "College, Alaska", 64.874, -147.86, 0.197 };
    }
}

/** Track channels in storage order, physical units. */
enum class WorldChannel : uint8_t
{
    hDeviation = 0,  ///< nT: station H minus its quiet level (WorldSnapshot::hQuietNt)
    kp,              ///< 0 … 9
    windSpeed,       ///< km/s
    density,         ///< cm⁻³
    bz,              ///< nT, GSM
    by,              ///< nT, GSM
};

/** A data track for the Sky Model: spec/03-architecture.md, hand-off A.

    Built on the data thread and immutable once published. Samples sit on a regular grid (1 min).
    Gaps are already filled the way the T1.2 sketch fills them (`prototype/sky/data.js`): gaps of
    up to 10 min are interpolated; longer ones hold the last value and decay toward climatology
    (Kp 2, 400 km/s, 5 cm⁻³, Bz 0) with τ 60 min. `valid` marks the samples that were measured.

    About 130 KB: it lives in a pre-allocated pool, never on the stack. The audio thread only reads it. */
struct WorldSnapshot
{
    static constexpr int kChannels = 6;
    static constexpr int kCapacity = 4320;   ///< 72 h at 1 min

    double    startUtc    = 0.0;    ///< time of sample 0, seconds since the Unix epoch
    double    stepSeconds = 60.0;
    int       length      = 0;      ///< samples in use, 2 … kCapacity
    float     hQuietNt    = 0.0f;   ///< quiet level subtracted from H (median of the calmest 6 h)
    StationId station     = StationId::CMO;
    uint64_t  sequence    = 0;      ///< grows with every published track

    std::array<std::array<float,   kCapacity>, kChannels> value {};
    std::array<std::array<uint8_t, kCapacity>, kChannels> valid {};   ///< 1 = measured, 0 = filled

    double endUtc() const noexcept { return startUtc + length * stepSeconds; }

    /** Linear interpolation at a UTC time, clamped to the first and last sample
        (the sketch's `sample()`). */
    double sample (WorldChannel channel, double utc) const noexcept
    {
        if (length <= 0)
            return 0.0;

        const auto& a = value[static_cast<size_t> (channel)];
        const double x = (utc - startUtc) / stepSeconds;

        if (x <= 0.0)
            return a[0];
        if (x >= length - 1)
            return a[static_cast<size_t> (length - 1)];

        const auto i = static_cast<size_t> (x);   // x > 0: truncation is floor
        const double lo = a[i], hi = a[i + 1];
        return lo + (hi - lo) * (x - static_cast<double> (i));
    }

    /** Whether the sample at or before `utc` was measured, clamped to the track
        (the sketch's `valid[name][floor(…)]`). */
    bool validAt (WorldChannel channel, double utc) const noexcept
    {
        if (length <= 0)
            return false;

        const double x = std::floor ((utc - startUtc) / stepSeconds);
        const int i = x < 0.0 ? 0 : x > length - 1 ? length - 1 : static_cast<int> (x);
        return valid[static_cast<size_t> (channel)][static_cast<size_t> (i)] != 0;
    }
};

static_assert (std::is_trivially_copyable_v<WorldSnapshot>);
static_assert (std::is_standard_layout_v<WorldSnapshot>);

} // namespace worldtable::data
