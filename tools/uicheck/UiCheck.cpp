// UI integration check: opens the real editor (WebView) in a window, then asks
// the page over evaluateJavascript whether the JUCE bridge works end to end.
//
//   WorldtableUICheck            → prints a JSON report, exit code 0 when all checks pass
//   WorldtableUICheck --soak     → afterwards holds each ambient level (0–3) for 12 s with
//                                  audio running and prints "soak level N", so the web view's
//                                  processes can be sampled from outside (TP.2)
//
// Checks: backend attached (not the browser mock), the world table arrived,
// telemetry is streaming, parameters sync C++ → JS and JS → C++, the Sky Model's
// readout shows the replayed storm (T2.4).

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <iostream>

namespace
{
juce::String evalSync (juce::WebBrowserComponent& browser, const juce::String& js, int timeoutMs = 3000)
{
    juce::String result = "<timeout>";
    bool done = false;
    browser.evaluateJavascript (js, [&] (juce::WebBrowserComponent::EvaluationResult r)
    {
        if (const auto* v = r.getResult())
            result = v->isString() ? v->toString() : juce::JSON::toString (*v, true);
        else if (const auto* e = r.getError())
            result = "<error: " + e->message + ">";
        done = true;
    });
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
    while (! done && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    return result;
}

juce::WebBrowserComponent* findBrowser (juce::Component& c)
{
    if (auto* b = dynamic_cast<juce::WebBrowserComponent*> (&c))
        return b;
    for (auto* child : c.getChildren())
        if (auto* b = findBrowser (*child))
            return b;
    return nullptr;
}

void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
    const bool soak = argc > 1 && juce::String (argv[1]) == "--soak";

    worldtable::PluginProcessor processor (false);
    processor.setPlayConfigDetails (0, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    // Feed a little audio so telemetry and the analyzer have something to show.
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 200; ++i)
        processor.processBlock (buffer, midi);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    juce::DocumentWindow window ("Worldtable UI check", juce::Colours::black, 0);
    window.setUsingNativeTitleBar (true);
    window.setContentNonOwned (editor.get(), true);
    window.setTopLeftPosition (40, 40);
    window.setVisible (true);
    if (soak)
    {
        // WebKit throttles rendering of occluded windows: the soak window must be in front.
        juce::Process::makeForegroundProcess();
        window.toFront (true);
    }

    auto* browser = findBrowser (*editor);
    if (browser == nullptr)
    {
        std::cerr << "no WebBrowserComponent in editor\n";
        return 1;
    }

    // Wait for the page to load and the first table/telemetry to arrive.
    juce::String ready;
    for (int i = 0; i < 60; ++i)
    {
        for (int k = 0; k < 4; ++k)
        {
            processor.processBlock (buffer, midi);
            pump (25);
        }
        ready = evalSync (*browser, "!!(window.__wtState && window.__wtState.table && window.__wtState.tel.spectrum)");
        if (ready == "true")
            break;
    }

    const auto state = evalSync (*browser,
        "JSON.stringify({ mock: WT.isMock, frames: __wtState.table && __wtState.table.frames,"
        " station: __wtState.table && __wtState.table.station, origin: __wtState.table && __wtState.table.origin,"
        " bands: (__wtState.tel.spectrum || []).length, voices: __wtState.tel.voices,"
        " cutoff: WT.get('flt_cutoff'), mode: WT.get('trig_mode'), knobs: document.querySelectorAll('.knob').length })");

    // C++ → JS
    auto* cutoff = dynamic_cast<juce::RangedAudioParameter*> (processor.getValueTreeState().getParameter ("flt_cutoff"));
    cutoff->setValueNotifyingHost (cutoff->convertTo0to1 (3210.0f));
    pump (300);
    const auto jsCutoff = evalSync (*browser, "WT.get('flt_cutoff')");

    // JS → C++
    evalSync (*browser, "WT.begin('rev_mix'); WT.set('rev_mix', 0.77); WT.end('rev_mix'); WT.commit('trig_mode', 1); true");
    pump (300);
    const float cppMix = processor.getValueTreeState().getRawParameterValue ("rev_mix")->load();
    const float cppMode = processor.getValueTreeState().getRawParameterValue ("trig_mode")->load();

    // Ambient background (TP.2): level 3 needs WebGL; without it the page falls back to level 1.
    const auto ambient = evalSync (*browser,
        "__wtAmbient.setLevel(3); JSON.stringify({ level: __wtAmbient.level,"
        " canvas: !!document.querySelector('#ambient canvas'), body: document.body.className })");
    pump (500);
    const auto ambientDrawn = evalSync (*browser, "__wtAmbient.smoothed.speed > 0");
    auto parsedAmbient = juce::JSON::parse (ambient);

    // The Sky Model (T2.4): the readout is visible and shows the storm day being replayed.
    pump (400);
    const auto skyState = evalSync (*browser,
        "JSON.stringify({ hidden: document.getElementById('sky').hidden,"
        " text: document.getElementById('sky-text').textContent,"
        " day: __wtState.tel.sky && __wtState.tel.sky.day })");
    auto parsedSky = juce::JSON::parse (skyState);

    auto parsed = juce::JSON::parse (state);
    const bool ok = ready == "true"
                 && ! static_cast<bool> (parsedSky["hidden"])
                 && parsedSky["text"].toString().startsWith ("10 May")
                 && parsedSky["day"].toString() == "2024-05-10"
                 && static_cast<int> (parsedAmbient["level"]) == 3
                 && static_cast<bool> (parsedAmbient["canvas"])
                 && ambientDrawn == "true"
                 && ! static_cast<bool> (parsed["mock"])
                 && static_cast<int> (parsed["frames"]) == 64
                 && static_cast<int> (parsed["bands"]) > 0
                 && std::abs (jsCutoff.getFloatValue() - 3210.0f) < 1.0f
                 && std::abs (cppMix - 0.77f) < 0.001f
                 && std::abs (cppMode - 1.0f) < 0.001f;

    std::cout << "ready:      " << ready << "\n"
              << "page state: " << state << "\n"
              << "C++→JS cutoff 3210 → JS sees " << jsCutoff << "\n"
              << "JS→C++ rev_mix 0.77 → C++ sees " << cppMix << ", trig_mode → " << cppMode << "\n"
              << "ambient:    " << ambient << "\n"
              << "sky:        " << skyState << "\n"
              << (ok ? "UI CHECK PASSED" : "UI CHECK FAILED") << std::endl;

    if (soak)
    {
        for (int level = 0; level <= 3; ++level)
        {
            evalSync (*browser, "__wtAmbient.setLevel(" + juce::String (level) + "); true");
            std::cout << "soak level " << level << std::endl;
            const auto end = juce::Time::getMillisecondCounter() + 12000;
            while (juce::Time::getMillisecondCounter() < end)
            {
                processor.processBlock (buffer, midi);   // 512 samples ≈ 10.7 ms: roughly real time
                pump (10);
            }
        }
        std::cout << "soak done" << std::endl;
    }

    window.setVisible (false);
    window.clearContentComponent();
    editor.reset();
    processor.releaseResources();
    return ok ? 0 : 1;
}
