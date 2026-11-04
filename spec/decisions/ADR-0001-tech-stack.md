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

## Consequences
- Licence: JUCE requires either GPLv3 for the plugin or a JUCE commercial
  licence (free tier has a revenue cap) — decide before any public release.
- VST3 SDK is bundled with JUCE; distributing VST3 binaries requires accepting
  Steinberg's licence terms.
- macOS distribution needs Developer ID signing + notarisation.

## Open ADRs to write in Phase 1
- ADR-0002 Wavetable generation method(s) for v1
- ADR-0003 Snapshot hand-off mechanism (triple buffer vs atomic shared_ptr)
- ADR-0004 UI technology (WebView vs native components)
- ADR-0005 Licence & distribution (open source vs commercial)
