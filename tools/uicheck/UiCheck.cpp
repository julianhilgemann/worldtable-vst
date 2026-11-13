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
// the plug-in's sky with WebGL and the day strip got the day's chart (T6.1). T6.7 (spec/09 §5):
// every parameter's control has a hint, a role and a value text and can be set with the keyboard
// alone (and a typed value lands), every picture has a hint, red/green/violet stay on the bands in
// the DOM, and the macro rings follow the engine (Space 0 / 0.5 / 1 → reverb mix ×0.5 / ×1 / ×1.5).
// T6.9: the Journey transport's controls, journeySet and the day menu reach the processor.
// T6.10: the day strip is a slider with a hint and a value sentence, its keogram is built, its canvas
// is no larger than its box × dpr, an arrow key seeks 10 minutes on, and a pointer drag across it
// seeks (at its start and on release) and the sky lands there.
// T6.8 (spec/09 §3, §5): every module has its size in the layout, no canvas's backing store is wider
// than its box × the pixel ratio, and every More popover opens from its title by pointer, is next in
// the Tab order, and closes on Escape with the focus back on the title.
// T6.12: the effect pictures are controls: a drag in each sets its parameters (read back in C++), the
// telemetry carries the input spectrum and the phaser's phase, every proxy sits inside its picture,
// and the page's phaser response is dsp::Phaser::magnitude.
// T6.14: the EQ display draws, a dot dragged, a bell scrolled, a drive bar pulled, the sky's end
// dragged and a dot's keys set the right parameters, and the processor gets them.
// T6.11: a crossover dragged in the resonator display moves its parameter in the engine; the
// grips and levels have proxies.

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "dsp/fx/Phaser.h"

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
    // a picture's handle may name the keys that move it (T6.14: a dot's frequency is ← →, its width [ ])
    var keys = (el.getAttribute('data-keys') || 'ArrowUp,ArrowDown').split(',');
    key(keys[0]);
    if (WT.get(id) === v0) key(keys[1]);
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
    if (r === 'focus' && (el.closest('.play-stack') || el.closest('[hidden]'))) hidden.push(id); else if (r) bad[r].push(id);
  });
  // hidden controls are reached as a person reaches them: the Play controls in Play mode, a closed
  // popover's controls (T6.8's More popovers) through its button; transitions held
  if (hidden.length) {
    var root = document.documentElement, shot = root.classList.contains('shot'), play0 = WT.get('play');
    root.classList.add('shot');
    UI.commit('play', 1);
    hidden.forEach(function (id) {
      var el = document.querySelector('[data-param="' + id + '"]'), pop = el.closest('[hidden]');
      var btn = pop ? document.querySelector('[aria-controls="' + pop.id + '"]') : null;
      if (btn) btn.click();
      var r = keyboard(id, el);
      if (r) bad[r].push(id);
      if (btn && !pop.hidden) btn.click();
    });
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
  // resonator display and the band row's swatches (T6.11), the keyboard strip, the dome, the day
  // strip, the brand mark
  var HUE = { red: '255, 107, 107', green: '92, 242, 160', violet: '143, 123, 255' };
  [].forEach.call(document.querySelectorAll('#stage *'), function (e) {
    if (e.closest('.k-red, .k-green, .k-violet, #dome-wrap, #strip, #cv-res, .bn-sw, #cv-keys, .brand')) return;
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
// T6.10: the day strip, from the keyboard and the pointer (synthetic events, dispatched in place).
// Returns where the drag let go (`target`, UTC s) and where the sky was (`from`), to check and undo.
const char* const stripProbe = R"JS(JSON.stringify((function () {
  var box = document.getElementById('strip'), cv = document.getElementById('cv-strip'), r = { problems: [] };
  if (!box || !cv || !UI.dayStrip) return { problems: ['no strip'] };
  if (box.getAttribute('role') !== 'slider') r.problems.push('role');
  if (!box.getAttribute('aria-valuetext')) r.problems.push('valuetext');
  var h = box.getAttribute('data-hint') || '';
  if (!h || h.length > 90) r.problems.push('hint');
  var k = UI.dayStrip.view.keo;
  r.keogram = k ? [k.cols, k.rows, Math.round(k.ms * 10) / 10] : null;
  if (!k || k.cols < 100) r.problems.push('keogram');
  var dpr = Math.min(3, UI.dpr * UI.scale);
  r.canvas = [cv.width, cv.height, cv.clientWidth, cv.clientHeight, dpr];
  if (cv.width > Math.ceil(cv.clientWidth * dpr) + 1 || cv.height > Math.ceil(cv.clientHeight * dpr) + 1) r.problems.push('canvas');
  var seeks = [], native = WT.native;
  WT.native = function (n, a) { if (n === 'seek') seeks.push(a); return native.apply(WT, arguments); };
  var u0 = UI.dayStrip.shownUtc();
  r.from = u0;
  box.focus();
  box.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowRight', bubbles: true, cancelable: true }));
  r.keySeek = seeks.length ? Math.round(seeks[0] - u0) : null;
  if (r.keySeek === null || Math.abs(r.keySeek - 600) > 30) r.problems.push('keys');
  // a drag from a fifth to three fifths of the strip's width
  var rect = cv.getBoundingClientRect(), y = rect.top + rect.height * 0.4, n0 = seeks.length;
  var at = function (f) { return rect.left + rect.width * f; };
  var ev = function (type, f) { cv.dispatchEvent(new PointerEvent(type, { clientX: at(f), clientY: y, button: 0, buttons: type === 'pointerup' ? 0 : 1, pointerId: 7, bubbles: true, cancelable: true })); };
  UI.dayStrip.scrub.lastSeekAt = -1e9;   // the key's seek does not throttle the drag's first
  ev('pointerdown', 0.2);
  for (var i = 1; i <= 8; i++) ev('pointermove', 0.2 + 0.4 * i / 8);
  ev('pointerup', 0.6);
  WT.native = native;
  var d = seeks.slice(n0);
  r.dragSeeks = d.length;
  r.target = d.length ? d[d.length - 1] : 0;
  r.expect = UI.dayStrip.view.utcAt((at(0.6) - rect.left) / UI.scale, true);
  // WebKit rounds a synthetic event's clientX to whole pixels: allow one pixel of the day
  var px = UI.dayStrip.view.utcAt(201, true) - UI.dayStrip.view.utcAt(200, true) + 1;
  if (d.length < 2 || Math.abs(r.target - r.expect) > px || Math.abs(d[0] - UI.dayStrip.view.utcAt((at(0.2) - rect.left) / UI.scale, true)) > px) r.problems.push('drag');
  box.blur();
  return r;
})()))JS";

