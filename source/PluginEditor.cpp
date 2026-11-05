#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <WorldtableUIData.h>

// The web UI is served from memory through a resource provider. JUCE offers that on
// macOS, and on Windows only with the WebView2 backend, which is not wired into the
// build yet (it needs the WebView2 SDK). Without it the editor shows a placeholder;
// the processor and the sound are unaffected. Define WORLDTABLE_WEB_UI=0 to compile
// the placeholder path on macOS.
#ifndef WORLDTABLE_WEB_UI
 #define WORLDTABLE_WEB_UI JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
#endif

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

   #if WORLDTABLE_WEB_UI
    auto options = juce::WebBrowserComponent::Options {}
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2 {}
                                     .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory))
                                     .withStatusBarDisabled())
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([this] (const juce::String& url) { return getResource (url); })
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
   #endif

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

    if (browser == nullptr)   // no web UI on this platform yet (see WORLDTABLE_WEB_UI)
    {
        g.setColour (juce::Colour (0xffa9c4dd));
        g.setFont (juce::FontOptions (18.0f));
        g.drawFittedText ("Worldtable\n\nThe interface is not available on this platform yet.\n"
                          "The synth runs normally; its parameters are in your host's generic editor.",
                          getLocalBounds().reduced (40), juce::Justification::centred, 6);
    }
}

void PluginEditor::resized()
{
    if (browser != nullptr)
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
    auto& scope = wtProcessor.getInstrument().getScope();
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

juce::var PluginEditor::makeSkyTelemetry()
{
    auto& runtime = wtProcessor.getSky();
    haveSkyFrame = runtime.readSnapshot (sky::SkyRuntime::Reader::ui, skyFrame) || haveSkyFrame;

    std::array<sky::SkyEvent, 64> events {};
    const int numEvents = runtime.readEvents (sky::SkyRuntime::Reader::ui, events);

    if (! haveSkyFrame)
        return {};

    static constexpr std::array<const char*, 4> phaseNames { "quiet", "growth", "onset", "recovery" };
    static constexpr std::array<const char*, 3> kindNames { "loaded", "forced", "pseudo" };
    const auto& s = skyFrame;
    const auto phaseName = phaseNames[static_cast<size_t> (s.phase) & 3u];

    auto* o = new juce::DynamicObject();
    o->setProperty ("day", wtProcessor.getSkyFeed().dayId());
    o->setProperty ("utc", s.modelTimeUtc);
    o->setProperty ("speed", round3 (s.speed));
    o->setProperty ("phase", phaseName);
    o->setProperty ("progress", round3 (s.phaseProgress));
    o->setProperty ("kp", round3 (s.kp));
    o->setProperty ("bz", round3 (s.bzNt));
    o->setProperty ("wind", std::round (static_cast<double> (s.windSpeedKmS)));
    o->setProperty ("dh", std::round (static_cast<double> (s.dhMeasuredNt)));
    o->setProperty ("dhModel", std::round (static_cast<double> (s.dhModelNt)));
    o->setProperty ("energy", round3 (s.energy));
    o->setProperty ("activity", round3 (s.activity));
    o->setProperty ("unloading", round3 (s.unloading));
    o->setProperty ("mlt", round3 (s.mltHours));
    o->setProperty ("arcLat", round3 (s.ovalLatitudeDeg));
    o->setProperty ("onsets", static_cast<int> (s.onsetCount));
    o->setProperty ("whistlers", static_cast<int> (s.whistlerCount));

    juce::Array<juce::var> news;
    for (int i = 0; i < numEvents; ++i)
    {
        const auto& e = events[static_cast<size_t> (i)];
        if (e.type != sky::SkyEvent::Type::onset)
            continue;
        auto* onset = new juce::DynamicObject();
        onset->setProperty ("utc", e.modelTimeUtc);
        onset->setProperty ("kind", kindNames[static_cast<size_t> (e.onsetKind) % kindNames.size()]);
        onset->setProperty ("strength", round3 (e.strength));
        news.add (juce::var (onset));
    }
    o->setProperty ("newOnsets", news);
    return juce::var (o);
}

juce::var PluginEditor::makeChordTelemetry()
{
    haveChord = wtProcessor.getInstrument().readChord (chordFrame) || haveChord;
    if (! haveChord)
        return {};

    const auto& c = chordFrame;
    auto* o = new juce::DynamicObject();
    o->setProperty ("label", juce::String::fromUTF8 (c.label.data()));
    o->setProperty ("roman", juce::String::fromUTF8 (c.roman.data()));
    o->setProperty ("key", c.key);
    o->setProperty ("function", c.function);
    o->setProperty ("shape", c.shape);
    o->setProperty ("voices", c.voices);
    o->setProperty ("bank", c.bank);
    o->setProperty ("latched", c.latched);
    o->setProperty ("pending", c.pending);
    o->setProperty ("changes", static_cast<int> (c.changes));
    o->setProperty ("tick", static_cast<juce::int64> (c.tick));

    juce::Array<juce::var> layers;
    for (int l = 0; l < dsp::kNumLayers; ++l)
    {
        juce::Array<juce::var> notes;
        for (int i = 0; i < std::min (c.voices, dsp::ChordState::kMaxNotes); ++i)
            notes.add (static_cast<int> (c.notes[static_cast<size_t> (l)][static_cast<size_t> (i)]));
        layers.add (juce::var (notes));
    }
    o->setProperty ("notes", layers);
    return juce::var (o);
}

juce::var PluginEditor::makeTelemetry()
{
    auto& t = wtProcessor.getInstrument().getTelemetry();

    const float pl = t.peakL.exchange (0.0f);
    const float pr = t.peakR.exchange (0.0f);
    meterL = std::max (pl, meterL * 0.86f);
    meterR = std::max (pr, meterR * 0.86f);

    auto* o = new juce::DynamicObject();
    o->setProperty ("peakL", round3 (meterL));
    o->setProperty ("peakR", round3 (meterR));
    o->setProperty ("gr", round3 (t.gainReduction.exchange (1.0f)));
    o->setProperty ("autoGain", round3 (t.autoGain.load()));
    o->setProperty ("verb", round3 (t.reverbLevel.load()));
    o->setProperty ("sky", makeSkyTelemetry());
    o->setProperty ("chord", makeChordTelemetry());

    juce::Array<juce::var> spec;
    for (auto b : bands)
        spec.add (std::round (b * 10.0f) / 10.0f);
    o->setProperty ("spectrum", spec);

    return juce::var (o);
}

void PluginEditor::timerCallback()
{
    if (! pageReady)
        return;   // nothing listens yet (the WebView may still show about:blank)

    updateAnalyzer();
    browser->emitEventIfBrowserIsVisible ("telemetry", makeTelemetry());
}

} // namespace worldtable
