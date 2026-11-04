#pragma once

#include "WorldData.h"
#include "WorldSources.h"
#include "dsp/TableExchange.h"
#include "params/Params.h"
#include "transform/WavetableBuilder.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <optional>

namespace worldtable::data
{

/** What the UI shows about the current table (immutable once published). */
struct TableInfo
{
    transform::TablePreview preview;
    int sourceIndex = 0;
    juce::String station, place, provider;
    juce::String origin;          ///< "live", "cache" or "factory"
    double fetchedAtUtc = 0.0;    ///< when the data was downloaded (0 for factory)
    uint64_t generation = 0;
};

/** Status line for the UI. */
struct FeedStatus
{
    juce::String state = "idle";  ///< idle | fetching | live | offline | disabled
    juce::String message;
    double lastSuccessUtc = 0.0;
    double nextFetchUtc = 0.0;
};

/** Background data pipeline (spec/03: data + builder threads, merged for the POC):
    fetch → cache → parse → build wavetable → publish via TableExchange.

    The audio thread never touches this object. Network access happens only on the
    feed thread; without network (or with networkEnabled = false) it plays from the
    disk cache or the bundled factory world. */
class WorldFeed : private juce::Thread
{
public:
    WorldFeed (dsp::TableExchange&, const params::ParamPointers&, bool networkEnabled);
    ~WorldFeed() override;

    /** Synchronously builds and publishes the table for the current parameters if
        anything changed. Only while the background thread is not running. */
    void prime();

    /** prime(), then starts the background thread (just a nudge if it already runs). */
    void start();

    /** Asks for an immediate refetch of the current source (UI "refresh"). */
    void requestRefresh();

    std::shared_ptr<const TableInfo> getTableInfo() const;
    FeedStatus getStatus() const;

private:
    void run() override;
    bool syncWithParameters (bool allowDebounce);
    bool loadLocal (int sourceIndex);
    bool fetch (int sourceIndex);
    bool parse (const SourceDef&, const juce::String& body);
    bool buildAndPublish();
    transform::TransformSettings currentSettings() const;
    int currentSource() const;
    juce::File cacheFile (const SourceDef&) const;
    void setStatus (const juce::String& state, const juce::String& message);

    dsp::TableExchange& exchange;
    const params::ParamPointers& params;
    const bool networkEnabled;

    transform::WavetableBuilder builder;

    // data currently loaded (feed thread / start() only)
    int loadedSource = -1;
    std::optional<SpectrumSeries> spectra;
    std::optional<ScalarSeries> series;
    juce::String origin = "factory";
    double fetchedAtUtc = 0.0;

    // build bookkeeping
    int builtSource = -1;
    transform::TransformSettings builtSettings;
    std::optional<transform::TransformSettings> settingsSeen;
    juce::uint32 settingsChangedAtMs = 0;
    uint64_t generation = 0;
    bool dirty = false;

    // fetch bookkeeping, per source
    std::array<double, 8> nextFetchUtc {};
    std::array<int, 8> failures {};
    std::atomic<bool> refreshRequested { false };

    mutable juce::CriticalSection infoLock;
    std::shared_ptr<const TableInfo> tableInfo;
    FeedStatus status;

    juce::CriticalSection streamLock;
    juce::WebInputStream* activeStream = nullptr;
};

} // namespace worldtable::data