// T6.8: the regrouped page (spec/09 §3) and its More popovers (§5), in the real editor.
const char* const layoutProbe = R"JS(JSON.stringify((function () {
  var out = { sizes: [], canvas: [], more: [], moreCount: 0, moreParams: 0 };
  // module → [width, height] at the design size (CSS px, before the stage's zoom)
  var SIZE = { strip: [1110, 92], sky: [213, 92], resonators: [435, 264], eq: [435, 177], 'dome-wrap': [448, 448],
               harmony: [433, 448], phaser: [214, 260], chorus: [214, 260], macros: [448, 260], delay: [146, 260],
               reverb: [175, 260], output: [98, 260], topbar: [1350, 44], hintbar: [1350, 28] };
  Object.keys(SIZE).forEach(function (id) {
    var e = document.getElementById(id);
    if (!e || e.offsetWidth !== SIZE[id][0] || e.offsetHeight !== SIZE[id][1])
      out.sizes.push(id + ' ' + (e ? e.offsetWidth + 'x' + e.offsetHeight : 'missing'));
  });
  // no canvas grows past its box (the Retina trap, spec/06 §Rendering)
  var k = Math.min(3, UI.dpr * UI.scale);
  [].forEach.call(document.querySelectorAll('#stage canvas'), function (c) {
    if (c.width > Math.ceil(c.clientWidth * k) + 1 || c.height > Math.ceil(c.clientHeight * k) + 1)
      out.canvas.push((c.id || 'canvas') + ' ' + c.clientWidth + 'x' + c.clientHeight + ' css, ' + c.width + 'x' + c.height + ' px');
  });
  // the More popovers: a click on the title opens it and shows its controls; the first of them is the
  // next stop in the Tab order after the title; Escape closes it and gives the focus back to the title
  var focusables = function () {
    return [].filter.call(document.querySelectorAll('#stage button, #stage [tabindex]'), function (e) {
      return e.tabIndex >= 0 && !e.disabled && e.getClientRects().length > 0 && getComputedStyle(e).visibility !== 'hidden';
    });
  };
  [].forEach.call(document.querySelectorAll('.more-btn[aria-controls]'), function (btn) {
    var panel = document.getElementById(btn.getAttribute('aria-controls')), id = btn.closest('.module').id;
    out.moreCount++;
    if (!panel || !panel.hidden) { out.more.push(id + ' not closed'); return; }
    btn.click();
    var params = panel.querySelectorAll('[data-param]');
    out.moreParams += params.length;
    if (panel.hidden || btn.getAttribute('aria-expanded') !== 'true') out.more.push(id + ' click');
    [].forEach.call(params, function (p) { if (p.getClientRects().length === 0) out.more.push(id + ' ' + p.dataset.param + ' hidden'); });
    var f = focusables(), i = f.indexOf(btn);
    if (i < 0 || !panel.contains(f[i + 1])) out.more.push(id + ' tab');
    if (params.length) {
      var first = params[0].matches('[tabindex]') ? params[0] : params[0].querySelector('[tabindex="0"]');
      first.focus();
      document.activeElement.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true, cancelable: true }));
      if (!panel.hidden) out.more.push(id + ' escape');
      if (document.activeElement !== btn) out.more.push(id + ' focus back');
    }
    if (!panel.hidden) btn.click();
    btn.blur();
  });
  return out;
})()))JS";
// T6.9: the Journey transport's controls: each has a hint and a role, the day list and the speeds
// came with the day, telemetry carries the journey
const char* const transportProbe = R"JS(JSON.stringify((function () {
  var bad = [];
  ['tp-run', 'tp-speed', 'tp-day', 'tp-loop', 'clock'].forEach(function (id) {
    var e = document.getElementById(id);
    if (!e) { bad.push(id + ' missing'); return; }
    var h = e.getAttribute('data-hint') || '';
    if (!h || h.length > 90) bad.push(id + ' hint');
  });
  if (document.getElementById('tp-speed').getAttribute('role') !== 'radiogroup') bad.push('tp-speed role');
  if (document.getElementById('tp-loop').getAttribute('role') !== 'switch') bad.push('tp-loop role');
  if (document.getElementById('tp-day').getAttribute('aria-haspopup') !== 'menu') bad.push('tp-day popup');
  if (!document.querySelector('#tp-speed [role="radio"][tabindex="0"]')) bad.push('tp-speed focus');
  return { bad: bad, speeds: document.querySelectorAll('#tp-speed button').length,
           days: __wtState.day && __wtState.day.days ? __wtState.day.days.length : 0,
           journey: __wtState.tel.journey || null, dayButton: document.getElementById('tp-day').textContent };
})()))JS";

