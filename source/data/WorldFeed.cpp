#include "WorldFeed.h"

namespace worldtable::data
{

namespace
{
double nowUtc() { return static_cast<double> (juce::Time::currentTimeMillis()) / 1000.0; }
constexpr int kDebounceMs = 180;
constexpr int kMaxBodyBytes = 8 * 1024 * 1024;
} // namespace

WorldFeed::WorldFeed (dsp::TableExchange& ex, const params::ParamPointers& p, bool network)
    : juce::Thread ("Worldtable data"), exchange (ex), params (p), networkEnabled (network)
{
    if (! networkEnabled)
        status.state = "disabled";
}

WorldFeed::~WorldFeed()
{
    signalThreadShouldExit();
    {
        const juce::ScopedLock sl (streamLock);
        if (activeStream != nullptr)
            activeStream->cancel();
    }
    notify();
    stopThread (6000);
}

void WorldFeed::prime()
{
    jassert (! isThreadRunning());
    if (isThreadRunning())
        return;

    syncWithParameters (false);
    if (dirty)
        buildAndPublish();
}

void WorldFeed::start()
{
    if (isThreadRunning())
    {
        notify();
        return;
    }

    prime();
    startThread (juce::Thread::Priority::low);
}

void WorldFeed::requestRefresh()
{
    refreshRequested = true;
    notify();
}

std::shared_ptr<const TableInfo> WorldFeed::getTableInfo() const
{
    const juce::ScopedLock sl (infoLock);
    return tableInfo;
}

FeedStatus WorldFeed::getStatus() const
{
    const juce::ScopedLock sl (infoLock);
    return status;
}

void WorldFeed::setStatus (const juce::String& state, const juce::String& message)
{
    const juce::ScopedLock sl (infoLock);
    status.state = state;
    status.message = message;
}

transform::TransformSettings WorldFeed::currentSettings() const
{
    return { params.wt_spread->load(), params.wt_tilt->load(), params.wt_contrast->load(), params.wt_phase->load() };
}

int WorldFeed::currentSource() const
{
    return juce::jlimit (0, numSources() - 1, static_cast<int> (std::lround (params.wt_source->load())));
}

juce::File WorldFeed::cacheFile (const SourceDef& s) const
{
#if JUCE_MAC
    const auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support");
#else
    const auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
#endif
    return base.getChildFile ("Worldtable").getChildFile ("cache")
               .getChildFile (juce::String (s.station) + (s.kind == SourceDef::Kind::ndbcSpectral ? ".data_spec" : ".json"));
}

bool WorldFeed::parse (const SourceDef& s, const juce::String& body)
{
    if (s.kind == SourceDef::Kind::ndbcSpectral)
    {
        SpectrumSeries parsed;
        if (parseNdbcSpectral (body, parsed).failed())
            return false;
        spectra = std::move (parsed);
        series.reset();
    }
    else
    {
        ScalarSeries parsed;
        if (parseUsgsGeomag (body, parsed).failed())
            return false;
        series = std::move (parsed);
        spectra.reset();
    }
    return true;
}

bool WorldFeed::loadLocal (int index)
{
    const auto& s = sourceDef (index);

    if (networkEnabled)
    {
        const auto f = cacheFile (s);
        if (f.existsAsFile() && parse (s, f.loadFileAsString()))
        {
            origin = "cache";
            fetchedAtUtc = static_cast<double> (f.getLastModificationTime().toMilliseconds()) / 1000.0;
            return true;
        }
    }

    if (parse (s, loadFactoryData (s)))
    {
        origin = "factory";
        fetchedAtUtc = 0.0;
        return true;
    }
    return false;
}

bool WorldFeed::syncWithParameters (bool allowDebounce)
{
    const int src = currentSource();
    if (src != loadedSource)
    {
        loadLocal (src);
        loadedSource = src;
        dirty = true;
    }

    const auto settings = currentSettings();
    if (settings != builtSettings || builtSource != src)
    {
        if (! allowDebounce)
        {
            dirty = true;
        }
        else
        {
            // Wait until a knob has stopped moving briefly before rebuilding.
            const auto now = juce::Time::getMillisecondCounter();
            if (! settingsSeen || ! (*settingsSeen == settings))
            {
                settingsSeen = settings;
                settingsChangedAtMs = now;
            }
            else if (now - settingsChangedAtMs >= kDebounceMs)
            {
                dirty = true;
            }
        }
    }
    return dirty;
}

bool WorldFeed::buildAndPublish()
{
    if (! spectra && ! series)
        return false;

    int slot = -1;
    for (int attempt = 0; (slot = exchange.acquireForWriting()) < 0; ++attempt)
    {
        if (threadShouldExit() || attempt > 200)
            return false;
        wait (25);   // the audio thread is still fading between two tables
    }

    auto& table = exchange.slot (slot);
    const auto settings = currentSettings();
    auto info = std::make_shared<TableInfo>();

    const bool ok = spectra ? builder.build (*spectra, settings, table, info->preview)
                            : builder.build (*series, settings, table, info->preview);
    if (! ok)
        return false;

    table.generation = ++generation;
    exchange.publish (slot);

    const auto& def = sourceDef (loadedSource);
    info->sourceIndex = loadedSource;
    info->station = def.station;
    info->place = def.place;
    info->provider = def.provider;
    info->origin = origin;
    info->fetchedAtUtc = fetchedAtUtc;
    info->generation = generation;

    {
        const juce::ScopedLock sl (infoLock);
        tableInfo = std::move (info);
    }

    builtSource = loadedSource;
    builtSettings = settings;
    settingsSeen = settings;
    dirty = false;
    return true;
}

bool WorldFeed::fetch (int index)
{
    const auto& s = sourceDef (index);
    const auto now = nowUtc();
    setStatus ("fetching", juce::String ("Fetching ") + s.provider + " " + s.station + juce::String::fromUTF8 ("…"));

    auto url = buildRequestUrl (s, now);
    juce::WebInputStream stream (url, false);
    stream.withExtraHeaders ("User-Agent: " + userAgent() + "\r\nAccept: */*")
          .withConnectionTimeout (12000)
          .withNumRedirectsToFollow (3);

    {
        const juce::ScopedLock sl (streamLock);
        activeStream = &stream;
    }

    juce::MemoryBlock body;
    bool ok = ! threadShouldExit() && stream.connect (nullptr) && stream.getStatusCode() == 200;
    if (ok)
    {
        char buffer[16384];
        while (! stream.isExhausted() && ! threadShouldExit())
        {
            const int n = stream.read (buffer, sizeof (buffer));
            if (n <= 0)
                break;
            body.append (buffer, static_cast<size_t> (n));
            if (body.getSize() > static_cast<size_t> (kMaxBodyBytes))
            {
                ok = false;
                break;
            }
        }
    }

    const int statusCode = stream.getStatusCode();
    {
        const juce::ScopedLock sl (streamLock);
        activeStream = nullptr;
    }

    if (threadShouldExit())
        return false;

    const auto text = body.toString();
    if (ok && loadedSource == index && parse (s, text))
    {
        const auto f = cacheFile (s);
        f.getParentDirectory().createDirectory();
        f.replaceWithText (text);

        origin = "live";
        fetchedAtUtc = now;
        dirty = true;
        failures[static_cast<size_t> (index)] = 0;
        nextFetchUtc[static_cast<size_t> (index)] = now + s.refreshMinutes * 60.0;

        const juce::ScopedLock sl (infoLock);
        status.state = "live";
        status.message = juce::String (s.provider) + " " + s.station;
        status.lastSuccessUtc = now;
        status.nextFetchUtc = nextFetchUtc[static_cast<size_t> (index)];
        return true;
    }

    // Back off: 1, 2, 4 … 30 minutes. Keep playing what we have.
    auto& fails = failures[static_cast<size_t> (index)];
    fails = std::min (fails + 1, 6);
    nextFetchUtc[static_cast<size_t> (index)] = now + std::min (30.0, std::pow (2.0, fails - 1)) * 60.0;

    const juce::ScopedLock sl (infoLock);
    status.state = "offline";
    status.message = "Offline (" + (statusCode > 0 ? "HTTP " + juce::String (statusCode) : juce::String ("no connection"))
                   + ") " + juce::String::fromUTF8 ("· playing ") + origin + " data";
    status.nextFetchUtc = nextFetchUtc[static_cast<size_t> (index)];
    return false;
}

void WorldFeed::run()
{
    while (! threadShouldExit())
    {
        // Local data first (instant), the network second.
        syncWithParameters (true);
        if (dirty)
            buildAndPublish();

        if (networkEnabled && ! threadShouldExit())
        {
            const auto idx = static_cast<size_t> (loadedSource);
            const bool due = nowUtc() >= nextFetchUtc[idx];
            if (refreshRequested.exchange (false) || due)
                if (fetch (loadedSource) && dirty)
                    buildAndPublish();
        }

        if (threadShouldExit())
            break;

        wait (dirty ? 30 : 120);
    }
}

} // namespace worldtable::data
