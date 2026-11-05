#include "SkyDays.h"

#include <WorldtableSkyData.h>

namespace worldtable::data
{

bool loadBundledDay (const juce::String& dayId, RawTrack& out)
{
    // CMake bundles resources/sky/<day>/track.json as "<day>.json".
    const auto wanted = dayId + ".json";

    for (int i = 0; i < WorldtableSkyData::namedResourceListSize; ++i)
    {
        const auto* name = WorldtableSkyData::namedResourceList[i];
        if (juce::String (WorldtableSkyData::getNamedResourceOriginalFilename (name)) != wanted)
            continue;

        int size = 0;
        if (const auto* bytes = WorldtableSkyData::getNamedResource (name, size))
            return parseTrackJson (juce::String::fromUTF8 (bytes, size), out);
        return false;
    }
    return false;
}

} // namespace worldtable::data
