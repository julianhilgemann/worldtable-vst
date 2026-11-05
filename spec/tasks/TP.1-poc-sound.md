# TP.1 — Proof of concept: world data → wavetable → sound → UI
**Milestone:** POC (vertical slice through M1–M5)   **Depends on:** T0.1   **Branch:** feat/poc-sound

> **Superseded direction (2026-11-05).** v1 is now a magnetosphere drone
> played by a Sky Model (ADR-0002). This file records the POC as built;
> milestone numbers and spec references below are from the old plan. What is
> reused or retired: `spec/07-implementation-plan.md`.

## Goal
Prove the whole idea end to end before building each milestone properly:
live data comes in from a public API, is bent into a wavetable, is audible
through a playable synth voice and FX chain, and can be shaped in a UI in
the FabFilter style (Pro-R look: yellow on dark blue).

Requested by the owner on 2026-11-04 ("let's see the POC work, then add features").

## Scope
- **Data → table:** two sources, both keyless, with a bundled "factory world" so
  everything works offline.
  - `wavespec`: NOAA NDBC buoy spectral wave density (`data/realtime2/<stn>.data_spec`),
    one spectrum every 30 min → **builder B (data-as-harmonics)**. Frame = one
    spectrum; 64 frames ≈ 32 h of swell. Stations 46026, 51201, 44013, 41010.
  - `geomag`: USGS geomagnetism, H component, 1-min (`geomag.usgs.gov/ws/data`)
    → **builder A (series-as-cycle)**. Stations BOU (Boulder), CMO (College, AK — auroral).
  - Transform controls (rebuild + crossfade): Spread, Tilt, Contrast, Phase.
- **Voice:** wavetable osc (64 × 2048, per-octave mipmaps, frame morph, unison ≤ 6,
  detune, stereo width), Drive (ADAA tanh + asymmetry for even harmonics),
  SVF filter (LP12/LP24/BP/HP) with key tracking, amp ADSR, filter ADSR.
- **Modulation:** 2 LFOs (sine/tri/saw/square/S&H/drift), each with one
  destination (position, cutoff, pitch, drive, amp, pan) and bipolar amount.
  The full mod matrix (spec 2.6) is not part of the POC.
- **Triggering:** Drone (held chord from root/scale/chord), Euclid
  (steps/pulses/rotate/rate/gate/range/note pool/mutate; host-synced when the
  transport runs, free-running otherwise), MIDI.
- **FX:** stereo ping-pong delay (tempo-synced) → FDN reverb (8 lines,
  modulated, damping, pre-delay) with shimmer (+12 st in the loop) → lookahead
  limiter (ceiling −1 dBFS default) → output trim.
- **UI:** JUCE 8 WebView, plain HTML/CSS/JS in `source/ui/`, parameter binding
  over JUCE's relay protocol, live telemetry (positions, envelopes, LFOs,
  sequencer step, meters, analyzer). Runs in a normal browser with mock data
  for fast iteration.
- **Tooling:** `tools/gen_params.py` (T0.2 pulled forward),
  `WorldtableRender` (offline render to WAV, T1.5 pulled forward).

## Deliberate deviations from the spec (owner to confirm)
1. **Euclid sequencer.** `00-vision.md` lists "no sequencer or arpeggiator" as a
   v1 non-goal. The owner asked for it explicitly; kept small and optional (Drone is default).
2. **One oscillator per voice** (spec: Osc A + B, sub, noise). B, sub and noise come later.
3. **UI technology:** WebView, as recommended in `03-architecture.md`. ADR-0004 is
   *not* written; this POC is evidence for it, not the decision.
4. **`DataSource.h` not created.** Spectra need a vector per timestamp; the
   scalar `DataReading` contract is still an open question (02-data-sources.md).
   POC adapters are standalone pure parsers in `source/data/`.
5. **Parameter set rewritten** in `spec/parameters.yaml` to match the POC.
   Nothing has been released, so IDs are still free to change.
6. Table crossfade is 1.5 s; data-driven slews ≥ 2 s are not applicable yet
   (no data mod sources in the POC).

