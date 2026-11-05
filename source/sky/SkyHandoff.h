#pragma once

#include "SkyModel.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace worldtable::sky
{

/** Newest-value hand-off from one writer thread to one reader thread (ADR-0003, hand-off B).

    Three slots: the writer fills its back slot and swaps it with the middle one; the reader swaps
    the middle one to its front slot when a fresh value is there and reads the front at leisure.
    Both sides are wait-free, nothing allocates, and a read is never torn. A slow reader skips
    values; the writer never waits. */
template <typename T>
class TripleBuffer
{
public:
    //== Writer =====================================================================================
    T& back() noexcept { return slots[writerSlot]; }

    /** Makes back() the newest value. */
    void publish() noexcept
    {
        const auto old = middle.exchange (static_cast<uint8_t> (writerSlot | kFresh), std::memory_order_acq_rel);
        writerSlot = old & kIndexMask;
    }

    void write (const T& value) noexcept
    {
        back() = value;
        publish();
    }

    //== Reader =====================================================================================
    /** Moves the newest value to front() if one arrived since the last call; returns whether it did. */
    bool update() noexcept
    {
        if ((middle.load (std::memory_order_acquire) & kFresh) == 0)
            return false;

        const auto old = middle.exchange (readerSlot, std::memory_order_acq_rel);
        readerSlot = old & kIndexMask;
        return true;
    }

    const T& front() const noexcept { return slots[readerSlot]; }

private:
    static constexpr uint8_t kIndexMask = 0x3, kFresh = 0x4;

    std::array<T, 3> slots {};
    alignas (64) uint8_t writerSlot = 0;
    alignas (64) uint8_t readerSlot = 1;
    alignas (64) std::atomic<uint8_t> middle { 2 };

    static_assert (std::atomic<uint8_t>::is_always_lock_free);
};

/** A fixed-size queue from one producer thread to one consumer thread. Wait-free; a push into a
    full queue fails. */
template <typename T, size_t Capacity>
class SpscQueue
{
public:
    static_assert (Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");

    bool push (const T& value) noexcept
    {
        const auto w = writeIndex.load (std::memory_order_relaxed);
        if (w - readIndex.load (std::memory_order_acquire) == Capacity)
            return false;
        items[w & (Capacity - 1)] = value;
        writeIndex.store (w + 1, std::memory_order_release);
        return true;
    }

    bool pop (T& out) noexcept
    {
        const auto r = readIndex.load (std::memory_order_relaxed);
        if (r == writeIndex.load (std::memory_order_acquire))
            return false;
        out = items[r & (Capacity - 1)];
        readIndex.store (r + 1, std::memory_order_release);
        return true;
    }

    size_t size() const noexcept
    {
        return static_cast<size_t> (writeIndex.load (std::memory_order_acquire) - readIndex.load (std::memory_order_acquire));
    }

private:
    std::array<T, Capacity> items {};
    alignas (64) std::atomic<uint64_t> writeIndex { 0 };
    alignas (64) std::atomic<uint64_t> readIndex { 0 };

    static_assert (std::atomic<uint64_t>::is_always_lock_free);
};

/** The tick events for one reader (ADR-0003): onsets, phase changes and whistlers in order. When
    the reader falls 256 events behind, new events are dropped and counted. */
class SkyEventFifo
{
public:
    static constexpr size_t kCapacity = 256;

    /** Audio thread. */
    void push (const SkyEvent& event) noexcept
    {
        if (! queue.push (event))
            dropped.fetch_add (1, std::memory_order_relaxed);
    }

    /** Reader thread: up to `max` events, oldest first; returns how many. */
    int pop (SkyEvent* dest, int max) noexcept
    {
        int n = 0;
        while (n < max && queue.pop (dest[n]))
            ++n;
        return n;
    }

    uint32_t droppedCount() const noexcept { return dropped.load (std::memory_order_relaxed); }

private:
    SpscQueue<SkyEvent, kCapacity> queue;
    std::atomic<uint32_t> dropped { 0 };
};

} // namespace worldtable::sky
