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
    if (ext == "woff2") return "font/woff2";
    if (ext == "woff") return "font/woff";
    if (ext == "ttf")  return "font/ttf";
    return "application/octet-stream";
}

std::vector<std::byte> toBytes (const void* data, size_t size)
{
    const auto* p = static_cast<const std::byte*> (data);
    return { p, p + size };
}

double round3 (float v) { return std::round (static_cast<double> (v) * 1000.0) / 1000.0; }

juce::var floats (const std::vector<float>& values, double scale)
{
    juce::Array<juce::var> a;
    a.ensureStorageAllocated (static_cast<int> (values.size()));
    for (auto v : values)
        a.add (std::round (static_cast<double> (v) * scale) / scale);
    return a;
}

constexpr std::array<const char*, 4> kPhaseNames { "quiet", "growth", "onset", "recovery" };
constexpr std::array<const char*, 3> kOnsetKindNames { "loaded", "forced", "pseudo" };

juce::PropertiesFile::Options settingsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Worldtable";
    o.filenameSuffix = ".settings";
    o.folderName = "Worldtable";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    o.millisecondsBeforeSaving = -1;   // UiSettings saves at once, explicitly
    return o;
}

/** The usable area of the display the editor is about to open on: the one under the pointer (the
    person just clicked the plug-in open there), else the main one. */
juce::Rectangle<int> openingDisplayArea()
{
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* d = displays.getDisplayForPoint (juce::Desktop::getMousePosition().toFloat());
    if (d == nullptr)
        d = displays.getPrimaryDisplay();
    return d != nullptr ? d->userBounds.toNearestIntEdges() : juce::Rectangle<int> { 0, 0, 1920, 1080 };
}
} // namespace

//==============================================================================
UiSettings::UiSettings (juce::File settingsFile) : file (std::move (settingsFile)) {}

juce::File UiSettings::defaultFile()
{
    if (const auto path = juce::SystemStats::getEnvironmentVariable ("WORLDTABLE_SETTINGS_FILE", {}); path.isNotEmpty())
        return juce::File::getCurrentWorkingDirectory().getChildFile (path);
    return settingsOptions().getDefaultFile();
}

int UiSettings::size() const
{
    const juce::PropertiesFile props (file, settingsOptions());
    const int v = props.getIntValue ("uiSize", -1);
    return v >= 0 && v < kNumSizes ? v : -1;
}

void UiSettings::setSize (int index)
{
    juce::PropertiesFile props (file, settingsOptions());
    props.setValue ("uiSize", juce::jlimit (0, kNumSizes - 1, index));
    props.saveIfNeeded();
}

bool UiSettings::tourSeen() const
{
    const juce::PropertiesFile props (file, settingsOptions());
    return props.getBoolValue ("tourSeen", false);
}

void UiSettings::setTourSeen (bool seen)
{
    juce::PropertiesFile props (file, settingsOptions());
    props.setValue ("tourSeen", seen);
    props.saveIfNeeded();
}

juce::Point<int> UiSettings::sizeOf (int index)
{
    switch (index)
    {
        case kSmall: return { 1148, 765 };    // 85 %
        case kLarge: return { 1688, 1125 };   // 125 %
        default:     return { PluginEditor::kBaseWidth, PluginEditor::kBaseHeight };
    }
}

