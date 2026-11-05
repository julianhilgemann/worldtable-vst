#pragma once

#include <cstdint>

namespace testing
{
/** Number of global operator new calls made on the calling thread so far (AllocationCounter.cpp
    replaces operator new for the test binary). Take the difference around the code under test. */
int64_t allocationsOnThisThread() noexcept;
} // namespace testing
