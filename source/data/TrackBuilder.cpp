#include "TrackBuilder.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace worldtable::data
{
namespace
{
    constexpr int kShortGapSamples = 10;    // gaps up to this long are interpolated
    constexpr double kDecayTauSamples = 60; // longer gaps: hold the last value, decay toward climatology
    constexpr std::array<const char*, WorldSnapshot::kChannels> kJsonNames { "h", "kp", "v", "n", "bz", "by" };
    /** What gaps decay toward (the sketch's CLIMATOLOGY); H uses its quiet level instead. */
    constexpr std::array<double, WorldSnapshot::kChannels> kClimatology { 0.0, 2.0, 400.0, 5.0, 0.0, 0.0 };

    bool isMissing (double x) noexcept { return std::isnan (x); }

    /** data.js fillGaps, sample for sample. */
    void fillGaps (const std::vector<double>& values, std::vector<double>& out, double climate)
    {
        const auto n = static_cast<int> (values.size());
        out.assign (values.size(), 0.0);
        int i = 0, lastIndex = -1;
        double last = 0.0;
        bool haveLast = false;

        while (i < n)
        {
            if (! isMissing (values[static_cast<size_t> (i)]))
            {
                last = out[static_cast<size_t> (i)] = values[static_cast<size_t> (i)];
                haveLast = true;
                lastIndex = i++;
                continue;
            }

            int j = i;
            while (j < n && isMissing (values[static_cast<size_t> (j)]))
                ++j;

            const int gap = j - i;
            const bool haveNext = j < n;
            const double next = haveNext ? values[static_cast<size_t> (j)] : 0.0;

            for (int k = i; k < j; ++k)
            {
                auto& o = out[static_cast<size_t> (k)];
                if (haveLast && haveNext && gap <= kShortGapSamples)
                    o = last + (next - last) * (k - lastIndex) / (j - lastIndex);
                else if (haveLast)
                    o = climate + (last - climate) * std::exp (-(k - lastIndex) / kDecayTauSamples);
                else
                    o = climate;
            }
            i = j;
        }
    }
} // namespace

double quietBaseline (const std::vector<double>& h)
{
    constexpr int win = 360, hop = 30;
    const auto n = static_cast<int> (h.size());
    std::vector<double> seg, best;
    double bestStd = std::numeric_limits<double>::infinity();

    for (int s = 0; s + win <= n; s += hop)
    {
        seg.clear();
        for (int k = s; k < s + win; ++k)
            if (! isMissing (h[static_cast<size_t> (k)]))
                seg.push_back (h[static_cast<size_t> (k)]);

        if (static_cast<double> (seg.size()) < win * 0.8)
            continue;

        double sum = 0.0;
        for (double x : seg)
            sum += x;
        const double mean = sum / static_cast<double> (seg.size());

        double sq = 0.0;
        for (double x : seg)
            sq += (x - mean) * (x - mean);
        const double sd = std::sqrt (sq / static_cast<double> (seg.size()));

        if (sd < bestStd)
        {
            bestStd = sd;
            best = seg;
        }
    }

    if (best.empty())
        for (double x : h)
            if (! isMissing (x))
                best.push_back (x);

    if (best.empty())
        return 0.0;

    std::sort (best.begin(), best.end());
    return best[best.size() >> 1];
}

bool parseTrackJson (const juce::String& json, RawTrack& out)
{
    const auto root = juce::JSON::parse (json);
    if (! root.isObject())
        return false;

    const auto start = juce::Time::fromISO8601 (root["start"].toString());
    const double step = root["step_s"];
    const auto channels = root["channels"];
    if (start.toMilliseconds() == 0 || step <= 0.0 || ! channels.isObject())
        return false;

    out.startUtc = static_cast<double> (start.toMilliseconds()) / 1000.0;
    out.stepSeconds = step;
    out.station = StationId::CMO;

    for (size_t c = 0; c < kJsonNames.size(); ++c)
    {
        const auto* values = channels[kJsonNames[c]].getArray();
        if (values == nullptr)
            return false;

        auto& dst = out.channels[c];
        dst.clear();
        dst.reserve (static_cast<size_t> (values->size()));
        for (const auto& v : *values)
            dst.push_back (v.isVoid() || v.isUndefined() ? std::numeric_limits<double>::quiet_NaN()
                                                         : static_cast<double> (v));

        if (dst.size() != out.channels[0].size())
            return false;
    }
    return true;
}

bool prepareTrack (const RawTrack& raw, WorldSnapshot& out, uint64_t sequence)
{
    const int length = raw.length();
    if (length < 2 || length > WorldSnapshot::kCapacity)
        return false;

    const double baseline = quietBaseline (raw.channels[0]);
    std::vector<double> filled;

    for (size_t c = 0; c < raw.channels.size(); ++c)
    {
        const auto& values = raw.channels[c];
        if (static_cast<int> (values.size()) != length)
            return false;

        const double offset = c == 0 ? baseline : 0.0;
        fillGaps (values, filled, c == 0 ? baseline : kClimatology[c]);

        for (size_t i = 0; i < values.size(); ++i)
        {
            out.value[c][i] = static_cast<float> (filled[i] - offset);
            out.valid[c][i] = isMissing (values[i]) ? 0 : 1;
        }
        for (size_t i = values.size(); i < static_cast<size_t> (WorldSnapshot::kCapacity); ++i)
        {
            out.value[c][i] = 0.0f;
            out.valid[c][i] = 0;
        }
    }

    out.startUtc = raw.startUtc;
    out.stepSeconds = raw.stepSeconds;
    out.length = length;
    out.hQuietNt = static_cast<float> (baseline);
    out.station = raw.station;
    out.sequence = sequence;
    return true;
}

} // namespace worldtable::data
