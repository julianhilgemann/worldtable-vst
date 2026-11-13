/* The welcome tour (T6.13): five steps, each a spotlight on a real region of the page with a card
 * beside it (title, a few sentences, Skip tour, Next or Done, Step n of 5). Texts: the mockup's
 * TOUR. It opens by itself once per computer, 1.5 s after the first editor opens (`tourSeen` in the
 * editor's settings file, set when the tour is finished or skipped), and again from the bottom bar's
 * Tour and the context menu. The page stays live under it: the dim lets the pointer through, so
 * each step's "try it" works. Escape skips. The spotlight glides from step to step; with reduced
 * motion it cuts.
 *
 * WebKit: the dim (with the spotlight) and the card sit over the dome's WebGL canvas, so both have
 * their own compositing layer (style.css, translateZ(0)). The dim is one stage-sized layer that
 * clips the spotlight's shadow; a 2000 px shadow on a layer of its own would be a huge texture. */
(function () {
  "use strict";
  const UI = window.UI;
  const $ = (id) => document.getElementById(id);
  const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
  const BASE_W = 1350, BASE_H = 900, PAD = 3, GAP = 12, CARD_W = 290;
  const reduced = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  /** [the regions the spotlight covers, title, text]. Step 5 covers the macros alone until T4.2 adds
   *  the LFO and Envelope panels: then it takes their ids and the mockup's text ("Macros, an LFO and
   *  an envelope move other controls. Press Fire and watch Shimmer: a ring on a knob means something
   *  is moving it."). */
  const TOUR = (UI.TOUR = [
    [["strip"], "The day", "This strip is the whole recorded day: the sky's light from north to south, and the magnetic field under it. Click anywhere to jump there."],
    [["dome-wrap"], "The light", "Every ray of the curtain is one resonator. Brighter light bows it harder; the three colours are the three bands."],
    [["harmony"], "The chord", "The sky's phase picks the chord. Switch to Play and your keyboard plays the same resonators."],
    [["col-left"], "The sound", "The resonators make the sound and the EQ under them shapes its tone. The effects run along the bottom. Drag inside the pictures."],
    [["macros"], "Motion", "The macros move other controls. Point at one to light what it moves: a ring on a knob means something is moving it."],
  ]);

  const dim = $("tour-dim"), spot = $("tour-spot"), card = $("tour-card"), go = $("tour-go");
  let at = -1, from = null;

  /** The box around a step's regions, in stage pixels (before the stage's zoom). */
  function regionOf(ids) {
    const s = $("stage").getBoundingClientRect();
    let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
    for (const id of ids) {
      const n = $(id);
      if (!n) continue;
      const r = n.getBoundingClientRect();
      x0 = Math.min(x0, r.left); y0 = Math.min(y0, r.top); x1 = Math.max(x1, r.right); y1 = Math.max(y1, r.bottom);
    }
    if (!(x1 > x0)) return { x: 0, y: 0, w: BASE_W, h: BASE_H };
    const k = UI.scale || 1;
    return { x: (x0 - s.left) / k, y: (y0 - s.top) / k, w: (x1 - x0) / k, h: (y1 - y0) / k };
  }

  /** The card under the region when it fits there, else over it; centred on it, inside the stage. */
  function cardPlace(p, ch) {
    const below = p.y + p.h + GAP + ch <= BASE_H - 10;
    return {
      x: clamp(p.x + p.w / 2 - CARD_W / 2, 10, BASE_W - CARD_W - 10),
      y: below ? p.y + p.h + GAP : Math.max(10, p.y - ch - GAP),
    };
  }

  const px = (v) => Math.round(v * 10) / 10 + "px";
  function show(glide) {
    const [ids, title, text] = TOUR[at];
    $("tour-n").textContent = `Step ${at + 1} of ${TOUR.length}`;
    $("tour-title").textContent = title;
    $("tour-text").textContent = text;
    go.textContent = at === TOUR.length - 1 ? "Done" : "Next";
    const cut = !glide || reduced;
    dim.classList.toggle("cut", cut);
    card.classList.toggle("cut", cut);
    dim.hidden = false;
    card.hidden = false;
    const p = regionOf(ids);
    Object.assign(spot.style, { left: px(p.x - PAD), top: px(p.y - PAD), width: px(p.w + 2 * PAD), height: px(p.h + 2 * PAD) });
    const c = cardPlace(p, card.offsetHeight);
    card.style.left = px(c.x);
    card.style.top = px(c.y);
    card.dataset.step = String(at);
    go.focus({ preventScroll: true });
  }

  function start(step = 0) {
    if (UI.menu && UI.menu.open) UI.menu.close();
    const was = at;
    if (was < 0) from = document.activeElement;
    at = clamp(step | 0, 0, TOUR.length - 1);
    show(was >= 0);
  }
  /** Ends the tour (finished or skipped); it will not open by itself again on this computer. */
  function end() {
    if (at < 0) return;
    at = -1;
    dim.hidden = true;
    card.hidden = true;
    UI.settings.markTourSeen();
    if (from && from.isConnected && from !== document.body) from.focus({ preventScroll: true });
    else go.blur();
    from = null;
  }
  function next() {
    if (at < 0) return;
    if (at >= TOUR.length - 1) end();
    else { at += 1; show(true); }
  }

  go.addEventListener("click", (e) => { e.stopPropagation(); next(); });
  $("tour-skip").addEventListener("click", (e) => { e.stopPropagation(); end(); });
  $("tour-btn").addEventListener("click", (e) => { e.stopPropagation(); start(0); });
  document.addEventListener("keydown", (e) => { if (e.key === "Escape" && at >= 0) end(); });

  UI.tour = { start, end, next, get step() { return at; }, regionOf, steps: TOUR.length };

  // once per computer, 1.5 s after the editor opens; never in review screenshots (#shot) unless
  // asked for with #tour=<step> (1…5)
  const hash = new URLSearchParams(location.hash.slice(1));
  if (hash.has("tour")) start((Number(hash.get("tour")) || 1) - 1);
  else if (!hash.has("shot"))
    UI.settings.ready.then((s) => {
      if (!s.tourSeen) setTimeout(() => { if (at < 0 && !UI.settings.get().tourSeen) start(0); }, 1500);
    });
})();
