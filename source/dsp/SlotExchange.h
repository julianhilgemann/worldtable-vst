#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace worldtable::dsp
{

/** Lock-free hand-off of large objects from one producer thread to the audio thread (ADR-0003):
    wavetables (TableExchange) and sky tracks (sky::TrackExchange).

    A fixed pool of N slots is allocated up front. All shared state lives in one atomic word, so
    every transition is a single compare-and-swap:
      bits 0..7   busy mask   — slots the audio thread is reading (current / fading out)
      bits 8..11  pending + 1 — slot published by the producer, not yet taken (0 = none)

    Producer:  acquireForWriting() → fill slot → publish()
    Audio:     take() at block start → read → release() once it is done with a slot.

    Only the producer writes into slots, only into slots the audio thread can't reach, and nothing
    is ever allocated or freed after construction. Exactly one producer thread and one audio thread
    may use an instance. */
template <typename T, int N>
class SlotExchange
{
public:
    static_assert (N >= 2 && N <= 8, "the busy mask has 8 bits");
    static constexpr int kNumSlots = N;

    SlotExchange()
    {
        for (auto& s : slots)
            s = std::make_unique<T>();
    }

    T& slot (int index) noexcept             { return *slots[static_cast<size_t> (index)]; }
    const T& slot (int index) const noexcept { return *slots[static_cast<size_t> (index)]; }

    //== Producer thread =======================================================
    /** Returns a slot the audio thread is not using and cannot take, or -1 if every
        slot is busy (try again later). An unconsumed pending slot is reclaimed. */
    int acquireForWriting() noexcept
    {
        auto s = state.load (std::memory_order_acquire);

        while (pendingOf (s) >= 0)
        {
            const auto cleared = s & ~kPendingMask;
            if (state.compare_exchange_weak (s, cleared, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                s = cleared;
                break;
            }
        }

        // From here the audio thread can only clear busy bits, never set them
        // (nothing is pending), so a slot seen free stays free.
        for (int i = 0; i < kNumSlots; ++i)
            if ((s & (1u << i)) == 0)
                return i;

        return -1;
    }

    void publish (int index) noexcept
    {
        auto s = state.load (std::memory_order_relaxed);
        for (;;)
        {
            const auto next = (s & ~kPendingMask) | (static_cast<uint32_t> (index + 1) << kPendingShift);
            if (state.compare_exchange_weak (s, next, std::memory_order_release, std::memory_order_relaxed))
                return;
        }
    }

    //== Audio thread ==========================================================
    /** Takes the pending slot (marking it busy) or returns -1 if nothing is pending. */
    int take() noexcept
    {
        auto s = state.load (std::memory_order_acquire);
        for (;;)
        {
            const int p = pendingOf (s);
            if (p < 0)
                return -1;

            const auto next = (s & ~kPendingMask) | (1u << p);
            if (state.compare_exchange_weak (s, next, std::memory_order_acq_rel, std::memory_order_acquire))
                return p;
        }
    }

    void release (int index) noexcept
    {
        state.fetch_and (~(1u << index), std::memory_order_acq_rel);
    }

    bool hasPending() const noexcept { return pendingOf (state.load (std::memory_order_acquire)) >= 0; }

private:
    static constexpr uint32_t kPendingShift = 8;
    static constexpr uint32_t kPendingMask  = 0xFu << kPendingShift;

    static int pendingOf (uint32_t s) noexcept
    {
        return static_cast<int> ((s & kPendingMask) >> kPendingShift) - 1;
    }

    std::array<std::unique_ptr<T>, static_cast<size_t> (N)> slots;
    std::atomic<uint32_t> state { 0 };

    static_assert (std::atomic<uint32_t>::is_always_lock_free);
};

} // namespace worldtable::dsp
