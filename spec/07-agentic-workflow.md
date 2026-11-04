# 07 — How to build this with agents

## The loop
1. **Spec → task.** You (or a planning agent) cut a milestone into tasks of
   ≤ ~400 lines of change each. One task = one branch = one PR.
2. **Interface first.** For any new module the first commit is only the header
   + tests (red). You review the header. Then the agent implements to green.
3. **Agent implements** inside its task's allowed paths only.
4. **Machine checks** (CI): build, unit tests, render tests, pluginval.
5. **Ear check (you).** Agents can't judge "beautiful". Every DSP PR ships a
   rendered WAV (`renders/<task>.wav`, 60 s) — you listen before merging.
6. **Update spec** if reality changed the plan (spec is living, ADRs are not).

## Where agents are strong / weak here
| Strong | Weak — keep a human (you) in the loop |
|---|---|
| CMake/JUCE boilerplate, APVTS, codegen | Whether it sounds good |
| API adapters + parsers + fixtures | Real-time safety subtleties (review every DSP diff) |
| Unit tests, render harness, CI | Host-specific quirks (test in your DAWs) |
| HTML mock & WebView UI | Licensing of data sources |

## Parallelism
Safe to run in parallel (separate worktrees): data adapters ∥ DSP modules ∥ UI mock.
They only meet at three interfaces — `DataSource.h`, `WorldSnapshot.h`,
`parameters.yaml` — so freeze those first (end of M0/M1).

## Task file template (`spec/tasks/T2.1-datasource-quake.md`)
```markdown
# T2.1 — USGS quake adapter
**Milestone:** M2   **Depends on:** T0.1   **Branch:** feat/t2.1-quake
## Goal
Implement `UsgsQuakeSource : DataSource` providing channels
`quake.count_1h`, `quake.max_mag_1h`, `quake.energy_1h`.
## Allowed paths
source/data/sources/UsgsQuake.*, tests/unit/UsgsQuakeTests.cpp, tests/fixtures/quake/*
## Acceptance criteria
- [ ] parse() passes on 3 fixtures (quiet hour, busy hour, malformed JSON)
- [ ] energy uses 10^(1.5·M) summation, output in log10 units
- [ ] no network in tests
## Notes / open questions
(agent fills in)
```

## Useful prompts to start each phase
- **Phase 1:** "Read spec/00 and spec/01. Interview me one `DECIDE:` at a time,
  propose a default with reasoning, then write my answers back into the spec and
  draft `parameters.yaml`."
- **Phase 1b:** "For each source in spec/02, verify the endpoint, record one
  real response into tests/fixtures/<id>/, note rate limits and licence."
- **Phase 2:** "Build mock/index.html per spec/05 using params from
  parameters.yaml and data from tests/fixtures. No frameworks."
- **Phase 3:** "Do task spec/tasks/T1.1-*.md. Header + failing tests first, stop
  for my review, then implement."
