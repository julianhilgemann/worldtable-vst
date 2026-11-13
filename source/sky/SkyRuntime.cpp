#include "SkyRuntime.h"

#include <algorithm>
#include <cmath>

namespace worldtable::sky
{

SkyRuntime::SkyRuntime()
{
    clock.prepare (48000.0);
    for (auto& s : snapshots)
        s.write (sky.current());
}

void SkyRuntime::setSpeed (double modelSecondsPerSecond) noexcept
{
    requestedSpeed.store (modelSecondsPerSecond, std::memory_order_relaxed);
}

void SkyRuntime::seek (double modelTimeUtc) noexcept
{
    requestedSeek.store (modelTimeUtc, std::memory_order_relaxed);
    seekRequests.fetch_add (1, std::memory_order_release);
}

void SkyRuntime::land (int slot, double utc) noexcept
{
    // The bundle is visible before its ticket is counted: a block that sees the count also takes
    // the bundle (see process()). seek() and land() come from one thread, so the ticket is exact.
    auto& bundle = exchange.slot (slot);
    bundle.landingUtc = utc;
    bundle.landingTicket = seekRequests.load (std::memory_order_relaxed) + 1;
    exchange.publish (slot);
    requestedSeek.store (utc, std::memory_order_relaxed);
    seekRequests.fetch_add (1, std::memory_order_release);
}

void SkyRuntime::setLoop (bool loop) noexcept
{
    loopRequested.store (loop, std::memory_order_relaxed);
}

double SkyRuntime::modelTimeNow() const noexcept
{
    if (seekRequests.load (std::memory_order_acquire) != seeksDone.load (std::memory_order_acquire))
        return requestedSeek.load (std::memory_order_relaxed);   // asked for, not yet applied
    return timeNow.load (std::memory_order_relaxed);
}

void SkyRuntime::prepare (double sampleRate) noexcept
{
    clock.prepare (sampleRate);
}

void SkyRuntime::process (int numSamples, TickListener* listener) noexcept
{
    const auto requests = seekRequests.load (std::memory_order_acquire);   // before take(): see land()
    pickUpTrack();
    applyRequests (requests);

    for (int pos = 0; pos < numSamples;)
    {
        if (clock.tickDue())
        {
            // T6.9 loop: at the end, back to the track's start, on the tick grid (rule 5). A seek, so
            // the sound re-bases its exciter and the harmony hears where it landed.
            if (looping && sky.speed() > 0.0 && modelAtTrackEnd())
                sky.seek (exchange.slot (currentSlot).track.startUtc);

            const auto tickEvents = sky.step();
            publish (tickEvents);
            clock.ticked();
            if (listener != nullptr)
                listener->skyTicked (sky, tickEvents, clock.samplesToNextTick());
        }

        const int n = std::min (numSamples - pos, clock.samplesToNextTick());
        if (listener != nullptr)
            listener->renderSamples (pos, n);
        clock.advance (n);
        pos += n;
    }

    timeNow.store (sky.modelTimeUtc(), std::memory_order_relaxed);
    endReached.store (modelAtTrackEnd(), std::memory_order_relaxed);
    seeksDone.store (seeksApplied, std::memory_order_release);
}

void SkyRuntime::pickUpTrack() noexcept
{
    const int next = exchange.take();
    if (next < 0)
        return;

    const auto& bundle = exchange.slot (next);
    sky.setTimeline (&bundle.timeline);   // a new track counts as a seek (SkyModel, T6.9)
    if (std::isfinite (bundle.landingUtc))
    {
        sky.seek (bundle.landingUtc);
        // seeks asked for before the landing were meant for the old track
        if (static_cast<int32_t> (bundle.landingTicket - seeksApplied) > 0)
            seeksApplied = bundle.landingTicket;
    }
    if (currentSlot >= 0)
        exchange.release (currentSlot);   // the model no longer points into it
    currentSlot = next;
}

void SkyRuntime::applyRequests (uint32_t requests) noexcept
{
    const double speed = requestedSpeed.load (std::memory_order_relaxed);
    if (speed < sky.speed() || speed > sky.speed())
        sky.setSpeed (speed);

    looping = loopRequested.load (std::memory_order_relaxed);

    if (static_cast<int32_t> (requests - seeksApplied) > 0)   // newer than what was applied
    {
        seeksApplied = requests;
        sky.seek (requestedSeek.load (std::memory_order_relaxed));
    }
}

bool SkyRuntime::modelAtTrackEnd() const noexcept
{
    if (currentSlot < 0)
        return false;
    // where SkyModel::step() stops advancing, in its whole µs
    const auto& track = exchange.slot (currentSlot).track;
    return std::llround (sky.modelTimeUtc() * 1e6) >= std::llround ((track.endUtc() - SkyModel::kSlowStepSeconds) * 1e6);
}

void SkyRuntime::publish (std::span<const SkyEvent> tickEvents) noexcept
{
    for (size_t r = 0; r < snapshots.size(); ++r)
    {
        snapshots[r].write (sky.current());
        for (const auto& e : tickEvents)
            events[r].push (e);
    }
}

bool SkyRuntime::readSnapshot (Reader reader, SkySnapshot& out) noexcept
{
    auto& buffer = snapshots[static_cast<size_t> (reader)];
    if (! buffer.update())
        return false;
    out = buffer.front();
    return true;
}

int SkyRuntime::readEvents (Reader reader, std::span<SkyEvent> out) noexcept
{
    return events[static_cast<size_t> (reader)].pop (out.data(), static_cast<int> (out.size()));
}

uint32_t SkyRuntime::droppedEvents (Reader reader) const noexcept
{
    return events[static_cast<size_t> (reader)].droppedCount();
}

} // namespace worldtable::sky
