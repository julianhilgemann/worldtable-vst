#pragma once

#include <juce_core/juce_core.h>

namespace worldtable::data
{

/** The curated POC sources, indexed like params::WtSource. */
struct SourceDef
{
    enum class Kind { ndbcSpectral, usgsGeomag };

    Kind kind;
    const char* station;
    const char* place;
    const char* provider;
    const char* factoryFile;      ///< original filename of the bundled recording
    int refreshMinutes;           ///< polite polling interval
};

int numSources() noexcept;
const SourceDef& sourceDef (int index) noexcept;

/** Request URL for a source at time `nowUtc` (seconds since epoch). */
juce::URL buildRequestUrl (const SourceDef&, double nowUtc);

/** Identifying User-Agent (spec/02-data-sources.md §Ethics). */
juce::String userAgent();

/** Bundled factory-world recording for a source (empty if missing). */
juce::String loadFactoryData (const SourceDef&);

} // namespace worldtable::data
