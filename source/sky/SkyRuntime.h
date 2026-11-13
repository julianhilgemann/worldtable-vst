#pragma once

#include "SkyHandoff.h"
#include "SkyModel.h"
#include "dsp/SlotExchange.h"

#include <atomic>
#include <cstdint>
#include <limits>
#include <span>

namespace worldtable::sky
{

/** A track and its slow sky, built together off the audio thread (ADR-0003, hand-off A). The
    timeline points at the track in the same bundle. */
struct TrackBundle
{
    data::WorldSnapshot track;
    SkyTimeline timeline;

    /** T6.9: where the model lands when the audio thread picks this track up (SkyRuntime::land), as
        one seek request with the track, so a new day never plays a block at the old day's time.
        NaN: the model keeps its time (clamped to the track). */
    double landingUtc = std::numeric_limits<double>::quiet_NaN();
    uint32_t landingTicket = 0;   ///< the seek request this landing answers (SkyRuntime's count)
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

    /** What runs on the sky's ticks inside a block: the sound (M3). Audio thread, real-time safe. */
    class TickListener
    {
    public:
        virtual ~TickListener() = default;

        /** After each model step: the model, the tick's events and the tick's length in samples. */
        virtual void skyTicked (const SkyModel&, std::span<const SkyEvent>, int tickSamples) noexcept = 0;

        /** Samples [offset, offset + numSamples) of the block, all inside the latest tick. */
        virtual void renderSamples (int offset, int numSamples) noexcept = 0;
    };

    /** Message thread: allocates the track pool. */
    SkyRuntime();

    /** The track hand-off: the data side fills a slot and publishes it (SkyFeed). */
    TrackExchange& tracks() noexcept { return exchange; }

    /** Producer thread (T6.9): publishes a filled slot and lands the model at `utc` when the audio
        thread picks it up, as one seek. Seeks asked for before this call are dropped (they were
        meant for the old track); seeks asked for after it apply on the new track. */
    void land (int slot, double utc) noexcept;

    //== Any thread: applied at the start of the next block ===========================================
    void setSpeed (double modelSecondsPerSecond) noexcept;
    void seek (double modelTimeUtc) noexcept;

    /** T6.9: at the track's end, start again from its beginning (a seek, so the sound re-bases)
        instead of holding there. Decided on the tick grid, so it doesn't depend on the block size. */
    void setLoop (bool loop) noexcept;

    //== Any thread: what the audio thread last did ==================================================
    /** Model time after the last block, or the target of a seek or landing not applied yet (so a
        state saved right after a restore, before any audio, saves the restored time). */
    double modelTimeNow() const noexcept;

    /** Model time has reached the track's end (it holds there unless looping). */
    bool atTrackEnd() const noexcept { return endReached.load (std::memory_order_relaxed); }

    //== Audio thread =================================================================================
    void prepare (double sampleRate) noexcept;

    /** Steps the sky through one block. A listener hears every tick at its sample: skyTicked()
        after each step, renderSamples() for the samples up to the next tick. */
    void process (int numSamples, TickListener* listener = nullptr) noexcept;

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
    void applyRequests (uint32_t requests) noexcept;
    bool modelAtTrackEnd() const noexcept;
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
    std::atomic<bool>     loopRequested { false };
    uint32_t              seeksApplied = 0;
    bool                  looping = false;

    // written by the audio thread after each block, read anywhere
    std::atomic<double>   timeNow { 0.0 };
    std::atomic<uint32_t> seeksDone { 0 };
    std::atomic<bool>     endReached { false };

    static_assert (std::atomic<double>::is_always_lock_free);
    static_assert (std::atomic<bool>::is_always_lock_free);
};

} // namespace worldtable::sky
