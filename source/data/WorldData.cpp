#include "WorldData.h"

#include <algorithm>
#include <cmath>

namespace worldtable::data
{

namespace
{
/** Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant's days_from_civil).
    Pure arithmetic: no time zone or DST can leak in (juce::Time's UTC constructor
    applied the local offset on macOS). */
long long daysFromCivil (int y, int m, int d)
{
    y -= m <= 2 ? 1 : 0;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const long long yoe = y - era * 400;
    const long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

double utcSeconds (int year, int month, int day, int hour, int minute)
{
    return static_cast<double> (daysFromCivil (year, month, day) * 86400LL + hour * 3600LL + minute * 60LL);
}

bool parseFloat (const juce::String& token, float& value)
{
    if (token.isEmpty() || ! token.containsOnly ("0123456789.-+eE"))
        return false;
    value = token.getFloatValue();
    return std::isfinite (value);
}
} // namespace

juce::Result parseNdbcSpectral (const juce::String& text, SpectrumSeries& out)
{
    out.rows.clear();

    juce::StringArray lines;
    lines.addLines (text);

    for (const auto& rawLine : lines)
    {
        const auto line = rawLine.trim();
        if (line.isEmpty() || line.startsWithChar ('#'))
            continue;

        juce::StringArray tokens;
        tokens.addTokens (line, " \t", "");
        tokens.removeEmptyStrings();

        // YYYY MM DD hh mm sep_freq, then pairs "value (freq)"
        if (tokens.size() < 6 + 2 * 4)
            continue;

        int date[5] {};
        bool ok = true;
        for (int i = 0; i < 5 && ok; ++i)
        {
            ok = tokens[i].containsOnly ("0123456789");
            date[i] = tokens[i].getIntValue();
        }
        if (! ok || date[0] < 1970 || date[1] < 1 || date[1] > 12 || date[2] < 1 || date[2] > 31)
            continue;

        SpectrumRow row;
        row.timeUtc = utcSeconds (date[0], date[1], date[2], date[3], date[4]);

        for (int i = 6; i + 1 < tokens.size(); i += 2)
        {
            float value = 0.0f, freq = 0.0f;
            const auto freqToken = tokens[i + 1].trimCharactersAtStart ("(").trimCharactersAtEnd (")");
            if (! parseFloat (tokens[i], value) || ! parseFloat (freqToken, freq) || freq <= 0.0f || value < 0.0f)
            {
                ok = false;
                break;
            }
            row.frequencyHz.push_back (freq);
            row.density.push_back (value);
        }

        if (! ok || row.frequencyHz.size() < 4)
            continue;
        if (! std::is_sorted (row.frequencyHz.begin(), row.frequencyHz.end()))
            continue;

        out.rows.push_back (std::move (row));
    }

    if (out.rows.empty())
        return juce::Result::fail ("No valid spectral rows");

    std::sort (out.rows.begin(), out.rows.end(),
               [] (const SpectrumRow& a, const SpectrumRow& b) { return a.timeUtc < b.timeUtc; });
    return juce::Result::ok();
}

juce::Result parseUsgsGeomag (const juce::String& json, ScalarSeries& out)
{
    out.values.clear();

    juce::var root;
    const auto parsed = juce::JSON::parse (json, root);
    if (parsed.failed())
        return parsed;

    const auto* times = root.getProperty ("times", {}).getArray();
    const auto* values = root.getProperty ("values", {}).getArray();
    if (times == nullptr || values == nullptr || values->isEmpty())
        return juce::Result::fail ("Missing times/values");

    const auto* samples = (*values)[0].getProperty ("values", {}).getArray();
    if (samples == nullptr || samples->size() != times->size() || samples->size() < 2)
        return juce::Result::fail ("Malformed values");

    const auto t0 = juce::Time::fromISO8601 ((*times)[0].toString());
    const auto t1 = juce::Time::fromISO8601 ((*times)[1].toString());
    out.startUtc = static_cast<double> (t0.toMilliseconds()) / 1000.0;
    out.periodSeconds = static_cast<double> ((t1 - t0).inSeconds());
    if (out.periodSeconds <= 0.0)
        out.periodSeconds = 60.0;

    const int n = samples->size();
    out.values.resize (static_cast<size_t> (n));
    std::vector<bool> valid (static_cast<size_t> (n), false);
    int validCount = 0;

    for (int i = 0; i < n; ++i)
    {
        const auto& v = (*samples)[i];
        if (v.isDouble() || v.isInt() || v.isInt64())
        {
            const auto f = static_cast<float> (static_cast<double> (v));
            if (std::isfinite (f))
            {
                out.values[static_cast<size_t> (i)] = f;
                valid[static_cast<size_t> (i)] = true;
                ++validCount;
            }
        }
    }

    if (validCount < 16)
    {
        out.values.clear();
        return juce::Result::fail ("Too few valid samples");
    }

    // fill gaps
    int lastValid = -1;
    for (int i = 0; i < n; ++i)
    {
        if (! valid[static_cast<size_t> (i)])
            continue;

        if (lastValid < 0)
        {
            for (int k = 0; k < i; ++k)
                out.values[static_cast<size_t> (k)] = out.values[static_cast<size_t> (i)];
        }
        else if (i - lastValid > 1)
        {
            const float a = out.values[static_cast<size_t> (lastValid)];
            const float b = out.values[static_cast<size_t> (i)];
            for (int k = lastValid + 1; k < i; ++k)
                out.values[static_cast<size_t> (k)] = a + (b - a) * static_cast<float> (k - lastValid) / static_cast<float> (i - lastValid);
        }
        lastValid = i;
    }
    for (int k = lastValid + 1; k < n; ++k)
        out.values[static_cast<size_t> (k)] = out.values[static_cast<size_t> (lastValid)];

    return juce::Result::ok();
}

} // namespace worldtable::data