## Allowed paths
spec/parameters.yaml, spec/tasks/TP.1-*, tools/gen_params.py, tools/render/*,
source/**, resources/world/**, tests/**, CMakeLists.txt, tests/CMakeLists.txt,
.github/workflows/build.yml (CI fix for this PR, 2026-11-05).

## Acceptance criteria
- [x] Builds VST3 / AU / Standalone with 0 warnings (Debug + Release).
- [x] ctest green, 22/22 (Debug + Release): parsers, factory world, builder (RMS,
      mip band-limits, determinism), euclid, scales, chords, table exchange, limiter
      (ceiling, transparency), ADSR timing, processor renders at 44.1/48/96 kHz,
      every source, extreme settings, MIDI silence, state round-trip.
- [x] Offline render: no NaN/inf, peak ≤ −1 dBFS, sound in Drone and Euclid.
- [x] Default patch (no MIDI) plays a drone made from the bundled ocean data.
- [x] Live fetch replaces the factory world with a 1.5 s crossfade (NDBC + USGS fetched,
      cached to `~/Library/Application Support/Worldtable/cache/`).
- [x] UI: `WorldtableUICheck` opens the real editor and verifies over
      `evaluateJavascript` that the backend is attached, the table and telemetry
      arrive, and parameters sync C++ → JS and JS → C++. 0 JS errors, 0 assertions.
- [x] pluginval strictness 8: VST3 + AU SUCCESS. auval: AU VALIDATION SUCCEEDED.

## How to try it
```sh
cmake --preset release -DWORLDTABLE_COPY_PLUGINS=ON && cmake --build --preset release   # installs AU + VST3
open build/release/Worldtable_artefacts/Release/Standalone/Worldtable.app
build/release/WorldtableRender_artefacts/Release/WorldtableRender --out renders/x.wav --seconds 45 \
    --set trig_mode=Euclid --set wt_source=5          # headless render (add --live to fetch)
open source/ui/index.html#trig_mode=1                 # UI with mock data, no build needed
WORLDTABLE_UI_DIR=$PWD/source/ui <host>               # plug-in serves the UI from disk (live edit)
build/dev/WorldtableUICheck_artefacts/Debug/WorldtableUICheck.app/Contents/MacOS/WorldtableUICheck
```

## Results (2026-11-04, macOS 13.5 arm64, Release)
- CPU ≈ 2 % of one core at 48 kHz (default drone, 4 voices × 3 unison + FX).
- Default drone: −17.8 dBFS RMS, peak −3.8 dBFS (limiter idle). 1-note drone −19 dBFS
  (equal-power drone velocity). Euclid with short envelopes ≈ −29 dBFS RMS.
- Ocean table: partials 3–4 dominate (the 0.068 Hz swell peak becomes a formant);
  moving Position walks through 32 h of swell.
- Renders for listening: `renders/tp1-*.wav` (git-ignored).

## Bugs found and fixed on the way
- Fractional delay reads (reverb + delay) could index one past the end when a
  float position wrapped to exactly `size`. With silent input it injected heap
  garbage into the reverb (flaky "MIDI mode is silent" test). Reads now wrap
  integer indices only.
- `juce::Time (…, useLocalTime = false)` applied the local UTC offset on macOS:
  NDBC times were one hour late. Replaced by pure days-from-civil arithmetic.
- Non-ASCII `char*` literals (choice names with "·", "m²/Hz") trip JUCE's String
  assertion; `gen_params.py` now emits `String::fromUTF8` with octal escapes.
- WebView: the resource provider receives the path, not the full URL; and events
  emitted before the page loads raise JS errors (126 Debug assertions from relay
  initial updates). Fixed with a `uiReady` handshake: attachments are created and
  streaming starts only once the page has registered its listeners.
- Host check in Ableton Live 12.1.11 (VST3): when the feed is offline, the long
  status line (nowrap) widened `#stage`'s implicit auto grid column past 1200 px and
  pushed Filter / LFO 2 / Output out of the window. Fixed with
  `grid-template-columns: minmax(0, 1fr)` + ellipsis on the status text. Reproduce in
  the browser with `index.html#status=offline` (new mock option).
  The Live instance was offline because Little Snitch had no rule for Live yet —
  the fallback to cache worked as intended.
- CI on PR #2 (2026-11-05):
  - **macOS** hung in Build until GitHub's 6 h cap. A bare `cmake --build --parallel`
    is an unlimited `make -j` with the Makefile generator; TP.1 added two more targets
    that each compile all of JUCE (4 × 22 units), which exhausts the 7 GB runner. The
    log of the cancelled job was not kept, so this is inferred: T0.1 already took
    20–54 min for half the units, while Windows (MSBuild, capped) takes 5–8 min.
    Fixed with `--parallel 3` and `timeout-minutes: 60`.
  - **Windows** failed to compile: JUCE only offers `withResourceProvider` on Windows
    with the WebView2 backend (`JUCE_USE_WIN_WEBVIEW2`), which is not wired. The editor
    now builds the web UI only where it is available (`WORLDTABLE_WEB_UI`, defaults to
    `JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE`) and otherwise paints a placeholder.
    The placeholder path was compiled on macOS with `-DWORLDTABLE_WEB_UI=0` (0 warnings).
  - MSVC warning C4458: local `playHead` in `processBlock` hid the JUCE member; renamed.

## Open questions (owner)
1. **Sound check** — listen to `renders/tp1-*.wav` / the plug-in: is the ocean
   formant musical enough, or should the spectrum map onto partials differently
   (e.g. log-frequency mapping, or partial = bin index)?
2. Euclid notes are uniform-random in the pool (with Mutate). Random walk / data-driven
   note choice would be more melodic — next iteration?
3. Euclid is ~10 dB quieter than the drone with short envelopes. Auto-gain per mode?
4. Default slews: table crossfade 1.5 s (spec asks ≥ 1 s) is OK; data-driven *modulation*
   (D1…D8, ≥ 2 s slew) is not in the POC yet.
5. ADR-0004 (WebView) can now be written with evidence; `DataSource.h` contract needs a
   decision for vector-valued sources (spectra) before more adapters are added.
6. Windows: the plug-in compiles in CI, but shows a placeholder instead of the UI. The real
   UI needs `NEEDS_WEBVIEW2 TRUE` + `JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1` and the
   Microsoft.Web.WebView2 SDK (NuGet) in the build — a new dependency (owner approval),
   best as its own task, tested on a Windows machine.
