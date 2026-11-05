# ADR-0001 — Tech stack

**Status:** Proposed (confirm in Phase 1)

## Decision
- C++20, **JUCE 8.x** pinned to a release tag via CPM/FetchContent.
- Build: CMake (no Projucer) — agents handle CMake far better than .jucer files.
- Formats: VST3, AU, Standalone; CLAP later via clap-juce-extensions.
- Tests: Catch2 v3 + custom offline render harness; pluginval in CI.
- UI: JUCE WebView editor (HTML/CSS/JS), see ADR-0004 once written.
- Networking: `juce::URL` / `WebInputStream` on a dedicated `juce::Thread`
  (no extra HTTP dependency in v1).
- JSON: `juce::JSON` (no extra dependency in v1).
- OSC: `juce_osc` (part of JUCE, no extra dependency).

## Consequences
- Licence: JUCE 8 is dual-licensed AGPLv3 / commercial; the project is open
  source, so AGPLv3 is proposed (ADR-0005).
- VST3 SDK is bundled with JUCE; distributing VST3 binaries requires accepting
  Steinberg's licence terms.
- macOS distribution needs Developer ID signing + notarisation.

## ADR index
- ADR-0002 v1 direction: magnetosphere drone played by a Sky Model — accepted
- ADR-0003 Hand-offs between threads: tracks in, snapshots out — proposed
- ADR-0004 UI technology (WebView vs native components) — to write, POC is the evidence
- ADR-0005 Licence & distribution — open source accepted, AGPLv3 proposed
- ADR-0006 Observer: all-sky dome above a magnetometer station — accepted
- ADR-0007 Sky Model clock and determinism — accepted
