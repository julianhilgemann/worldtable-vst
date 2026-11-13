/* The window (T6.13, spec/09 §3): three sizes, the page's own context menu, About.
 *
 * Sizes: Small 85 %, Medium 100 %, Large 125 % of the 1350 × 900 page. In the plug-in the editor
 * sets the window's size (native `setUiSize`) and keeps the choice per computer (a settings file,
 * PluginEditor.h UiSettings); the page scales to the window as it always has (app.js rescale). In
 * the browser mock the stage scales and the choice lives in localStorage.
 *
 * The context menu replaces the web view's own (Reload, Inspect), which never shows. On a control
 * (a knob, a picture's handle, a choice): its hint, dimmed, then Reset to default and, where Enter
 * opens a value field, Enter value…: the keyboard's own paths (Backspace, Enter). Anywhere else: the sizes, the welcome
 * tour, About. It is clamped to the stage; Escape, a click outside or a choice closes it. Everything
 * in it is reachable without a right-click too (the bottom bar, double-click, Enter).
 * T4.2: on a control the grid can move, "Modulate with ▸" opens the sources beside the menu (one
 * makes a route at +50 %), and the control's routes follow, each a row that clears it.
 *
 * WebKit: the menu and About open over the dome's WebGL canvas, so each has its own compositing
 * layer (style.css, translateZ(0)); Chrome paints them either way. */
