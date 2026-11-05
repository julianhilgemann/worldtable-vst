#pragma once

#include "SkyHandoff.h"
#include "SkyModel.h"
#include "dsp/SlotExchange.h"

#include <atomic>
#include <cstdint>
#include <span>

namespace worldtable::sky
{

/** A track and its slow sky, built together off the audio thread (ADR-0003, hand-off A). The
    timeline points at the track in the same bundle. */
struct TrackBundle
{
    data::WorldSnapshot track;
    SkyTimeline timeline;
};

/** Current, pending and one being built. */
using TrackExchange = dsp::SlotExchange<TrackBundle, 3>;

/** The Sky Model inside the plug-in (ADR-0007, ADR-0003).

    The audio thread calls process() for every block: it picks up a newly published track, applies
    speed and seek requests, steps the model at every 100 Hz tick inside the block (SkyClock, so the
    grid doesn't depend on the block size) and hands each tick's snapshot and events to the readers.
    The UI and the OSC sender each read through their own triple buffer and event queue; they never
    touch the model. */
class SkyRuntime
{
public:
    enum class Reader : int { ui = 0, osc = 1 };
    static constexpr int kNumReaders = 2;

    /** Message thread: allocates the track pool. */
    SkyRuntime();

    /** The track hand-off: the data side fills a slot and publishes it (SkyFeed). */
    TrackExchange& tracks() noexcept { return exchange; }

    //== Any thread: applied at the start of the next block ===========================================
    void setSpeed (double modelSecondsPerSecond) noexcept;
    void seek (double modelTimeUtc) noexcept;

    //== Audio thread =================================================================================
    void prepare (double sampleRate) noexcept;
    void process (int numSamples) noexcept;

    /** The model's own view, for the sound on the audio thread (M3). */
    const SkyModel& model() const noexcept { return sky; }

    //== Readers: each Reader from one thread only ====================================================
    /** Copies the newest snapshot into `out` if one arrived since this reader's last call. */
    bool readSnapshot (Reader, SkySnapshot& out) noexcept;

    /** Up to out.size() events, oldest first; returns how many. */
    int readEvents (Reader, std::span<SkyEvent> out) noexcept;

    /** Events this reader lost because it fell behind. */
    uint32_t droppedEvents (Reader) const noexcept;

private:
    void pickUpTrack() noexcept;
    void applyRequests() noexcept;
    void publish (std::span<const SkyEvent>) noexcept;

    SkyModel sky;
    SkyClock clock;
    TrackExchange exchange;
    int currentSlot = -1;

    std::array<TripleBuffer<SkySnapshot>, kNumReaders> snapshots;
    std::array<SkyEventFifo, kNumReaders> events;

    std::atomic<double>   requestedSpeed { 0.0 };
    std::atomic<double>   requestedSeek { 0.0 };
    std::atomic<uint32_t> seekRequests { 0 };
    uint32_t              seeksApplied = 0;

    static_assert (std::atomic<double>::is_always_lock_free);
};

} // namespace worldtable::sky