int UiSettings::openingSize (int saved, juce::Rectangle<int> usableArea)
{
    if (saved >= 0 && saved < kNumSizes)
        return saved;
    const auto medium = sizeOf (kMedium);
    return usableArea.getWidth() < medium.x + 80 || usableArea.getHeight() < medium.y + 80 ? kSmall : kMedium;
}

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (p), wtProcessor (p)
{
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
                             })
        .withNativeFunction ("seek", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 // T6.6: a click on the day strip jumps the Journey there (UTC
                                 // seconds; T6.10: a drag scrubs, at most 5 seeks a second).
                                 // Applied by the audio thread at its next block.
                                 if (! args.isEmpty())
                                     if (const double utc = args[0]; std::isfinite (utc))
                                         wtProcessor.getSky().seek (utc);
                                 complete (true);
                             })
        // T6.13: the window's size (Small, Medium, Large) and the welcome tour, kept per computer
        .withNativeFunction ("getUiSettings", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 complete (makeUiSettings());
                             })
        .withNativeFunction ("setUiSize", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 const int index = args.isEmpty() ? -1 : static_cast<int> (args[0]);
                                 const bool valid = index >= 0 && index < UiSettings::kNumSizes;
                                 complete (valid);
                                 if (! valid)
                                     return;
                                 // resized after the page's call has returned, not from inside it
                                 juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PluginEditor> (this), index]
                                 {
                                     if (safe != nullptr)
                                         safe->applyUiSize (index);
                                 });
                             })
        .withNativeFunction ("setTourSeen", [this] (const juce::Array<juce::var>&, auto complete)
                             {
                                 settings.setTourSeen (true);
                                 complete (true);
                             })
        .withNativeFunction ("journeySet", [this] (const juce::Array<juce::var>& args, auto complete)
                             {
                                 // T6.9: the transport. { frozen?, speed?, loop?, day? }; answers
                                 // with the Journey as it now is.
                                 JourneyChange change;
                                 if (! args.isEmpty() && args[0].isObject())
                                 {
                                     const auto& a = args[0];
                                     if (a.hasProperty ("frozen")) change.frozen = static_cast<bool> (a["frozen"]);
                                     if (a.hasProperty ("speed"))  change.speed = static_cast<double> (a["speed"]);
                                     if (a.hasProperty ("loop"))   change.loop = static_cast<bool> (a["loop"]);
                                     if (a.hasProperty ("day"))    change.day = a["day"].toString();
                                 }
                                 wtProcessor.setJourney (change);
                                 complete (makeJourneyTelemetry());
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

    // T6.13: three fixed sizes instead of a free resize; the page scales itself to the window
    setResizable (false, false);
    uiSize = UiSettings::openingSize (settings.size(), openingDisplayArea());
    const auto size = UiSettings::sizeOf (uiSize);
    setSize (size.x, size.y);

    startTimerHz (30);
}

void PluginEditor::applyUiSize (int index)
{
    uiSize = juce::jlimit (0, UiSettings::kNumSizes - 1, index);
    settings.setSize (uiSize);
    const auto size = UiSettings::sizeOf (uiSize);
    setSize (size.x, size.y);
}

juce::var PluginEditor::makeUiSettings() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("size", uiSize);
    o->setProperty ("tourSeen", settings.tourSeen());
    o->setProperty ("version", juce::String (WORLDTABLE_VERSION));
    if (wtProcessor.wrapperType != juce::AudioProcessor::wrapperType_Undefined)
        o->setProperty ("wrapper", juce::String (juce::AudioProcessor::getWrapperTypeDescription (wtProcessor.wrapperType)));
    return juce::var (o);
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

    daySent = 0;   // the page needs the day chart again

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
PluginEditor::Analyzer::Analyzer()
    : history (static_cast<size_t> (kFftSize), 0.0f),
      fftData (static_cast<size_t> (2 * kFftSize), 0.0f),
      window (static_cast<size_t> (kFftSize), 0.0f)
{
    for (int i = 0; i < kFftSize; ++i)
        window[static_cast<size_t> (i)] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * static_cast<float> (i) / kFftSize);
    bands.fill (-100.0f);
}

void PluginEditor::Analyzer::update (dsp::ScopeFifo& scope, double sr)
{
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

juce::var PluginEditor::Analyzer::toVar() const
{
    juce::Array<juce::var> spec;
    spec.ensureStorageAllocated (kBands);
    for (auto b : bands)
        spec.add (std::round (b * 10.0f) / 10.0f);
    return spec;
}

void PluginEditor::updateAnalyzer()
{
    auto& instrument = wtProcessor.getInstrument();
    const double sr = wtProcessor.getSampleRate() > 0 ? wtProcessor.getSampleRate() : 48000.0;
    outAnalyzer.update (instrument.getScope(), sr);
    inAnalyzer.update (instrument.getInputScope(), sr);   // T6.12: the sources before the tone stage
}

juce::var PluginEditor::makeSkyTelemetry()
{
    auto& runtime = wtProcessor.getSky();
    haveSkyFrame = runtime.readSnapshot (sky::SkyRuntime::Reader::ui, skyFrame) || haveSkyFrame;

    std::array<sky::SkyEvent, 64> events {};
    const int numEvents = runtime.readEvents (sky::SkyRuntime::Reader::ui, events);

    if (! haveSkyFrame)
        return {};

    const auto& s = skyFrame;
    const auto phaseName = kPhaseNames[static_cast<size_t> (s.phase) & 3u];

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
    o->setProperty ("gain", round3 (s.gain));
    o->setProperty ("coupling", round3 (s.couplingMvM));
    o->setProperty ("density", round3 (s.densityCm3));
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
        onset->setProperty ("kind", kOnsetKindNames[static_cast<size_t> (e.onsetKind) % kOnsetKindNames.size()]);
        onset->setProperty ("strength", round3 (e.strength));
        news.add (juce::var (onset));
    }
    o->setProperty ("newOnsets", news);
    return juce::var (o);
}

