#pragma once

#include "TrackBuilder.h"

#include <juce_core/juce_core.h>

#include <array>

namespace worldtable::data
{

/** The recorded sky days bundled into the plug-in (resources/sky/, T1.1). */
struct SkyDay
{
    const char* id;      ///< folder name in resources/sky/, e.g. "2024-05-10"
    const char* label;
};

inline constexpr std::array<SkyDay, 3> kSkyDays { {
    { "2024-05-10", "10–11 May 2024 · Gannon storm (Kp 9)" },
    { "2025-01-19", "19–20 Jan 2025 · moderate, substorms" },
    { "2025-01-25", "25–26 Jan 2025 · quiet" },
} };

/** The default Journey (spec/01-functional-spec.md §3): the 10–11 May 2024 storm. */
inline constexpr const char* kDefaultSkyDay = "2024-05-10";

/** Parses a bundled day's track.json into `out`. Returns false for an unknown day. Allocates:
    data or message thread only. */
bool loadBundledDay (const juce::String& dayId, RawTrack& out);

} // namespace worldtable::data
