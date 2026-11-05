# TP.2 — Prototype: ambient animated background (levels 1–3)
**Milestone:** POC   **Depends on:** TP.1   **Branch:** feat/poc-sound

> **Superseded (2026-11-05).** The all-sky dome (ADR-0006) replaces the
> ambient background; level 3's techniques (smoothed targets, phase-integrated
> speed, GPU budget) carry over to the dome renderer.

## Goal
Let the owner compare three ways of adding soft motion behind the UI, in the
browser mock and in a DAW, before deciding what the product gets.
Requested on 2026-11-04 ("sure show me the levels please").

## Scope
- **Level 0 — off:** today's static background.
- **Level 1 — CSS only:** a few large soft radial gradients drifting on
  compositor-only properties (`transform`). No JS per frame, no data.
- **Level 2 — WebGL shader:** one full-window canvas behind the panels, flowing
  domain-warped noise. Palette follows the world source: *ocean* for swell
  sources, *aurora* for magnetosphere sources. Free-running, no data.
- **Level 3 — data-reactive shader:** level 2, driven by what is already sent
  to the page:
  - swell sources: relative swell energy of the current frame → how much the crests
    light up; mean frequency of the spectrum → wave period → the crests roll at
    roughly that period, and longer periods give longer waves;
  - magnetosphere sources: per-frame activity (mean |dH/dt| + range) → curtain
    height, brightness, speed;
  - output peak → warm glow; amp envelope transients → glints; LFO 1 → drift.
  All targets are smoothed (≈0.6–3 s), and speed is integrated into a phase
  so that speed changes never make the picture jump.
- A small prototype switcher in the header (`AMBIENT OFF 1 2 3`), and a
  `#ambient=N` hash for the browser. The choice is per-viewer UI state
  (localStorage when available); **no new plug-in parameters**.
- Mock: a magnetosphere-like table when a magnetosphere source is selected, so
  the aurora palette can be reviewed without a backend.

## Constraints
- No new dependency (plain WebGL 1, which WKWebView on macOS 13 has).
- GPU budget: render at half the CSS resolution, ~30 fps cap, `low-power` GPU
  preference, pause when the page is hidden. Fallback to level 1 if WebGL is
  missing or the context is lost.
- `prefers-reduced-motion` → default off, CSS animation disabled.
- Panels become slightly translucent only while an ambient level is on; the
  display wells (wavetable, filter, …) stay opaque for legibility.

## Allowed paths
source/ui/**, spec/tasks/TP.2-*, tools/uicheck/UiCheck.cpp (WebGL check in the real web view).

## Acceptance criteria
- [x] All four levels render in the browser mock (Chrome) and in the plug-in (WKWebView,
      Standalone; the owner clicked through 1–3).
- [ ] Level 3 visibly follows Position / source / output level, without jumps —
      implemented; palette switch by source verified in the mock; needs the owner's eye.
- [x] No visual jumps by construction: palette crossfade (τ 1.2 s), all targets smoothed,
      speed integrated into a phase, first table snaps instead of morphing on open.
- [x] `WorldtableUICheck` passes, now also checks level 3 (WebGL canvas up, animation
      advancing) in the real web view; ctest 22/22; Release build 0 warnings.
- [ ] CPU cost per level — **not measured yet.** `WorldtableUICheck --soak` holds each
      level 12 s, but WebKit throttles occluded windows, and in both runs the window
      ended up behind other apps (numbers invalid). Run it while not using the Mac.
      One uncontrolled sample from the Standalone (owner clicking levels, front window):
      WebContent ≈ 39 %, WebKit GPU ≈ 30 %, app ≈ 21 % of one core — suspected to be
      mostly the existing 12 canvases redrawn every frame, not the ambient layer.

## Notes
- Panels become translucent (0.62–0.74 instead of 0.92–0.95) only while a level is on.
- Review aids: `index.html#ambient=N`, `&bgonly` (hide panels), `&wt_source=5` (aurora).
- Headless Chrome with `--virtual-time-budget` barely advances rAF; use `--timeout`.

## Decisions
- 2026-11-04, owner: **level 3** is the one ("I like the version 3"). It is already the
  default (no stored choice, no reduced-motion preference).

## Open questions (owner)
1. Keep the `AMBIENT OFF 1 2 3` switcher? Suggest: keep only OFF / ON (level 3) as a small
   setting, since OFF matters for CPU with many instances; drop levels 1–2 from the UI.
2. Should the visual mapping become part of the spec (e.g. ADR next to ADR-0004)?
