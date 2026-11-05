#include "SkyModel.h"
#include "SlowSky.h"

#include <array>
#include <cmath>

namespace worldtable::sky
{

void SkyTimeline::build (const data::WorldSnapshot& source, uint32_t seedValue, const SkyParams& paramValues)
{
    track = &source;
    seed = seedValue;
    params = paramValues;

    const Observer observer (data::stationInfo (source.station), params.poleLatDeg, params.poleLonDeg);
    const auto stepsPerMinute = static_cast<int> (std::lround (60.0 / SkyModel::kSlowStepSeconds));
    const auto trackMinutes = static_cast<int> (std::lround (source.length * source.stepSeconds / 60.0));

    minutes.clear();
    events.clear();
    minutes.reserve (static_cast<size_t> (trackMinutes) + 1);

    auto state = initialSlowState (source, params, source.startUtc);
    std::array<SkyEvent, kMaxEventsPerSlowStep> stepEvents {};

    for (int minute = 0; minute < trackMinutes; ++minute)
    {
        minutes.push_back (state);

        for (int k = 0; k < stepsPerMinute; ++k)
        {
            const int n = slowStep (state, source, params, observer, seed, stepEvents);
            events.insert (events.end(), stepEvents.begin(), stepEvents.begin() + n);
        }
    }

    minutes.push_back (state);
}

const SlowSkyState* SkyTimeline::checkpointAtOrBefore (double utc) const noexcept
{
    if (minutes.empty() || track == nullptr)
        return nullptr;

    const double k = std::floor ((utc - track->startUtc) / 60.0);
    const double last = static_cast<double> (minutes.size() - 1);
    const double index = k < 0.0 ? 0.0 : k > last ? last : k;   // NaN falls through to 0 below
    return &minutes[index >= 0.0 ? static_cast<size_t> (index) : 0];
}

} // namespace worldtable::sky
