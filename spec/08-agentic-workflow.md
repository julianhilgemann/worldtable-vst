# 08 — How to build this with agents

## Where ideas come from
The owner iterates ideas on the phone in the Claude Docs project brief
("Worldtable — project brief": vision, sessions, Idea inbox). The brief leads;
this spec follows it. On the laptop:
1. Read the brief's newest sessions and Idea inbox.
2. Move settled decisions into `spec/` (an ADR for anything irreversible).
3. Cut the next step into task files; build; tick the item in the brief and
   update its "Where we are" when work merges.

## The loop
1. **Spec → task.** Cut a milestone into tasks of ≤ ~400 lines of change
   each. One task = one branch = one PR.
2. **Interface first.** For any new module the first commit is only the
   header + tests (red). The owner reviews the header. Then implement to green.
3. **Implement** inside the task's allowed paths only.
4. **Machine checks** (CI): build, unit tests, render tests, pluginval.
5. **Ear and eye check (owner).** Agents can't judge "beautiful". Every sound
   PR ships a render (`renders/<task>.wav`, 60 s, or a 5-minute Journey
   bounce); every visual PR ships a screenshot or a short capture.
6. **Update the spec** if reality changed the plan (spec is living, ADRs are not).

## Where agents are strong / weak here
| Strong | Weak — keep the owner in the loop |
|---|---|
| CMake/JUCE boilerplate, APVTS, codegen | Whether it sounds and looks good |
| Adapters, parsers, fixtures | Real-time safety subtleties (review every DSP diff) |
| Unit tests, render harness, CI | Host quirks (TouchDesigner, DAWs) |
| Web UI, shaders, prototypes | Data licences |

## Parallelism
Safe in parallel (separate worktrees): data adapters ∥ Sky Model ∥ sound ∥ UI.
They meet at four interfaces — `DataSource.h`, `WorldSnapshot.h`,
`SkySnapshot.h`, `parameters.yaml` — so freeze those first.

## Task file template (`spec/tasks/T5.1-datasource-geomag.md`)
```markdown
# T5.1 — USGS geomag adapter
**Milestone:** M5   **Depends on:** T2.1   **Branch:** feat/t5.1-geomag
## Goal
Implement `UsgsGeomagSource : DataSource` providing `geomag.h` (nT, 1 min)
for a station and a UTC span.
## Allowed paths
source/data/sources/UsgsGeomag.*, tests/unit/UsgsGeomagTests.cpp, tests/fixtures/geomag/*
## Acceptance criteria
- [ ] parse() passes on 3 fixtures (quiet day, 10 May 2024 storm, malformed JSON)
- [ ] gaps marked invalid, not zero-filled
- [ ] no network in tests
## Notes / open questions
(agent fills in)
```

## Prompts to start each step
- **Sync with the brief:** "Read the project brief's sessions since rev N and
  the Idea inbox. List what changes the spec, then update spec/ and propose
  task files."
- **Prototype:** "Do spec/tasks/T1.2-*.md. Show me the dome replaying
  10 May 2024 before tuning anything."
- **Port:** "Do spec/tasks/T2.1-*.md. Header + failing tests first, stop for
  my review, then implement."
