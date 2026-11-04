# CLAUDE.md — rules for agents working on Worldtable

Read `spec/README` order: `00-vision` → `03-architecture` → the task file you were given.
Never start coding without a task file in `spec/tasks/`.

## Hard rules (non-negotiable)

1. **Audio thread is sacred.** Inside `processBlock` and anything it calls
   (`source/dsp/**`): no heap allocation, no locks/mutexes, no file or network
   I/O, no logging, no `std::string` building, no exceptions, no unbounded loops.
   Data arrives only via `WorldSnapshot` (atomic pointer swap) and pre-built
   wavetables (double-buffered, swapped atomically).
2. **Network lives in `source/data/` only**, on background threads. Every
   source must work offline from cache/fixtures.
3. **Parameters are generated.** Never hand-edit `source/params/*`. Change
   `spec/parameters.yaml` and run `tools/gen_params.py`. Parameter IDs are
   forever once released (DAW sessions depend on them).
4. **No sudden sound.** Every value reaching the DSP is smoothed. Any change in
   data, table, or preset must be inaudible as a click. Default slew ≥ 2 s for
   data-driven targets.
5. **Deterministic tests.** Unit tests use `tests/fixtures/`, never live APIs.
6. **Ask before** adding a dependency, changing a public interface in
   `*.h` under `source/data/DataSource.h`, `WorldSnapshot.h`, `ModMatrix.h`,
   or writing an ADR-level decision.

## Definition of done for any task
- Builds on macOS (AU + VST3 + Standalone) with no new warnings.
- `ctest` green; new code has unit tests.
- If DSP changed: offline render via `tools/render_offline` produces no NaN/inf,
  peak ≤ −1 dBFS, no discontinuities (test `NoClicks`).
- `pluginval --strictness-level 8` passes (CI).
- Task file updated with what was done and any open questions.

## Style
C++20, JUCE 8.x (pinned tag in CMake), `juce::dsp` where sensible.
Small headers with interfaces first; implementation follows the header the
spec defined. Prefer clarity over cleverness — this is ambient, not a CPU race.