(function () {
  "use strict";
  const WT = window.WT, UI = window.UI;
  const $ = (id) => document.getElementById(id);
  const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
  const BASE_W = 1350, BASE_H = 900, EDGE = 6;
  const SIZES = [
    { name: "Small", factor: 0.85, px: "1148 × 765" },
    { name: "Medium", factor: 1, px: "1350 × 900" },
    { name: "Large", factor: 1.25, px: "1688 × 1125" },
  ];
  const MEDIUM = 1;
  const sizeIndex = (i) => (Number.isInteger(i) && i >= 0 && i < SIZES.length ? i : MEDIUM);

  // ---------------------------------------------------------------- the per-computer settings
  const local = {
    get(k, d) { try { const v = window.localStorage.getItem("wt." + k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } },
    set(k, v) { try { window.localStorage.setItem("wt." + k, JSON.stringify(v)); } catch (e) { /* private window: fine */ } },
  };
  const S = { size: MEDIUM, tourSeen: false, version: "", wrapper: "" };
  if (WT.isMock) {
    Object.assign(S, { size: sizeIndex(local.get("uiSize", MEDIUM)), tourSeen: !!local.get("tourSeen", false), version: "mock", wrapper: "browser" });
    UI.sizeFactor = SIZES[S.size].factor;
    UI.rescale();
  }
  const ready = (WT.isMock ? Promise.resolve(S) : WT.native("getUiSettings"))
    .then((got) => {
      if (got && typeof got === "object") Object.assign(S, got);
      S.size = sizeIndex(S.size);
      showSize();
      $("about-ver").textContent = [S.version, S.wrapper].filter(Boolean).join("  ");
      return S;
    });

  /** UI.settings: what the editor keeps per computer. `ready` resolves with { size, tourSeen, … }. */
  const settings = (UI.settings = {
    ready,
    get: () => S,
    SIZES,
    /** Sets the window's size (0 Small, 1 Medium, 2 Large) and keeps the choice. */
    setSize(i) {
      i = sizeIndex(i);
      if (i === S.size) return;
      S.size = i;
      showSize();
      if (WT.isMock) {
        local.set("uiSize", i);
        UI.sizeFactor = SIZES[i].factor;
        UI.rescale();
      } else {
        WT.native("setUiSize", i);   // the window resizes; the page follows on its resize event
      }
    },
    /** The tour was finished or skipped: it does not open by itself again on this computer. */
    markTourSeen() {
      if (S.tourSeen) return;
      S.tourSeen = true;
      if (WT.isMock) local.set("tourSeen", true); else WT.native("setTourSeen");
    },
  });

  // ---------------------------------------------------------------- S M L on the bottom bar
  const seg = $("size-seg"), segButtons = [...seg.querySelectorAll("button")];
  function showSize() {
    segButtons.forEach((b, i) => {
      b.classList.toggle("on", i === S.size);
      b.setAttribute("aria-checked", String(i === S.size));
      b.tabIndex = i === S.size ? 0 : -1;
    });
    seg.setAttribute("aria-valuetext", SIZES[S.size].name);
  }
  segButtons.forEach((b, i) => b.addEventListener("click", () => settings.setSize(i)));
  // arrows step one size (no wrap: Large → Small would be a big jump of the window); Home, End
  seg.addEventListener("keydown", (e) => {
    const k = e.key, last = SIZES.length - 1;
    const to = k === "ArrowRight" || k === "ArrowDown" ? Math.min(last, S.size + 1) : k === "ArrowLeft" || k === "ArrowUp" ? Math.max(0, S.size - 1)
      : k === "Home" ? 0 : k === "End" ? last : null;
    if (to === null) return;
    e.preventDefault();
    settings.setSize(to);
    segButtons[to].focus({ preventScroll: true });
  });
  showSize();

  // ---------------------------------------------------------------- the context menu
  const stage = $("stage"), menu = $("ctx-menu");
  let menuFrom = null;   // what had the focus before the menu opened (it gets it back)

  /** A menu row: a label, a small grey note (beside the label, or at the right with `right`). */
  function item(label, note, pick, opts = {}) {
    const parent = opts.parent || menu;
    const b = document.createElement("button");
    b.type = "button";
    b.tabIndex = -1;
    b.setAttribute("role", opts.radio ? "menuitemradio" : "menuitem");
    if (opts.radio) b.setAttribute("aria-checked", String(!!opts.on));
    if (opts.on) b.classList.add("on");
    if (opts.action) b.dataset.action = opts.action;
    const s = document.createElement("span");
    s.textContent = label;
    b.appendChild(s);
    if (note) {
      const n = document.createElement("small");
      n.textContent = note;
      (opts.right ? b : s).appendChild(n);
    }
    if (opts.disabled) b.setAttribute("aria-disabled", "true");
    b.addEventListener("click", (e) => {
      e.stopPropagation();
      if (opts.disabled) return;
      if (opts.keep) { pick(); return; }
      closeMenu();
      pick();
    });
    parent.appendChild(b);
    return b;
  }
  const rule = () => { const hr = document.createElement("hr"); hr.setAttribute("role", "separator"); menu.appendChild(hr); };

  /** Background and panels: the sizes, the tour, About. */
  function windowMenu() {
    menu.dataset.kind = "window";
    SIZES.forEach((s, i) => item(s.name, s.px, () => settings.setSize(i), { radio: true, on: i === S.size, action: "size-" + i }));
    rule();
    item("Welcome tour", "", () => UI.tour && UI.tour.start(0), { action: "tour" });
    item("About Worldtable", "", () => about.set(true), { action: "about" });
  }

  /** The default as the control shows it: a choice's label, else the knob's value text. */
  function defaultText(ctl, id, v) {
    if (ctl.getAttribute("role") === "radiogroup") {
      const radio = ctl.querySelectorAll('[role="radio"]')[Math.round(v)];
      if (radio) return radio.getAttribute("aria-label") || radio.textContent;
    }
    return UI.format(id, v);
  }

  /** The key a control takes (spec/09 §5), sent to it as the keyboard would: its own code path. */
  const press = (ctl, key) => ctl.dispatchEvent(new KeyboardEvent("keydown", { key, bubbles: true, cancelable: true }));

  /** A control (a knob, a handle or its proxy, a value chip, a choice, a switch): its hint, Reset to
   *  default (Backspace: every control's default, a crossover kept between its neighbours), Enter
   *  value… where Enter opens a value field (every slider but a value chip, a button that steps). */
  function controlMenu(ctl) {
    const id = ctl.dataset.param;
    menu.dataset.kind = "control";
    menu.dataset.param = id;
    const hint = document.createElement("div");
    hint.className = "ctx-hint";
    hint.setAttribute("role", "none");
    hint.textContent = ctl.dataset.hint || UI.hintOf(id);
    menu.appendChild(hint);
    item("Reset to default", defaultText(ctl, id, UI.defaultValue(id)), () => press(ctl, "Backspace"), { right: true, action: "reset" });
    if (ctl.getAttribute("role") === "slider" && ctl.tagName !== "BUTTON")
      item("Enter value…", "", () => { ctl.focus({ preventScroll: true }); press(ctl, "Enter"); }, { action: "enter" });
    modulationItems(id);
  }

  // ---------------------------------------------------------------- T4.2: Modulate with ▸, the routes
  const pct = (d) => (d > 0 ? "+" : "−") + Math.round(Math.abs(d) * 100) + " %";
  let sub = null, subFrom = null;
  function closeSub(focusBack) {
    if (!sub) return;
    sub.remove();
    sub = null;
    if (subFrom) { subFrom.classList.remove("sub-open"); subFrom.setAttribute("aria-expanded", "false"); if (focusBack) subFrom.focus({ preventScroll: true }); }
    subFrom = null;
  }
  /** The sources in their groups beside the menu; a source already routed here says by how much. */
  function openSub(from, dst, focusFirst) {
    if (sub && subFrom === from) { if (focusFirst) { const f = sub.querySelector("button:not([aria-disabled])"); if (f) f.focus({ preventScroll: true }); } return; }
    closeSub(false);
    const info = UI.mod.info;
    sub = document.createElement("div");
    sub.className = "ctx-menu ctx-sub";
    sub.setAttribute("role", "menu");
    sub.setAttribute("aria-label", "Modulate with");
    const GROUP = { macros: "Macros", sky: "Sky", motion: "Motion" };
    let group = null;
    const j = info.destinations.findIndex((d) => d.key === dst.key);
    info.sources.forEach((src, i) => {
      if (src.group !== group) {
        group = src.group;
        const h = document.createElement("div");
        h.className = "ctx-group";
        h.setAttribute("role", "none");
        h.textContent = GROUP[group] || group;
        sub.appendChild(h);
      }
      const depth = info.depths[i][j];
      item(src.label, depth ? pct(depth) : "", () => UI.mod.set(src.key, dst.key, 0.5), { parent: sub, right: true, disabled: !!depth, action: "mod-" + src.key });
    });
    sub.addEventListener("keydown", (e) => {
      const items = [...sub.querySelectorAll("button:not([aria-disabled])")], i = items.indexOf(document.activeElement), k = e.key;
      e.stopPropagation();
      if (k === "ArrowLeft" || k === "Escape") { e.preventDefault(); closeSub(true); return; }
      if (k === "Tab") { e.preventDefault(); closeMenu(); return; }
      const to = k === "ArrowDown" ? (i + 1) % items.length : k === "ArrowUp" ? (i + items.length - 1) % items.length : k === "Home" ? 0 : k === "End" ? items.length - 1 : null;
      if (to === null || !items.length) return;
      e.preventDefault();
      items[to].focus({ preventScroll: true });
    });
    menu.appendChild(sub);
    subFrom = from;
    from.classList.add("sub-open");
    from.setAttribute("aria-expanded", "true");
    // beside the row, on the right unless that leaves the stage; kept inside it
    sub.style.top = Math.min(from.offsetTop - 5, BASE_H - EDGE - menu.offsetTop - sub.offsetHeight) + "px";
    sub.classList.toggle("left", menu.offsetLeft + menu.offsetWidth + 4 + sub.offsetWidth > BASE_W - EDGE);
    if (focusFirst) { const f = sub.querySelector("button:not([aria-disabled])"); if (f) f.focus({ preventScroll: true }); }
  }
  /** On a control the grid moves: Modulate with ▸, then its routes, each a row that clears it. */
  function modulationItems(id) {
    if (!UI.mod || !UI.mod.info) return;
    const dst = UI.mod.info.destinations.find((d) => d.key === id || (d.params || []).includes(id));
    if (!dst) return;
    rule();
    const open = item("Modulate with ▸", "", () => openSub(open, dst, true), { keep: true, action: "modulate" });
    open.setAttribute("aria-haspopup", "menu");
    open.setAttribute("aria-expanded", "false");
    open.addEventListener("pointerenter", () => openSub(open, dst, false));
    open.addEventListener("keydown", (e) => { if (e.key === "ArrowRight") { e.preventDefault(); e.stopPropagation(); openSub(open, dst, true); } });
    for (const r of UI.mod.routesOf(id))
      item(`Clear ${r.source.label}`, pct(r.depth), () => UI.mod.set(r.source.key, r.destination.key, 0), { right: true, action: "clear-" + r.source.key });
  }

  /** Opens the menu at (x, y) in stage pixels, kept inside the stage. */
  function openMenu(x, y, build, focusFirst) {
    menuFrom = menu.contains(document.activeElement) ? menuFrom : document.activeElement;
    closeSub(false);
    menu.innerHTML = "";
    delete menu.dataset.param;
    build();
    menu.hidden = false;
    const w = menu.offsetWidth, h = menu.offsetHeight;
    menu.style.left = clamp(x, EDGE, BASE_W - w - EDGE) + "px";
    menu.style.top = clamp(y, EDGE, BASE_H - h - EDGE) + "px";
    const first = menu.querySelector(":scope > button");
    (focusFirst && first ? first : menu).focus({ preventScroll: true });
  }
  function closeMenu() {
    if (menu.hidden) return;
    const hadFocus = menu.contains(document.activeElement);
    menu.hidden = true;
    closeSub(false);
    menu.innerHTML = "";
    if (hadFocus && menuFrom && menuFrom.isConnected && menuFrom !== document.body) menuFrom.focus({ preventScroll: true });
    else if (hadFocus) menu.blur();
    menuFrom = null;
  }

  /** Where in the stage an event (or an element) is, in stage pixels. */
  const stageXY = (cx, cy) => { const r = stage.getBoundingClientRect(); return [(cx - r.left) / UI.scale, (cy - r.top) / UI.scale]; };

  function menuFor(target, x, y, fromKeys) {
    const ctl = target && target.closest ? target.closest("[data-param]") : null;
    openMenu(x, y, ctl && WT.META[ctl.dataset.param] ? () => controlMenu(ctl) : windowMenu, fromKeys);
  }

  // every right-click: the web view's own menu never shows, ours does (inside the stage)
  document.addEventListener("contextmenu", (e) => {
    e.preventDefault();
    if (menu.contains(e.target)) return;
    const fromKeys = e.clientX === 0 && e.clientY === 0 && e.target !== document.documentElement;
    let x, y;
    if (fromKeys && e.target.getBoundingClientRect) {
      const r = e.target.getBoundingClientRect();
      [x, y] = stageXY(r.left, r.bottom + 4 * UI.scale);
    } else {
      [x, y] = stageXY(e.clientX, e.clientY);
    }
    menuFor(e.target, x, y, fromKeys);
  });
  // from the keyboard: Shift+F10 opens it for the control in focus
  document.addEventListener("keydown", (e) => {
    if (e.key !== "F10" || !e.shiftKey || menu.contains(e.target)) return;
    e.preventDefault();
    const t = document.activeElement && document.activeElement !== document.body ? document.activeElement : stage;
    const r = t.getBoundingClientRect();
    const [x, y] = stageXY(r.left, t === stage ? r.top + 40 : r.bottom + 4 * UI.scale);
    menuFor(t, x, y, true);
  });

  menu.addEventListener("pointerover", (e) => {
    const b = e.target.closest && e.target.closest("button");
    if (sub && b && b.parentNode === menu && b !== subFrom) closeSub(false);
  });
  menu.addEventListener("keydown", (e) => {
    const items = [...menu.querySelectorAll(":scope > button:not([disabled])")];
    const i = items.indexOf(document.activeElement), k = e.key;
    let to = null;
    if (k === "ArrowDown") to = i < 0 ? 0 : (i + 1) % items.length;
    else if (k === "ArrowUp") to = i < 0 ? items.length - 1 : (i + items.length - 1) % items.length;
    else if (k === "Home") to = 0;
    else if (k === "End") to = items.length - 1;
    else if (k === "Escape" || k === "Tab") { e.preventDefault(); e.stopPropagation(); closeMenu(); return; }
    if (to === null || !items.length) return;
    e.preventDefault();
    items[to].focus({ preventScroll: true });
  });
  // Escape anywhere closes it first (and only it: the tour under it stays)
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && !menu.hidden) { closeMenu(); e.stopImmediatePropagation(); }
  });
  // a press outside closes it (a right-press too: the contextmenu that follows reopens it there)
  document.addEventListener("pointerdown", (e) => { if (!menu.hidden && !menu.contains(e.target)) closeMenu(); }, true);
  window.addEventListener("resize", closeMenu);
  window.addEventListener("blur", closeMenu);

  // ---------------------------------------------------------------- About
  const aboutPanel = $("about");
  const about = {
    get open() { return !aboutPanel.hidden; },
    set(open) {
      if (open === this.open) return;
      if (open) { about.from = document.activeElement; aboutPanel.hidden = false; $("about-close").focus({ preventScroll: true }); return; }
      const hadFocus = aboutPanel.contains(document.activeElement);
      aboutPanel.hidden = true;
      if (hadFocus && about.from && about.from.isConnected && about.from !== document.body) about.from.focus({ preventScroll: true });
    },
  };
  $("about-close").addEventListener("click", (e) => { e.stopPropagation(); about.set(false); });
  document.addEventListener("pointerdown", (e) => { if (about.open && !aboutPanel.contains(e.target) && !menu.contains(e.target)) about.set(false); }, true);
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape" && about.open) { about.set(false); e.stopImmediatePropagation(); }
  });

  /** For the tour, the UI check and review screenshots (#menu, #menu=<param>, #about). */
  UI.menu = {
    get open() { return !menu.hidden; },
    openAt(x, y, target) { menuFor(target || stage, x, y, false); },
    close: closeMenu,
    about,
  };
  const hash = new URLSearchParams(location.hash.slice(1));
  if (hash.has("about")) about.set(true);
  if (hash.has("menu")) {
    const id = hash.get("menu"), ctl = id && document.querySelector(`[data-param="${id}"]`);
    if (ctl) {
      const r = ctl.getBoundingClientRect(), [x, y] = stageXY(r.left + r.width / 2, r.top + r.height / 2);
      menuFor(ctl, x, y, false);
    } else {
      UI.menu.openAt(560, 300);
    }
  }
})();
