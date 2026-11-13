# CLAUDE.md — rules for agents working on Worldtable

Read `spec/00-vision.md` → `spec/03-architecture.md` → the spec file your task
names → the task file. Never start coding without a task file in `spec/tasks/`.
This repo leads (owner, 2026-11-13): `spec/` is the source of truth for
direction. The owner's phone-side project brief (Claude Docs) is where the
early vision came from; do not wait for it or sync against it unless asked.

## Hard rules (non-negotiable)

1. **Audio thread is sacred.** Inside `processBlock` and anything it calls
   (`source/dsp/**`, and the Sky Model step in `source/sky/**`, ADR-0007): no
   heap allocation, no locks/mutexes, no file or network I/O, no logging, no
   `std::string` building, no exceptions, no unbounded loops. Data arrives only
   via `WorldSnapshot` and pre-built raw-field tables, both swapped atomically.
   The UI and OSC see the sky only through published `SkySnapshot`s.
2. **Network lives in `source/data/` only**, on background threads. Every
   source must work offline from cache/fixtures.
3. **Parameters are generated.** Never hand-edit `source/params/*`. Change
   `spec/parameters.yaml` and run `tools/gen_params.py`. Parameter IDs are
   forever once released (DAW sessions depend on them).
4. **No sudden sound.** Every value reaching the DSP is smoothed. Any change in
   data, table, chord, or preset must be inaudible as a click. Default slew
   ≥ 2 s for data-driven targets. The visuals follow the same rule.
5. **Deterministic.** Unit tests use `tests/fixtures/`, never live APIs. All
   randomness is seeded: same seed + same data + same sample rate → same
   output, whatever the block size.
6. **Ask before** adding a dependency, changing a protected interface
   (`DataSource.h`, `WorldSnapshot.h`, `SkySnapshot.h`, `ModMatrix.h`), or
   writing an ADR-level decision.

## Definition of done for any task
- Builds on macOS (AU + VST3 + Standalone) with no new warnings.
- `ctest` green; new code has unit tests.
- If DSP changed: offline render via `WorldtableRender` (`tools/render/`)
  produces no NaN/inf, peak ≤ −1 dBFS, no discontinuities (test `NoClicks`).
- If the Sky Model changed: determinism and block-size tests pass; goldens
  change only on purpose, with the reason in the task file.
- `pluginval --strictness-level 8` passes (locally until CI runs it, T0.3).
- Task file updated with what was done and any open questions.

## Style
C++20, JUCE 8.x (pinned tag in CMake), `juce::dsp` where sensible.
Small headers with interfaces first; implementation follows the header the
spec defined. Prefer clarity over cleverness — this is ambient, not a CPU race.
