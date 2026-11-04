#include "WorldSources.h"
#include "params/Params.h"

#include <WorldtableWorldData.h>

#include <array>
#include <ctime>

namespace worldtable::data
{

namespace
{
using K = SourceDef::Kind;

// Order must match params::WtSource (spec/parameters.yaml → wt_source).
constexpr std::array<SourceDef, 6> kSources { {
    { K::ndbcSpectral, "46026", "San Francisco", "NOAA NDBC", "46026.data_spec", 20 },
    { K::ndbcSpectral, "51201", "Waimea Bay, Hawaii", "NOAA NDBC / CDIP", "51201.data_spec", 20 },
    { K::ndbcSpectral, "44013", "Gulf of Maine, Boston", "NOAA NDBC", "44013.data_spec", 20 },
    { K::ndbcSpectral, "41010", "Cape Canaveral East", "NOAA NDBC", "41010.data_spec", 20 },
    { K::usgsGeomag,   "BOU",   "Boulder, Colorado", "USGS Geomagnetism", "BOU.json", 10 },
    { K::usgsGeomag,   "CMO",   "College, Alaska", "USGS Geomagnetism", "CMO.json", 10 },
} };

static_assert (kSources.size() == static_cast<size_t> (params::WtSource::kCount),
               "Source catalogue out of sync with parameters.yaml");

juce::String isoUtc (double seconds)
{
    const auto t = static_cast<std::time_t> (seconds);
    std::tm tm {};
#if JUCE_WINDOWS
    gmtime_s (&tm, &t);
#else
    gmtime_r (&t, &tm);
#endif
    char buf[32];
    std::strftime (buf, sizeof (buf), "%Y-%m-%dT%H:%M:00Z", &tm);
    return buf;
}
} // namespace

int numSources() noexcept { return static_cast<int> (kSources.size()); }

const SourceDef& sourceDef (int index) noexcept
{
    return kSources[static_cast<size_t> (juce::jlimit (0, numSources() - 1, index))];
}

juce::URL buildRequestUrl (const SourceDef& s, double nowUtc)
{
    if (s.kind == K::ndbcSpectral)
        return juce::URL ("https://www.ndbc.noaa.gov/data/realtime2/" + juce::String (s.station) + ".data_spec");

    return juce::URL ("https://geomag.usgs.gov/ws/data/")
        .withParameter ("id", s.station)
        .withParameter ("elements", "H")
        .withParameter ("sampling_period", "60")
        .withParameter ("format", "json")
        .withParameter ("starttime", isoUtc (nowUtc - 48.0 * 3600.0))
        .withParameter ("endtime", isoUtc (nowUtc));
}

juce::String userAgent()
{
    return "Worldtable/" WORLDTABLE_VERSION " (+https://github.com/julianhilgemann/worldtable-vst)";
}

juce::String loadFactoryData (const SourceDef& s)
{
    for (int i = 0; i < WorldtableWorldData::namedResourceListSize; ++i)
    {
        const auto* name = WorldtableWorldData::namedResourceList[i];
        if (juce::String (WorldtableWorldData::getNamedResourceOriginalFilename (name)) == s.factoryFile)
        {
            int size = 0;
            if (const auto* data = WorldtableWorldData::getNamedResource (name, size))
                return juce::String::fromUTF8 (data, size);
        }
    }
    return {};
}

} // namespace worldtable::data
