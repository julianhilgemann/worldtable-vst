#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace worldtable::data
{

/** One spectrum at one time, e.g. an NDBC buoy's spectral wave density. */
struct SpectrumRow
{
    double timeUtc = 0.0;            ///< seconds since the Unix epoch
    std::vector<float> frequencyHz;  ///< ascending
    std::vector<float> density;      ///< same length; raw unit (m²/Hz for NDBC)
};

/** A time-ordered sequence of spectra (oldest first). */
struct SpectrumSeries
{
    std::vector<SpectrumRow> rows;
};

/** A regularly sampled scalar series (oldest first), gaps already filled. */
struct ScalarSeries
{
    double startUtc = 0.0;
    double periodSeconds = 60.0;
    std::vector<float> values;
};

/** Parses NOAA NDBC `data/realtime2/<station>.data_spec` text.
    Rows are newest-first in the file; the result is oldest-first. Malformed rows
    and rows with missing values ("MM") are skipped. Fails if no row survives.
    Pure: no I/O. */
juce::Result parseNdbcSpectral (const juce::String& text, SpectrumSeries& out);

/** Parses a USGS geomagnetism web-service JSON response (`format=json`, one element).
    Null samples are linearly interpolated; leading/trailing nulls take the nearest
    value. Fails if the structure is wrong or fewer than 16 valid samples exist.
    Pure: no I/O. */
juce::Result parseUsgsGeomag (const juce::String& json, ScalarSeries& out);

} // namespace worldtable::data
