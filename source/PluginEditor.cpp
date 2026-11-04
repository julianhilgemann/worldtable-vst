#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <WorldtableUIData.h>

namespace worldtable
{

namespace
{
juce::String mimeFor (const juce::String& path)
{
    const auto ext = path.fromLastOccurrenceOf (".", false, false).toLowerCase();
    if (ext == "html") return "text/html";
    if (ext == "js")   return "text/javascript";
    if (ext == "css")  return "text/css";
    if (ext == "svg")  return "image/svg+xml";
    if (ext == "png")  return "image/png";
    if (ext == "json") return "application/json";
    return "application/octet-stream";
}

std::vector<std::byte> toBytes (const void* data, size_t size)
{
    const auto* p = static_cast<const std::byte*> (data);
    return { p, p + size };
}

double round3 (float v) { return std::round (static_cast<double> (v) * 1000.0) / 1000.0; }

juce::var floatArray (const std::vector<float>& v)
{
    juce::Array<juce::var> a;
    a.ensureStorageAllocated (static_cast<int> (v.size()));
    for (auto x : v)
        a.add (round3 (x));
    return a;
}
} // namespace

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (p), wtProcessor (p),
      history (static_cast<size_t> (kFftSize), 0.0f),
      fftData (static_cast<size_t> (2 * kFftSize), 0.0f),
      window (static_cast<size_t> (kFftSize), 0.0f)
{
    for (int i = 0; i < kFftSize; ++i)
        window[static_cast<size_t> (i)] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * static_cast<float> (i) / kFftSize);
    bands.fill (-100.0f);

    auto options = juce::WebBrowserComponent::Options {}
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2 {}
                                     .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory))
                                     .withStatusBarDisabled())
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([this] (const juce::String& url) { return getResource (url); })
        .withNativeFunction ("refreshWorld", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 wtProcessor.getWorldFeed().requestRefresh();
                                 complete (true);
                             })
        .withNativeFunction ("requestTable", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 tableRequested = true;
                                 complete (true);
                             })
        .withNativeFunction ("uiReady", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 // The page has loaded and registered its listeners: bind the
                                 // parameters (their initial update now reaches a live page) and
                                 // start streaming.
                                 complete (true);
                                 juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PluginEditor> (this)]
                                 {
                                     if (safe != nullptr)
                                         safe->onPageReady();
                                 });
                             });

    for (auto* param : wtProcessor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
        {
            relays.push_back (std::make_unique<juce::WebSliderRelay> (ranged->getParameterID()));
            options = options.withOptionsFrom (*relays.back());
        }

    browser = std::make_unique<juce::WebBrowserComponent> (options);
    addAndMakeVisible (*browser);

    browser->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable (true, true);
    setResizeLimits (840, 560, 2400, 1600);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (static_cast<double> (kBaseWidth) / kBaseHeight);
    setSize (kBaseWidth, kBaseHeight);

    startTimerHz (30);
}

void PluginEditor::onPageReady()
{
    if (attachments.empty())
    {
        // Relays were created in parameter order; relay name == parameter ID.
        auto& apvts = wtProcessor.getValueTreeState();
        size_t i = 0;
        for (auto* param : wtProcessor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                attachments.push_back (std::make_unique<juce::WebSliderParameterAttachment> (*ranged, *relays[i++], apvts.undoManager));
    }
    else
    {
        for (auto& a : attachments)   // page reloaded: resend everything
            a->sendInitialUpdate();
    }

    tableRequested = true;
    pageReady = true;
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    attachments.clear();
    browser.reset();
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0a1422));
}

void PluginEditor::resized()
{
    browser->setBounds (getLocalBounds());
}

