#include "AllocationCounter.h"

#include <cstdlib>
#include <new>

// Replaces the global operator new / delete for the whole test binary so a test can check that
// audio-thread code never allocates (CLAUDE.md rule 1). Counts per thread, so background threads
// started by other tests can't make a check flaky. The array, nothrow and sized forms default to
// these two.

namespace
{
thread_local int64_t allocations = 0;
}

int64_t testing::allocationsOnThisThread() noexcept { return allocations; }

void* operator new (std::size_t size)
{
    ++allocations;
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
