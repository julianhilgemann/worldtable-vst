# ADR-0003 — Hand-offs between threads: tracks in, snapshots out

**Status:** Accepted (owner, 2026-11-05). Drafted by Claude Code on the
owner's go-ahead in the T2.1 review; implemented in T2.4.

## Context
Two hand-offs cross the audio thread (`spec/03-architecture.md`):
- **A. Tracks in.** The data thread builds a `WorldSnapshot` (130 KB) and its
  `SkyTimeline` (about 1.1 MB for 72 h, held in `std::vector`s) and gives both
  to the audio thread. New ones arrive rarely: another day, a Live update
  every few minutes, a seed or tuning change.
- **B. Snapshots out.** After every tick (100 Hz) the audio thread gives a
  `SkySnapshot` (4.8 KB) and the tick's events to the UI (30–60 Hz) and the
  OSC sender (about 60 Hz).

The audio thread never allocates, frees, locks or waits (`CLAUDE.md` rule 1).
Readers never see a half-written object. A slow reader may skip frames, but it
never holds up the writer. ADR-0001 left the choice open: triple buffer or
`std::atomic<std::shared_ptr>`.

## Decision
- **No `std::shared_ptr` across the audio thread.** `std::atomic<shared_ptr>`
  is not lock-free (or not available) in the standard libraries we build with,
  and dropping the last reference on the audio thread would free a 1 MB
  timeline there.
- **A. Tracks: a slot exchange, the POC's `TableExchange` pattern.** A
  `TrackBundle` is a `WorldSnapshot` plus its `SkyTimeline`. Three bundles are
  allocated up front (current, pending, one being built; about 3.6 MB at 72 h).
  One atomic word holds the busy mask and the pending slot, so every
  transition is a single compare-and-swap. The data thread fills a free slot
  and publishes it. At block start the audio thread takes it and calls
  `SkyModel::setTimeline()`; the previous slot goes back to the pool once that
  call has returned. Only the data thread allocates or frees, and only in
  slots the audio thread can't reach. `TableExchange` becomes a template
  (`SlotExchange<T, N>`) that the raw-field tables keep using.
- **B. Snapshots: a triple buffer per reader.** Three `SkySnapshot` slots and
  one atomic byte holding the back, middle and front indices plus a "fresh"
  bit. After each step the audio thread copies `SkyModel::current()` into the
  back slot and swaps it with the middle one. A reader swaps the middle slot to
  the front when it is fresh and reads the front at leisure. Both sides are
  wait-free, nothing allocates and no read is torn. One buffer for the UI, one
  for OSC: 29 KB in all, and 2 × 4.8 KB × 100 Hz ≈ 1 MB/s of copying.
- **Events: a FIFO per reader.** A reader that skips frames would miss events,
  so each tick's `SkyEvent`s also go into a fixed-size single-producer,
  single-consumer FIFO per reader (`juce::AbstractFifo`, 256 entries). When a
  FIFO is full the audio thread drops the event and counts the drop. The
  counters in `SkySnapshot` (`onsetCount`, `whistlerCount`) let a reader notice
  gaps.

## Alternatives
- **Seqlock** (one buffer and a version counter; readers retry): smallest, but
  copying a plain struct while it is being written is a data race in C++, and
  a reader can retry for a long time under load.
- **A lock the audio thread only try-locks**: a busy reader costs the audio
  thread a missed publish, and rule 1 bans locks on the audio thread anyway.
- **`std::atomic<std::shared_ptr>`**: rejected above.

## Consequences
- New code: `SlotExchange<T, N>` (generalised from `TableExchange`),
  `TripleBuffer<T>` and the event FIFO, each with unit tests including a
  two-thread stress test. It lands with the first task that runs the Sky Model
  in `processBlock` (T2.4 at the latest).
- The UI and OSC see the sky at most one tick late; events arrive in order.
- Memory: about 3.6 MB for the track pool and 29 KB for the snapshot buffers.