std::optional<juce::WebBrowserComponent::Resource> PluginEditor::getResource (const juce::String& url) const
{
    // JUCE passes the path ("/", "/style.css"); accept a full root URL too.
    auto path = url;
    if (const auto& root = juce::WebBrowserComponent::getResourceProviderRoot(); path.startsWith (root))
        path = path.substring (root.length());
    path = path.upToFirstOccurrenceOf ("?", false, false).trimCharactersAtStart ("/");
    if (path.isEmpty())
        path = "index.html";

    // Development: serve straight from disk so the UI can be edited without rebuilding.
    if (const auto dir = juce::SystemStats::getEnvironmentVariable ("WORLDTABLE_UI_DIR", {}); dir.isNotEmpty())
    {
        const auto f = juce::File (dir).getChildFile (path);
        juce::MemoryBlock mb;
        if (f.existsAsFile() && f.loadFileAsData (mb))
            return juce::WebBrowserComponent::Resource { toBytes (mb.getData(), mb.getSize()), mimeFor (path) };
    }

    const auto filename = path.fromLastOccurrenceOf ("/", false, false);
    for (int i = 0; i < WorldtableUIData::namedResourceListSize; ++i)
    {
        const auto* name = WorldtableUIData::namedResourceList[i];
        if (filename == WorldtableUIData::getNamedResourceOriginalFilename (name))
        {
            int size = 0;
            if (const auto* data = WorldtableUIData::getNamedResource (name, size))
                return juce::WebBrowserComponent::Resource { toBytes (data, static_cast<size_t> (size)), mimeFor (path) };
        }
    }
    return std::nullopt;
}

