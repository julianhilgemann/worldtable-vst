#include "SkyRuntime.h"

#include <algorithm>

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

void SkyRuntime::prepare (double sampleRate) noexcept
{
    clock.prepare (sampleRate);
}

void SkyRuntime::process (int numSamples) noexcept
{
    pickUpTrack();
    applyRequests();

    for (int pos = 0; pos < numSamples;)
    {
        if (clock.tickDue())
        {
            publish (sky.step());
            clock.ticked();
        }

        const int n = std::min (numSamples - pos, clock.samplesToNextTick());
        clock.advance (n);
        pos += n;
    }
}

void SkyRuntime::pickUpTrack() noexcept
{
    const int next = exchange.take();
    if (next < 0)
        return;

    sky.setTimeline (&exchange.slot (next).timeline);
    if (currentSlot >= 0)
        exchange.release (currentSlot);   // the model no longer points into it
    currentSlot = next;
}

void SkyRuntime::applyRequests() noexcept
{
    const double speed = requestedSpeed.load (std::memory_order_relaxed);
    if (speed < sky.speed() || speed > sky.speed())
        sky.setSpeed (speed);

    const auto requests = seekRequests.load (std::memory_order_acquire);
    if (requests != seeksApplied)
    {
        seeksApplied = requests;
        sky.seek (requestedSeek.load (std::memory_order_relaxed));
    }
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