// T6.14: the EQ display in the real editor (spec/tasks/T6.14-eq-display.md). Synthetic pointer, wheel
// and key events on the picture; the values it touches are kept in window.__eqSaved and restored after
// the C++ side has read them.
const char* const eqProbe = R"JS(JSON.stringify((function () {
  var out = { bad: [] };
  var cv = document.getElementById('cv-eq');
  // the picture is drawn: the share of its backing store's pixels that are not empty
  var px = cv.getContext('2d').getImageData(0, 0, cv.width, cv.height).data, lit = 0;
  for (var i = 3; i < px.length; i += 4) if (px[i] > 0) lit++;
  out.drawn = Math.round(lit / (px.length / 4) * 1000) / 1000;
  out.size = [cv.clientWidth, cv.clientHeight, cv.width, cv.height];
  var ids = ['eq1_drive', 'eq2_freq', 'eq2_gain', 'eq3_q', 'eq4_freq', 'eq_sky'];
  var saved = { eq2_q: WT.get('eq2_q') };
  ids.forEach(function (id) { saved[id] = WT.get(id); });
  window.__eqSaved = saved;
  UI.commit('eq4_freq', 6500);   // a cut below the top, so the sky's reach is drawn
  UI.commit('eq_sky', 0.3);
  var ev = function (type, x, y) {
    cv.dispatchEvent(new PointerEvent(type, { clientX: x, clientY: y, bubbles: true, cancelable: true, pointerId: 1, button: 0,
                                              buttons: type === 'pointerup' ? 0 : 1, isPrimary: true, pointerType: 'mouse' }));
  };
  var pull = function (a, dx, dy) {
    ev('pointerdown', a.x, a.y);
    for (var i = 1; i <= 4; i++) ev('pointermove', a.x + dx * a.scale * i / 4, a.y + dy * a.scale * i / 4);
    ev('pointerup', a.x + dx * a.scale, a.y + dy * a.scale);
  };
  var near = function (x, y, tol) { return Math.abs(x - y) <= tol; };
  // a dot: band 2, 30 px right (frequency) and 15 px up (gain)
  var a = UI.eq.at('dot', 1), f0 = WT.get('eq2_freq'), g0 = WT.get('eq2_gain');
  pull(a, 30, -15);
  var fx = f0 * Math.pow(1000, 30 / a.w), gx = Math.min(12, g0 + 15 / a.span * 12);
  out.dot = [Math.round(WT.get('eq2_freq')), Math.round(fx), WT.get('eq2_gain'), Math.round(gx * 10) / 10];
  if (!near(Math.log(WT.get('eq2_freq') / fx), 0, 0.02) || !near(WT.get('eq2_gain'), gx, 0.15)) out.bad.push('dot');
  // scroll over a bell: band 3's width, one notch up (narrower)
  var b = UI.eq.at('dot', 2), q0 = WT.get('eq3_q');
  cv.dispatchEvent(new WheelEvent('wheel', { clientX: b.x, clientY: b.y, deltaY: -100, bubbles: true, cancelable: true }));
  out.wheel = [q0, Math.round(WT.get('eq3_q') * 1000) / 1000];
  if (!near(WT.get('eq3_q'), q0 * Math.exp(0.11), 0.01)) out.bad.push('wheel');
  // pull a drive bar: band 1, from its foot 11 px up → 12 of its 22 px
  var c = UI.eq.at('bar', 0);
  pull(c, 0, -11);
  out.bar = Math.round(WT.get('eq1_drive') * 1000) / 1000;
  if (!near(WT.get('eq1_drive'), 12 / c.bh, 0.02)) out.bad.push('bar');
  // the sky's reach: its end 20 px to the left
  var s = UI.eq.at('sky'), s0 = WT.get('eq_sky');
  pull(s, -20, 0);
  var sx = s0 - 20 / s.w * Math.log2(1000) / 3;
  out.sky = [s0, Math.round(WT.get('eq_sky') * 1000) / 1000, Math.round(sx * 1000) / 1000];
  if (!near(WT.get('eq_sky'), sx, 0.01)) out.bad.push('sky');
  // the keys on a dot: up and down move the gain, left and right the frequency, ] the width
  var dot = document.querySelector('[data-param="eq2_freq"]'), key = function (k) {
    document.activeElement.dispatchEvent(new KeyboardEvent('keydown', { key: k, bubbles: true, cancelable: true }));
  };
  dot.focus();
  var v = [WT.get('eq2_freq'), WT.get('eq2_gain'), WT.get('eq2_q')];
  key('ArrowUp');
  if (WT.get('eq2_gain') <= v[1] || WT.get('eq2_freq') !== v[0]) out.bad.push('key up');
  key('ArrowLeft');
  if (WT.get('eq2_freq') >= v[0]) out.bad.push('key left');
  key(']');
  if (WT.get('eq2_q') <= v[2]) out.bad.push('key ]');
  key('Backspace');   // the frequency's default
  dot.blur();
  cv.dispatchEvent(new PointerEvent('pointerleave', { pointerId: 1, pointerType: 'mouse' }));   // the pointer goes
  out.after = ids.map(function (id) { return Math.round(WT.get(id) * 1000) / 1000; });
  return out;
})()))JS";
// T6.11: drag the Low|Red crossover four semitones up in the resonator display, with pointer events
// as a person's drag sends them; the parameter is read back here and in C++, then restored.
const char* const resonatorProbe = R"JS(JSON.stringify((function () {
  var cv = document.getElementById('cv-res'), r = cv.getBoundingClientRect(), k = r.width / cv.clientWidth, g = UI.res.geom();
  var x0 = WT.get('band_x1'), y = r.top + g.gy * k;
  var send = function (type, m) {
    cv.dispatchEvent(new PointerEvent(type, { pointerId: 1, pointerType: 'mouse', isPrimary: true, bubbles: true, cancelable: true,
                                              button: 0, buttons: type === 'pointerup' ? 0 : 1, clientX: r.left + g.X(m) * k, clientY: y }));
  };
  send('pointermove', x0);
  send('pointerdown', x0);
  send('pointermove', x0 + 2);
  send('pointermove', x0 + 4);
  send('pointerup', x0 + 4);
  send('pointerleave', x0 + 4);   // the pointer goes, the grip is no longer hot
  return { from: x0, to: WT.get('band_x1'), grips: document.querySelectorAll('#resonators .proxy.grip[data-param]').length,
           levels: document.querySelectorAll('#resonators .proxy.level[data-param]').length, canvas: cv.width + 'x' + cv.height,
           css: cv.clientWidth + 'x' + cv.clientHeight, lines: UI.res.lines().length };
})()))JS";