//==============================================================================
void PluginEditor::updateAnalyzer()
{
    auto& scope = wtProcessor.getEngine().getScope();
    std::array<float, 2048> chunk {};
    for (int n; (n = scope.pull (chunk.data(), static_cast<int> (chunk.size()))) > 0;)
        for (int i = 0; i < n; ++i)
        {
            history[static_cast<size_t> (historyPos)] = chunk[static_cast<size_t> (i)];
            historyPos = (historyPos + 1) % kFftSize;
        }

    for (int i = 0; i < kFftSize; ++i)
        fftData[static_cast<size_t> (i)] = history[static_cast<size_t> ((historyPos + i) % kFftSize)] * window[static_cast<size_t> (i)];
    std::fill (fftData.begin() + kFftSize, fftData.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    const double sr = wtProcessor.getSampleRate() > 0 ? wtProcessor.getSampleRate() : 48000.0;
    const double binHz = sr / kFftSize;
    const float norm = 4.0f / kFftSize;   // Hann window coherent gain, single-sided

    for (int b = 0; b < kBands; ++b)
    {
        const double f0 = 20.0 * std::pow (1000.0, static_cast<double> (b) / kBands);
        const double f1 = 20.0 * std::pow (1000.0, static_cast<double> (b + 1) / kBands);
        const int i0 = juce::jlimit (1, kFftSize / 2 - 1, static_cast<int> (f0 / binHz));
        const int i1 = juce::jlimit (i0 + 1, kFftSize / 2, static_cast<int> (std::ceil (f1 / binHz)));
        float mag = 0.0f;
        for (int i = i0; i < i1; ++i)
            mag = std::max (mag, fftData[static_cast<size_t> (i)]);
        const float db = juce::Decibels::gainToDecibels (mag * norm, -100.0f);
        auto& band = bands[static_cast<size_t> (b)];
        band = db > band ? db : band + 0.18f * (db - band);   // fast attack, slow fall
    }
}

juce::var PluginEditor::makeTelemetry()
{
    auto& t = wtProcessor.getEngine().getTelemetry();

    const float pl = t.peakL.exchange (0.0f);
    const float pr = t.peakR.exchange (0.0f);
    meterL = std::max (pl, meterL * 0.86f);
    meterR = std::max (pr, meterR * 0.86f);

    auto* o = new juce::DynamicObject();
    o->setProperty ("pos", round3 (t.position.load()));
    o->setProperty ("ampEnv", round3 (t.ampEnv.load()));
    o->setProperty ("fltEnv", round3 (t.filterEnv.load()));
    o->setProperty ("cutoff", std::round (t.cutoffHz.load()));
    o->setProperty ("lfo1", round3 (t.lfo1.load()));
    o->setProperty ("lfo2", round3 (t.lfo2.load()));
    o->setProperty ("lfo1Phase", round3 (t.lfo1Phase.load()));
    o->setProperty ("lfo2Phase", round3 (t.lfo2Phase.load()));
    o->setProperty ("peakL", round3 (meterL));
    o->setProperty ("peakR", round3 (meterR));
    o->setProperty ("gr", round3 (t.gainReduction.exchange (1.0f)));
    o->setProperty ("verb", round3 (t.reverbLevel.load()));
    o->setProperty ("fade", round3 (t.tableFade.load()));
    o->setProperty ("step", t.euclidStep.load());
    o->setProperty ("hits", static_cast<int> (t.euclidHits.load()));
    o->setProperty ("voices", t.activeVoices.load());
    o->setProperty ("bpm", std::round (t.bpm.load() * 10.0) / 10.0);

    juce::Array<juce::var> notes;
    for (int w = 0; w < 4; ++w)
    {
        const auto bits = t.noteMask[static_cast<size_t> (w)].load();
        for (int b = 0; b < 32; ++b)
            if ((bits >> b) & 1u)
                notes.add (w * 32 + b);
    }
    o->setProperty ("notes", notes);

    juce::Array<juce::var> spec;
    for (auto b : bands)
        spec.add (std::round (b * 10.0f) / 10.0);
    o->setProperty ("spectrum", spec);

    return juce::var (o);
}

juce::var PluginEditor::makeTableMessage() const
{
    const auto info = wtProcessor.getWorldFeed().getTableInfo();
    if (info == nullptr)
        return {};

    const auto& pv = info->preview;
    auto* o = new juce::DynamicObject();
    o->setProperty ("generation", static_cast<juce::int64> (info->generation));
    o->setProperty ("source", info->sourceIndex);
    o->setProperty ("station", info->station);
    o->setProperty ("place", info->place);
    o->setProperty ("provider", info->provider);
    o->setProperty ("origin", info->origin);
    o->setProperty ("fetchedAt", info->fetchedAtUtc);
    o->setProperty ("frames", pv.numFrames);
    o->setProperty ("wavePoints", transform::TablePreview::kWavePoints);
    o->setProperty ("harmonicCount", transform::TablePreview::kHarmonics);
    o->setProperty ("rawPoints", transform::TablePreview::kRawPoints);
    o->setProperty ("wave", floatArray (pv.wave));
    o->setProperty ("harmonics", floatArray (pv.harmonics));
    o->setProperty ("raw", floatArray (pv.raw));
    o->setProperty ("rawMin", pv.rawMin);
    o->setProperty ("rawMax", pv.rawMax);
    o->setProperty ("rawUnit", pv.rawUnit);
    o->setProperty ("rawAxis", pv.rawAxis);
    o->setProperty ("isSpectrum", pv.isSpectrum);

    juce::Array<juce::var> times;
    for (auto t : pv.frameTimesUtc)
        times.add (t);
    o->setProperty ("times", times);
    return juce::var (o);
}

juce::var PluginEditor::makeStatus() const
{
    const auto s = wtProcessor.getWorldFeed().getStatus();
    auto* o = new juce::DynamicObject();
    o->setProperty ("state", s.state);
    o->setProperty ("message", s.message);
    o->setProperty ("lastSuccess", s.lastSuccessUtc);
    o->setProperty ("nextFetch", s.nextFetchUtc);
    o->setProperty ("now", static_cast<double> (juce::Time::currentTimeMillis()) / 1000.0);
    return juce::var (o);
}

void PluginEditor::timerCallback()
{
    if (! pageReady)
        return;   // nothing listens yet (the WebView may still show about:blank)

    updateAnalyzer();
    browser->emitEventIfBrowserIsVisible ("telemetry", makeTelemetry());

    if (const auto info = wtProcessor.getWorldFeed().getTableInfo();
        info != nullptr && (info->generation != sentGeneration || tableRequested.exchange (false)))
    {
        sentGeneration = info->generation;
        browser->emitEventIfBrowserIsVisible ("table", makeTableMessage());
        statusCountdown = 0;
    }

    if (--statusCountdown <= 0)
    {
        browser->emitEventIfBrowserIsVisible ("status", makeStatus());
        statusCountdown = 15;
    }
}

} // namespace worldtable