juce::var PluginEditor::makeDomeTelemetry() const
{
    // What the dome renderer needs each frame (spec/06, T6.1): the curtain nodes' footprints and
    // emission, the oval, the sun and the moon. The station frame comes once with the "day" event.
    if (! haveSkyFrame)
        return {};

    const auto& s = skyFrame;
    const int n = juce::jlimit (0, sky::SkySnapshot::kMaxNodes, static_cast<int> (s.nodeCount));
    juce::Array<juce::var> mlat, mlon, green, red, violet;
    for (auto* a : { &mlat, &mlon, &green, &red, &violet })
        a->ensureStorageAllocated (n);
    for (int i = 0; i < n; ++i)
    {
        const auto& node = s.nodes[static_cast<size_t> (i)];
        mlat.add (round3 (node.mlatDeg));
        mlon.add (round3 (node.mlonDeg));
        green.add (round3 (node.green));
        red.add (round3 (node.red));
        violet.add (round3 (node.violet));
    }

    auto* o = new juce::DynamicObject();
    o->setProperty ("n", n);
    o->setProperty ("mlat", mlat);
    o->setProperty ("mlon", mlon);
    o->setProperty ("green", green);
    o->setProperty ("red", red);
    o->setProperty ("violet", violet);
    o->setProperty ("eqLat", round3 (s.eqLatitudeDeg));
    o->setProperty ("poleLat", round3 (s.poleLatitudeDeg));
    o->setProperty ("dayShift", round3 (s.dayShiftDeg));
    o->setProperty ("stormRed", round3 (s.stormRed));
    o->setProperty ("pcPhase", round3 (s.pcPhase));
    o->setProperty ("recoveryPulse", round3 (s.recoveryPulse));
    o->setProperty ("sunAz", round3 (s.astronomy.sunAzDeg));
    o->setProperty ("sunEl", round3 (s.astronomy.sunElDeg));
    o->setProperty ("moonAz", round3 (s.astronomy.moonAzDeg));
    o->setProperty ("moonEl", round3 (s.astronomy.moonElDeg));
    o->setProperty ("moonIllum", round3 (s.astronomy.moonIllumination));
    return juce::var (o);
}

juce::var PluginEditor::makeDayEvent() const
{
    // The loaded day at a glance (the day strip: keogram and magnetometer, T6.10) and the station
    // the dome looks up from.
    const auto& c = wtProcessor.getSkyFeed().dayChart();
    const auto& ob = skyFrame.observer;

    auto* station = new juce::DynamicObject();
    const auto info = data::stationInfo (c.station);
    station->setProperty ("code", juce::String (info.code));
    station->setProperty ("name", juce::String::fromUTF8 (info.name));
    station->setProperty ("lat", round3 (ob.latDeg));
    station->setProperty ("lon", round3 (ob.lonDeg));
    station->setProperty ("elevKm", round3 (ob.elevKm));
    station->setProperty ("mlat", round3 (ob.mlatDeg));
    station->setProperty ("mlon", round3 (ob.mlonDeg));
    station->setProperty ("poleLat", round3 (ob.poleLatDeg));
    station->setProperty ("poleLon", round3 (ob.poleLonDeg));
    station->setProperty ("mzAz", round3 (ob.magZenithAzDeg));
    station->setProperty ("mzEl", round3 (ob.magZenithElDeg));

    juce::Array<juce::var> phases, onsets;
    phases.ensureStorageAllocated (c.size());
    for (auto p : c.phase)
        phases.add (static_cast<int> (p));
    for (const auto& e : c.onsets)
    {
        auto* onset = new juce::DynamicObject();
        onset->setProperty ("utc", e.utc);
        onset->setProperty ("kind", kOnsetKindNames[static_cast<size_t> (e.kind) % kOnsetKindNames.size()]);
        onset->setProperty ("strength", round3 (e.strength));
        onsets.add (juce::var (onset));
    }

    auto* o = new juce::DynamicObject();
    o->setProperty ("day", c.dayId);
    o->setProperty ("sequence", static_cast<juce::int64> (c.sequence));
    o->setProperty ("t0", c.startUtc);
    o->setProperty ("step", c.stepSeconds);
    o->setProperty ("station", juce::var (station));
    o->setProperty ("dh", floats (c.dhMeasuredNt, 10.0));
    o->setProperty ("dhModel", floats (c.dhModelNt, 10.0));
    o->setProperty ("coupling", floats (c.couplingMvM, 100.0));
    o->setProperty ("energy", floats (c.reservoir, 1000.0));
    o->setProperty ("threshold", floats (c.threshold, 1000.0));
    o->setProperty ("phase", phases);
    o->setProperty ("onsets", onsets);

    // T6.10: the keogram's inputs, per minute (2 decimals), and the oval's constants
    o->setProperty ("eqLat", floats (c.eqLatitudeDeg, 100.0));
    o->setProperty ("poleLat", floats (c.poleLatitudeDeg, 100.0));
    o->setProperty ("activity", floats (c.activity, 100.0));
    o->setProperty ("gain", floats (c.gain, 100.0));
    o->setProperty ("unloading", floats (c.unloading, 100.0));
    o->setProperty ("stormRed", floats (c.stormRed, 100.0));
    o->setProperty ("mlt", floats (c.mltHours, 100.0));
    o->setProperty ("kp", floats (c.kp, 100.0));
    o->setProperty ("stationMlat", std::round (c.stationMlatDeg * 100.0) / 100.0);
    o->setProperty ("dayShift", std::round (c.dayShiftDeg * 100.0) / 100.0);
    o->setProperty ("arcPosition", std::round (c.arcPosition * 100.0) / 100.0);

    // T6.9: what the transport offers: the bundled days and the speeds
    juce::Array<juce::var> days, speeds;
    for (const auto& d : data::kSkyDays)
    {
        auto* day = new juce::DynamicObject();
        day->setProperty ("id", juce::String (d.id));
        day->setProperty ("label", juce::String::fromUTF8 (d.label));
        days.add (juce::var (day));
    }
    for (const double s : JourneyState::kSpeeds)
        speeds.add (s);
    o->setProperty ("days", days);
    o->setProperty ("speeds", speeds);
    return juce::var (o);
}