// T6.12: a drag in each picture, with pointer events dispatched in WebKit (the views' own handlers);
// values are restored afterwards by restoreProbe, after C++ has read them.
const char* const picturesProbe = R"JS(JSON.stringify((function () {
  var IDS = ['ph_depth', 'ph_feedback', 'ch_rate', 'dly_time', 'dly_fb', 'rev_decay', 'lim_ceiling'];
  var before = {}, after = {}, out = { telemetry: [], proxies: [], before: before, after: after };
  IDS.forEach(function (id) { before[id] = WT.get(id); });
  window.__t612 = before;
  var drag = function (cvId, x0, y0, x1, y1) {
    var cv = document.getElementById(cvId), r = cv.getBoundingClientRect(), s = UI.scale;
    var ev = function (type, x, y) {
      cv.dispatchEvent(new PointerEvent(type, { clientX: r.left + (x + cv.clientLeft) * s, clientY: r.top + (y + cv.clientTop) * s,
        button: 0, buttons: type === 'pointerup' ? 0 : 1, pointerId: 7, isPrimary: true, bubbles: true, cancelable: true }));
    };
    ev('pointerdown', x0, y0);
    for (var i = 1; i <= 6; i++) ev('pointermove', x0 + (x1 - x0) * i / 6, y0 + (y1 - y0) * i / 6);
    ev('pointerup', x1, y1);
  };
  drag('cv-phaser', 90, 70, 60, 50);    // left: shallower sweep; up: more feedback
  drag('cv-chorus', 90, 70, 120, 70);   // right: faster
  drag('cv-delay', 40, 68, 70, 40);     // the first echo later, more feedback
  drag('cv-reverb', 60, 100, 90, 100);  // right: a longer tail
  drag('cv-output', 30, 40, 30, 60);    // down: a lower ceiling
  IDS.forEach(function (id) { after[id] = WT.get(id); });
  var tel = __wtState.tel;
  if (!tel.spectrumIn || tel.spectrumIn.length !== 120) out.telemetry.push('spectrumIn');
  if (typeof tel.phaser !== 'number' || tel.phaser < 0 || tel.phaser > 1) out.telemetry.push('phaser');
  if (!(tel.sampleRate > 0)) out.telemetry.push('sampleRate');
  [].forEach.call(document.querySelectorAll('.handle[data-param]'), function (h) {
    var cv = h.parentElement.querySelector('canvas.view'), x = h.offsetLeft + h.offsetWidth / 2, y = h.offsetTop + h.offsetHeight / 2;
    if (!cv || x < cv.offsetLeft || x > cv.offsetLeft + cv.offsetWidth || y < cv.offsetTop || y > cv.offsetTop + cv.offsetHeight) out.proxies.push(h.dataset.param);
  });
  // the page's phaser response on a grid, for C++ to compare with dsp::Phaser::magnitude
  out.response = [];
  [[632.5, 6, 0.3, 0.4], [2000, 8, 0.8, 1], [200, 4, 0.8, 0.5], [900, 3, 0.55, 0.7]].forEach(function (c) {
    for (var k = 0; k <= 30; k++) { var f = 20 * Math.pow(1000, k / 30); out.response.push([f, c[0], c[1], c[2], c[3], UI.phaserResponse(f, c[0], 48000, c[1], c[2], c[3])]); }
  });
  return out;
})()))JS";

