// UI integration check: opens the real editor (WebView) in a window, then asks
// the page over evaluateJavascript whether the JUCE bridge works end to end.
//
//   WorldtableUICheck            → prints a JSON report, exit code 0 when all checks pass
//   WorldtableUICheck --soak     → afterwards keeps the editor running for 30 s with audio and
//                                  the dome drawing, and prints "soak", so the web view's
//                                  processes can be sampled from outside (T6.4)
//   WorldtableUICheck --eval JS  → afterwards evaluates JS in the real editor (WebKit) and prints
//                                  the result, for layout probes the browser mock can't answer
//                                  (with --soak too: the soak runs after it, for a screenshot,
//                                  and the probe runs again at its end)
//
// Checks: backend attached (not the browser mock), telemetry is streaming, parameters
// sync C++ → JS and JS → C++, every parameter has exactly one control, the header shows the
// replayed storm (T2.4), the chord readout shows the sounding chord (T3.6), the dome draws
// the plug-in's sky with WebGL and the magnetogram got the day's chart (T6.1).

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
    bool soak = false;
    juce::String probe;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--soak")
            soak = true;
        else if (a == "--eval" && i + 1 < argc)
            probe = argv[++i];
    }

    worldtable::PluginProcessor processor (false);
    processor.setPlayConfigDetails (0, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    // Feed a little audio so telemetry and the analyzer have something to show.
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 400; ++i)
        processor.processBlock (buffer, midi);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    juce::DocumentWindow window ("Worldtable UI check", juce::Colours::black, 0);
    window.setUsingNativeTitleBar (true);
    window.setContentNonOwned (editor.get(), true);
    window.setTopLeftPosition (40, 40);
    window.setVisible (true);
    // WebKit throttles rendering of occluded windows (and the page pauses the dome while it is
    // hidden, spec/06): the check's window must be in front for the dome to draw.
    juce::Process::makeForegroundProcess();
    window.toFront (true);

    auto* browser = findBrowser (*editor);
    if (browser == nullptr)
    {
        std::cerr << "no WebBrowserComponent in editor\n";
        return 1;
    }

    // Wait for the page to load and the first telemetry to arrive.
    juce::String ready;
    for (int i = 0; i < 60; ++i)
    {
        for (int k = 0; k < 4; ++k)
        {
            processor.processBlock (buffer, midi);
            pump (25);
        }
        ready = evalSync (*browser, "!!(window.__wtState && window.__wtState.tel.spectrum"
                                    " && window.__wtState.tel.chord)");
        if (ready == "true")
            break;
    }

    const auto state = evalSync (*browser,
        "JSON.stringify({ mock: WT.isMock, bands: (__wtState.tel.spectrum || []).length,"
        " chord: __wtState.tel.chord && __wtState.tel.chord.label,"
        " roman: __wtState.tel.chord && __wtState.tel.chord.roman,"
        " voices: __wtState.tel.chord && __wtState.tel.chord.voices,"
        " readout: document.getElementById('chord-text').textContent,"
        " progression: WT.get('progression'), knobs: document.querySelectorAll('.knob').length,"
        " segs: document.querySelectorAll('.seg').length,"
        " unbound: Object.keys(WT.META).filter(function (id) {"
        "   return document.querySelectorAll('[data-param=\"' + id + '\"]').length !== 1; }) })");

    // C++ → JS
    auto* revMix = dynamic_cast<juce::RangedAudioParameter*> (processor.getValueTreeState().getParameter ("rev_mix"));
    revMix->setValueNotifyingHost (revMix->convertTo0to1 (0.77f));
    pump (300);
    const auto jsRevMix = evalSync (*browser, "WT.get('rev_mix')");

    // JS → C++
    evalSync (*browser, "WT.begin('master'); WT.set('master', -12); WT.end('master');"
                        " WT.commit('mood', 2); true");
    pump (300);
    const float cppMaster = processor.getValueTreeState().getRawParameterValue ("master")->load();
    const float cppMood = processor.getValueTreeState().getRawParameterValue ("mood")->load();

    // The dome (T6.1): WebGL, drawing frames from the plug-in's sky; the day chart arrived.
    for (int k = 0; k < 40; ++k)
    {
        processor.processBlock (buffer, midi);
        pump (25);
    }
    const auto dome = evalSync (*browser,
        "JSON.stringify({ mode: __wtState.domeMode, frames: __wtState.frames,"
        " nodes: __wtState.tel.dome ? __wtState.tel.dome.n : 0,"
        " day: __wtState.day ? __wtState.day.day : '', minutes: __wtState.day ? __wtState.day.phase.length : 0,"
        " station: __wtState.day && __wtState.day.station ? __wtState.day.station.code : '' })");
    auto parsedDome = juce::JSON::parse (dome);

    // The Sky Model (T2.4) and the chord readout (T3.6).
    pump (600);
    const auto skyState = evalSync (*browser,
        "JSON.stringify({ date: document.getElementById('ro-date').textContent,"
        " ut: document.getElementById('ro-ut').textContent,"
        " day: __wtState.tel.sky && __wtState.tel.sky.day,"
        " chord: document.getElementById('chord-text').textContent })");
    auto parsedSky = juce::JSON::parse (skyState);

    auto parsed = juce::JSON::parse (state);
    const bool ok = ready == "true"
                 && parsedSky["date"].toString().startsWith ("10 May")
                 && parsedSky["ut"].toString().endsWith ("UT")
                 && parsedSky["day"].toString() == "2024-05-10"
                 && parsedSky["chord"].toString().isNotEmpty()
                 && parsedSky["chord"].toString() != "-"
                 && parsedDome["mode"].toString() == "webgl"
                 && static_cast<int> (parsedDome["frames"]) > 0
                 && static_cast<int> (parsedDome["nodes"]) > 0
                 && parsedDome["day"].toString() == "2024-05-10"
                 && static_cast<int> (parsedDome["minutes"]) > 1000
                 && parsedDome["station"].toString() == "CMO"
                 && ! static_cast<bool> (parsed["mock"])
                 && static_cast<int> (parsed["bands"]) > 0
                 && parsed["chord"].toString().isNotEmpty()
                 && static_cast<int> (parsed["voices"]) > 0
                 && static_cast<int> (parsed["knobs"]) >= 40
                 && static_cast<int> (parsed["segs"]) >= 4
                 && parsed["unbound"].isArray() && parsed["unbound"].size() == 0
                 && std::abs (jsRevMix.getFloatValue() - 0.77f) < 0.001f
                 && std::abs (cppMaster - (-12.0f)) < 0.001f
                 && std::abs (cppMood - 2.0f) < 0.001f;

    std::cout << "ready:      " << ready << "\n"
              << "page state: " << state << "\n"
              << "C++→JS rev_mix 0.77 → JS sees " << jsRevMix << "\n"
              << "JS→C++ master −12 → C++ sees " << cppMaster << ", mood → " << cppMood << "\n"
              << "dome:       " << dome << "\n"
              << "sky/chord:  " << skyState << "\n"
              << (ok ? "UI CHECK PASSED" : "UI CHECK FAILED") << std::endl;

    if (probe.isNotEmpty())
    {
        pump (500);
        std::cout << "eval:       " << evalSync (*browser, probe, 20000) << std::endl;
    }

    if (soak)
    {
        std::cout << "soak" << std::endl;
        const auto end = juce::Time::getMillisecondCounter() + 30000;
        while (juce::Time::getMillisecondCounter() < end)
        {
            processor.processBlock (buffer, midi);   // 512 samples ≈ 10.7 ms: roughly real time
            pump (10);
        }
        std::cout << "soak done, frames " << evalSync (*browser, "__wtState.frames") << std::endl;
        if (probe.isNotEmpty())
            std::cout << "eval after: " << evalSync (*browser, probe, 20000) << std::endl;
    }

    window.setVisible (false);
    window.clearContentComponent();
    editor.reset();
    processor.releaseResources();
    return ok ? 0 : 1;
}
