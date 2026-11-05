# ADR-0005 — Licence and distribution

**Status:** Direction accepted (owner, session 2, 2026-11-04); licence text proposed

## Context
Worldtable is built for the owner, intrinsically motivated, with friends who
produce wanting to try it. There is no monetisation and no target-group
research. It is the first of a family of sister plug-ins on the same frame.

## Decision
- **Open source, no monetisation.** Public repository, free binaries.
- **Proposed licence: AGPLv3.** The pinned JUCE 8 (`8.0.17-security`) is
  dual-licensed AGPLv3 / commercial (`_deps/juce-src/LICENSE.md`). Without a
  commercial JUCE licence the plug-in must be AGPLv3-compatible, and AGPLv3 for
  the whole repo is the simplest consistent choice. Owner to confirm before the
  first public release, then add `LICENSE` at the repo root.
- **Data must be redistributable**, not commercially licensable: a source
  qualifies when its terms allow bundling recorded data in a public repo and
  in the plug-in, with credit. US federal data (USGS, NOAA, NASA) is public
  domain; CC BY data needs attribution in the About panel and README.

## Consequences
- The old ground rule "commercial-safe data" is replaced by "redistributable
  data" (`spec/02-data-sources.md`).
- Steinberg's VST3 SDK terms and macOS signing/notarisation still apply to
  distributed binaries (M8).
- Non-commercial-only (NC) data cannot be bundled in an AGPL repo without care;
  check each additional magnetometer station's terms before adding it.