const char* const restoreProbe = R"JS((function () {
  var b = window.__t612 || {};
  Object.keys(b).forEach(function (id) { UI.commit(id, b[id]); });
  return Object.keys(b).length;
})())JS";
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

    // T6.8: the layout, the canvases, the More popovers
    const auto layout = evalSync (*browser, juce::String::fromUTF8 (layoutProbe), 20000);
    auto parsedLayout = juce::JSON::parse (layout);
    bool layoutOk = parsedLayout.isObject();
    for (const auto* list : { "sizes", "canvas", "more" })
        layoutOk = layoutOk && parsedLayout[list].isArray() && parsedLayout[list].size() == 0;
    // T6.11: the band edges, tones and shifts left Resonators' More for the display; T6.12 and T6.14
    // emptied others (the EQ has none)
    layoutOk = layoutOk && static_cast<int> (parsedLayout["moreCount"]) >= 4 && static_cast<int> (parsedLayout["moreParams"]) >= 8;

    // T6.11: a crossover dragged in the picture moves the parameter, in the page and in the engine
    const auto resDrag = evalSync (*browser, juce::String::fromUTF8 (resonatorProbe));
    pump (300);
    auto parsedRes = juce::JSON::parse (resDrag);
    const float cppX1 = processor.getValueTreeState().getRawParameterValue ("band_x1")->load();
    const int x1From = static_cast<int> (parsedRes["from"]);
    const bool resOk = parsedRes.isObject() && static_cast<int> (parsedRes["to"]) == x1From + 4
                    && std::abs (cppX1 - static_cast<float> (x1From + 4)) < 0.001f
                    && static_cast<int> (parsedRes["grips"]) == 5 && static_cast<int> (parsedRes["levels"]) == 4;
    evalSync (*browser, "UI.commit('band_x1', " + juce::String (x1From) + "); true");
    pump (300);

    // T6.12: the pictures as controls. The drags' values must have reached the processor.
    const auto pictures = evalSync (*browser, juce::String::fromUTF8 (picturesProbe), 20000);
    pump (400);
    auto parsedPictures = juce::JSON::parse (pictures);
    auto& apvts = processor.getValueTreeState();
    const auto cpp = [&apvts] (const char* id) { return static_cast<double> (apvts.getRawParameterValue (id)->load()); };
    const auto js = [&parsedPictures] (const char* when, const char* id) { return static_cast<double> (parsedPictures[when][id]); };
    juce::StringArray dragBad;
    const auto expect = [&] (const char* id, int direction)
    {
        const double b = js ("before", id), a = js ("after", id);
        if (direction * (a - b) <= 1.0e-6 || std::abs (cpp (id) - a) > 1.0e-3 * (1.0 + std::abs (a)))
            dragBad.add (juce::String (id) + " " + juce::String (b, 3) + " -> js " + juce::String (a, 3) + ", c++ " + juce::String (cpp (id), 3));
    };
    expect ("ph_depth", -1);
    expect ("ph_feedback", 1);
    expect ("ch_rate", 1);
    expect ("dly_time", 1);
    expect ("dly_fb", 1);
    expect ("rev_decay", 1);
    expect ("lim_ceiling", -1);
    double responseError = 1.0e9;
    if (const auto* rows = parsedPictures["response"].getArray())
    {
        responseError = 0.0;
        for (const auto& r : *rows)
        {
            const double want = worldtable::dsp::Phaser::magnitude (r[0], r[1], 48000.0, static_cast<int> (r[2]), r[3], r[4]);
            responseError = std::max (responseError, std::abs (20.0 * std::log10 (want) - 20.0 * std::log10 (static_cast<double> (r[5]))));
        }
    }
    const auto restored = evalSync (*browser, juce::String::fromUTF8 (restoreProbe));
    pump (400);
    bool picturesOk = parsedPictures.isObject() && dragBad.isEmpty() && responseError < 1.0e-6
                   && parsedPictures["response"].size() > 100;
    for (const auto* list : { "telemetry", "proxies" })
        picturesOk = picturesOk && parsedPictures[list].isArray() && parsedPictures[list].size() == 0;

    // T6.14: the EQ display, by pointer and keys; the values it set arrive in the processor
    const auto eqRun = evalSync (*browser, juce::String::fromUTF8 (eqProbe), 20000);
    pump (400);
    auto parsedEq = juce::JSON::parse (eqRun);
    bool eqOk = parsedEq.isObject() && parsedEq["bad"].isArray() && parsedEq["bad"].size() == 0
             && static_cast<double> (parsedEq["drawn"]) > 0.05;
    juce::String eqCpp;
    {
        const char* const ids[] = { "eq1_drive", "eq2_freq", "eq2_gain", "eq3_q", "eq4_freq", "eq_sky" };
        const auto* after = parsedEq["after"].getArray();
        eqOk = eqOk && after != nullptr && after->size() == 6;
        for (int i = 0; eqOk && i < 6; ++i)
        {
            const double inCpp = cpp (ids[i]), inJs = (*after)[i];
            eqCpp << (i ? ", " : "") << ids[i] << " " << inCpp;
            eqOk = eqOk && std::abs (inCpp - inJs) <= 0.002 * std::max (1.0, std::abs (inJs));
        }
    }
    evalSync (*browser, "Object.keys(window.__eqSaved).forEach(function (id) { UI.commit(id, window.__eqSaved[id]); }); true");
    pump (300);

    bool accessOk = parsedAccess.isObject();
    for (const auto* list : { "hint", "long", "role", "text", "focus", "keys", "pictures", "hues" })
        accessOk = accessOk && parsedAccess[list].isArray() && parsedAccess[list].size() == 0;
    accessOk = accessOk && std::abs (static_cast<double> (parsedAccess["typed"]) - 0.5) < 1.0e-3;
    const auto* ring = parsedAccess["spaceRing"].getArray();
    accessOk = accessOk && ring != nullptr && ring->size() == 3
            && std::abs (static_cast<double> ((*ring)[0]) - 0.5) < 1.0e-3
            && std::abs (static_cast<double> ((*ring)[1]) - 1.0) < 1.0e-3
            && std::abs (static_cast<double> ((*ring)[2]) - 1.5) < 1.0e-3;

    // T6.9: the transport, in the real editor: its controls, journeySet reaching the processor, a day
    // picked from the menu loading, then the defaults back
    const auto transport = evalSync (*browser, juce::String::fromUTF8 (transportProbe));
    auto parsedTransport = juce::JSON::parse (transport);
    evalSync (*browser, "WT.native('journeySet', { speed: 240, loop: true }); true");
    for (int k = 0; k < 10; ++k) { processor.processBlock (buffer, midi); pump (25); }
    const auto set = processor.getJourney();
    const bool setOk = set.speed == 240.0 && set.loop && processor.getSky().model().speed() == 240.0;
    evalSync (*browser, "document.getElementById('tp-day').click();"
                        " document.querySelectorAll('#tp-days [role=\"menuitemradio\"]')[2].click(); true");
    for (int k = 0; k < 40; ++k) { processor.processBlock (buffer, midi); pump (25); }
    const auto picked = evalSync (*browser, "JSON.stringify({ day: __wtState.day && __wtState.day.day,"
                                            " tel: __wtState.tel.journey && __wtState.tel.journey.day,"
                                            " menu: document.getElementById('tp-days').hidden })");
    const auto parsedPicked = juce::JSON::parse (picked);
    const bool pickOk = processor.getSkyFeed().dayId() == "2025-01-25" && parsedPicked["day"].toString() == "2025-01-25"
                     && parsedPicked["tel"].toString() == "2025-01-25" && static_cast<bool> (parsedPicked["menu"]);
    evalSync (*browser, "WT.native('journeySet', { day: '2024-05-10', speed: 24, loop: false }); true");
    for (int k = 0; k < 40; ++k) { processor.processBlock (buffer, midi); pump (25); }
    const bool transportOk = parsedTransport.isObject() && parsedTransport["bad"].isArray() && parsedTransport["bad"].size() == 0
                          && static_cast<int> (parsedTransport["speeds"]) == static_cast<int> (worldtable::JourneyState::kSpeeds.size())
                          && static_cast<int> (parsedTransport["days"]) == 3
                          && parsedTransport["journey"].isObject() && parsedTransport["journey"]["day"].toString() == "2024-05-10"
                          && setOk && pickOk && processor.getSkyFeed().dayId() == "2024-05-10";

    // T6.10: the day strip; after the drag the sky must have landed where it let go, then it goes back
    const auto strip = evalSync (*browser, juce::String::fromUTF8 (stripProbe), 10000);
    auto parsedStrip = juce::JSON::parse (strip);
    for (int k = 0; k < 30; ++k)
    {
        processor.processBlock (buffer, midi);
        pump (20);
    }
    const double landed = evalSync (*browser, "__wtState.tel.sky.utc").getDoubleValue();
    const double target = parsedStrip["target"];
    evalSync (*browser, "UI.dayStrip.scrubTo(" + juce::String (static_cast<double> (parsedStrip["from"]), 3) + ", true); true");
    pump (300);
    const bool stripOk = parsedStrip.isObject() && parsedStrip["problems"].isArray() && parsedStrip["problems"].size() == 0
                      && landed >= target - 1.0 && landed < target + 120.0;   // ≈ 0.6 s at 24× after the seek

    auto parsed = juce::JSON::parse (state);
    const bool ok = ready == "true"
                 && transportOk
                 && accessOk
                 && eqOk
                 && stripOk
                 && layoutOk
                 && resOk
                 && picturesOk
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
              << "day strip:  " << strip << ", the sky landed " << juce::String (landed - target, 1) << " s after the drag's end\n"
              << "layout, canvases, More: " << layout << "\n"
              << "EQ display: " << eqRun << "\n"
              << "EQ in C++:  " << eqCpp << "\n"
              << "resonators, a crossover dragged: " << resDrag << ", C++ band_x1 " << cppX1 << "\n"
              << "transport:  " << transport << "; journeySet 240x + loop → C++ " << set.speed << "x, loop " << (set.loop ? "on" : "off")
              << "; day menu → " << picked << " (C++ " << processor.getSkyFeed().dayId() << " after the restore, load "
              << juce::String (processor.getSkyFeed().lastLoadMs(), 1) << " ms)" << (transportOk ? "" : "  FAILED") << "\n"
              << "pictures (T6.12): drags " << (dragBad.isEmpty() ? juce::String ("all read back in C++") : dragBad.joinIntoString ("; "))
              << ", page phaser response vs dsp::Phaser " << responseError << " dB"
              << ", telemetry " << juce::JSON::toString (parsedPictures["telemetry"], true)
              << ", proxies off their picture " << juce::JSON::toString (parsedPictures["proxies"], true)
              << ", restored " << restored << "\n"
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