juce::var PluginEditor::makeJourneyTelemetry() const
{
    // T6.9: the transport's state; `speed` is the running speed, also while frozen
    const auto j = wtProcessor.getJourney();
    auto* o = new juce::DynamicObject();
    o->setProperty ("day", j.day);
    o->setProperty ("frozen", j.frozen);
    o->setProperty ("speed", j.speed);
    o->setProperty ("loop", j.loop);
    o->setProperty ("ended", wtProcessor.journeyEnded());
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
    o->setProperty ("play", c.play);
    o->setProperty ("held", c.held);
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

    // the band edges the notes were voiced with (T3.8): floor, x1, x2, x3, ceiling; the blend
    juce::Array<juce::var> edges;
    for (const auto e : c.edges)
        edges.add (static_cast<int> (e));
    o->setProperty ("edges", edges);
    o->setProperty ("blend", round3 (c.blend));
    return juce::var (o);
}

juce::var PluginEditor::makePlayTelemetry()
{
    auto& instrument = wtProcessor.getInstrument();
    instrument.readPlay (playFrame);

    auto* o = new juce::DynamicObject();
    o->setProperty ("mix", round3 (instrument.getTelemetry().playMix.load()));
    juce::Array<juce::var> voices;
    for (int i = 0; i < std::min (playFrame.count, dsp::PlayState::kMax); ++i)
    {
        const auto k = static_cast<size_t> (i);
        voices.add (juce::Array<juce::var> { static_cast<int> (playFrame.notes[k]), round3 (playFrame.levels[k]),
                                             playFrame.held[k] != 0 });
    }
    o->setProperty ("voices", voices);
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
    o->setProperty ("breath", round3 (t.breath.load()));
    o->setProperty ("sky", makeSkyTelemetry());
    o->setProperty ("journey", makeJourneyTelemetry());
    o->setProperty ("dome", makeDomeTelemetry());
    o->setProperty ("chord", makeChordTelemetry());
    o->setProperty ("play", makePlayTelemetry());
    {
        auto* low = new juce::DynamicObject();
        low->setProperty ("note", t.lowNote.load());
        low->setProperty ("level", round3 (t.lowLevel.load()));
        o->setProperty ("low", juce::var (low));
    }
    o->setProperty ("eqCutHz", std::round (static_cast<double> (t.eqCutHz.load())));   // T3.9: 20000 = open
    o->setProperty ("sampleRate", wtProcessor.getSampleRate() > 0 ? wtProcessor.getSampleRate() : 48000.0);   // for the EQ's curve (T6.14) and the phaser's (T6.12)

    o->setProperty ("spectrum", outAnalyzer.toVar());
    // T6.12: the sources' spectrum before the tone stage (same bands) and the phaser's LFO phase,
    // for the phaser picture (which also reads sampleRate, for its exact response)
    o->setProperty ("spectrumIn", inAnalyzer.toVar());
    o->setProperty ("phaser", round3 (t.phaserPhase.load()));

    return juce::var (o);
}

void PluginEditor::timerCallback()
{
    if (! pageReady)
        return;   // nothing listens yet (the WebView may still show about:blank)

    updateAnalyzer();
    browser->emitEventIfBrowserIsVisible ("telemetry", makeTelemetry());

    // The day chart once per page load and per loaded day, after the first sky frame (it carries
    // the station frame).
    const auto daySequence = wtProcessor.getSkyFeed().dayChart().sequence;
    if (haveSkyFrame && daySequence != 0 && daySequence != daySent)
    {
        browser->emitEventIfBrowserIsVisible ("day", makeDayEvent());
        daySent = daySequence;
    }
}

} // namespace worldtable
