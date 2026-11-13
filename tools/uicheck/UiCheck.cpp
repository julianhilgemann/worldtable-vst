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
//   WorldtableUICheck --at X,Y   → puts the window's top-left there (screen points), e.g. on a taller
//                                  display so a screencapture holds the whole editor
//
// Checks: backend attached (not the browser mock), telemetry is streaming, parameters
// sync C++ → JS and JS → C++, every parameter has exactly one control, the header shows the
// replayed storm (T2.4), the chord readout shows the sounding chord (T3.6), the dome draws
// the plug-in's sky with WebGL and the magnetogram got the day's chart (T6.1). T6.7 (spec/09 §5):
// every parameter's control has a hint, a role and a value text and can be set with the keyboard
// alone (and a typed value lands), every picture has a hint, red/green/violet stay on the bands in
// the DOM, and the macro rings follow the engine (Space 0 / 0.5 / 1 → reverb mix ×0.5 / ×1 / ×1.5).

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

// T6.7: runs in the real editor, synchronously (key events are dispatched in place), and restores
// every value it touches.
const char* const accessProbe = R"JS(JSON.stringify((function () {
  var bad = { hint: [], long: [], role: [], text: [], focus: [], keys: [], pictures: [], hues: [] };
  /** Focus the control (or its selected radio), press Up (Down at the top), see the value move. */
  var keyboard = function (id, el) {
    var target = el.matches('[tabindex], button') ? el : el.querySelector('[tabindex="0"]');
    if (target) target.focus();
    if (!target || !el.contains(document.activeElement)) return 'focus';
    var v0 = WT.get(id);
    var key = function (k) { document.activeElement.dispatchEvent(new KeyboardEvent('keydown', { key: k, bubbles: true, cancelable: true })); };
    key('ArrowUp');
    if (WT.get(id) === v0) key('ArrowDown');
    var moved = WT.get(id) !== v0;
    UI.commit(id, v0);
    return moved ? '' : 'keys';
  };
  var hidden = [];
  Object.keys(WT.META).forEach(function (id) {
    var el = document.querySelector('[data-param="' + id + '"]');
    if (!el) return;
    var h = el.getAttribute('data-hint') || '';
    if (!h) bad.hint.push(id); else if (h.length > 90) bad.long.push(id);
    if (['slider', 'switch', 'radiogroup'].indexOf(el.getAttribute('role')) < 0) bad.role.push(id);
    if (!el.getAttribute('aria-valuetext')) bad.text.push(id);
    var r = keyboard(id, el);
    if (r === 'focus' && el.closest('.play-stack')) hidden.push(id); else if (r) bad[r].push(id);
  });
  // the Play controls show (and take focus) in Play mode: switch it on, transitions held
  if (hidden.length) {
    var root = document.documentElement, shot = root.classList.contains('shot'), play0 = WT.get('play');
    root.classList.add('shot');
    UI.commit('play', 1);
    hidden.forEach(function (id) { var r = keyboard(id, document.querySelector('[data-param="' + id + '"]')); if (r) bad[r].push(id); });
    UI.commit('play', play0);
    if (!shot) root.classList.remove('shot');
  }
  // Enter on a knob opens a field; a typed value lands
  var rm = document.querySelector('[data-param="rev_mix"]'), rm0 = WT.get('rev_mix'), typed = -1;
  rm.focus();
  rm.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true, cancelable: true }));
  var f = rm.querySelector('input');
  if (f) { f.value = '50 %'; f.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true, cancelable: true })); typed = WT.get('rev_mix'); }
  UI.commit('rev_mix', rm0);
  rm.blur();
  [].forEach.call(document.querySelectorAll('#stage canvas'), function (c) { if (!c.closest('[data-hint]')) bad.pictures.push(c.id || 'canvas'); });
  // spec/09 rule 2 in the DOM (canvas pixels are reviewed in the code): the bands' knobs, the
  // Layers picture, the voicing view, the keyboard strip, the dome, the magnetogram, the brand mark
  var HUE = { red: '255, 107, 107', green: '92, 242, 160', violet: '143, 123, 255' };
  [].forEach.call(document.querySelectorAll('#stage *'), function (e) {
    if (e.closest('.k-red, .k-green, .k-violet, #dome-wrap, #mag, #cv-layers, #cv-voicing, #cv-keys, .brand')) return;
    var cs = getComputedStyle(e);
    var v = [cs.color, cs.backgroundColor, cs.borderTopColor, cs.borderBottomColor, cs.borderLeftColor, cs.borderRightColor,
             cs.fill, cs.stroke, cs.backgroundImage, cs.boxShadow, cs.outlineColor].join('|');
    for (var k in HUE) if (v.indexOf(HUE[k]) >= 0) bad.hues.push((e.id || e.getAttribute('data-param') || e.tagName) + ' ' + k);
  });
  // the Space macro's ring on Reverb mix, as a multiple of the knob's own value
  var base = WT.get('rev_mix'), s0 = WT.get('macro_space'), ring = [];
  [0, 0.5, 1].forEach(function (m) { UI.commit('macro_space', m); UI.updateRings(); var md = UI.knobs.rev_mix.mod; ring.push(md ? Math.round(md.now / base * 1000) / 1000 : -1); });
  UI.commit('macro_space', s0);
  UI.updateRings();
  bad.typed = typed;
  bad.spaceRing = ring;
  return bad;
})()))JS";
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
    bool soak = false;
    juce::String probe;
    juce::Point<int> at (40, 40);
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--soak")
            soak = true;
        else if (a == "--eval" && i + 1 < argc)
            probe = argv[++i];
        else if (a == "--at" && i + 1 < argc)
        {
            const juce::String xy (argv[++i]);
            at = { xy.upToFirstOccurrenceOf (",", false, false).getIntValue(),
                   xy.fromFirstOccurrenceOf (",", false, false).getIntValue() };
        }
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
    window.setTopLeftPosition (at.x, at.y);
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

    // T6.7: the keyboard, the hints, the colour rule, the macro rings
    const auto access = evalSync (*browser, juce::String::fromUTF8 (accessProbe), 20000);
    pump (600);   // let the restored values travel back to C++
    auto parsedAccess = juce::JSON::parse (access);
    bool accessOk = parsedAccess.isObject();
    for (const auto* list : { "hint", "long", "role", "text", "focus", "keys", "pictures", "hues" })
        accessOk = accessOk && parsedAccess[list].isArray() && parsedAccess[list].size() == 0;
    accessOk = accessOk && std::abs (static_cast<double> (parsedAccess["typed"]) - 0.5) < 1.0e-3;
    const auto* ring = parsedAccess["spaceRing"].getArray();
    accessOk = accessOk && ring != nullptr && ring->size() == 3
            && std::abs (static_cast<double> ((*ring)[0]) - 0.5) < 1.0e-3
            && std::abs (static_cast<double> ((*ring)[1]) - 1.0) < 1.0e-3
            && std::abs (static_cast<double> ((*ring)[2]) - 1.5) < 1.0e-3;

    auto parsed = juce::JSON::parse (state);
    const bool ok = ready == "true"
                 && accessOk
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
              << "keyboard, hints, colour, rings: " << access << "\n"
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
