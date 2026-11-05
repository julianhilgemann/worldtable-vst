#include "dsp/TableExchange.h"
#include "dsp/fx/Limiter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace worldtable;
using namespace worldtable::dsp;

TEST_CASE ("TableExchange hands slots over without sharing", "[exchange]")
{
    TableExchange ex;

    CHECK (ex.take() == -1);

    const int a = ex.acquireForWriting();
    REQUIRE (a >= 0);
    ex.publish (a);
    CHECK (ex.hasPending());

    // An untaken pending slot can be reclaimed and replaced.
    const int b = ex.acquireForWriting();
    CHECK_FALSE (ex.hasPending());
    ex.publish (b);

    const int current = ex.take();
    CHECK (current == b);
    CHECK (ex.take() == -1);

    // Busy slots are never handed to the builder.
    const int c = ex.acquireForWriting();
    REQUIRE (c >= 0);
    CHECK (c != current);
    ex.publish (c);
    const int next = ex.take();
    CHECK (next == c);

    for (int i = 0; i < 10; ++i)
    {
        const int w = ex.acquireForWriting();
        CHECK (w != current);
        CHECK (w != next);
    }

    // Two busy, pending reclaimed each time → never exhausted.
    ex.release (current);
    CHECK (ex.acquireForWriting() >= 0);
}

TEST_CASE ("Limiter holds the ceiling with no overshoot", "[limiter]")
{
    Limiter lim;
    lim.prepare (48000.0);
    lim.setSettings (12.0f, -1.0f, 100.0f);
    const float ceiling = dbToGain (-1.0f);

    std::vector<float> l (48000), r (48000);
    for (size_t i = 0; i < l.size(); ++i)
    {
        l[i] = 0.9f * std::sin (0.05f * static_cast<float> (i));
        r[i] = (i % 5000 == 0) ? 1.0f : 0.3f * l[i];   // isolated spikes
    }
    lim.process (l.data(), r.data(), static_cast<int> (l.size()));

    for (size_t i = 0; i < l.size(); ++i)
    {
        REQUIRE (std::abs (l[i]) <= ceiling * 1.0001f);
        REQUIRE (std::abs (r[i]) <= ceiling * 1.0001f);
    }
}

TEST_CASE ("Limiter passes quiet signals untouched, delayed by its latency", "[limiter]")
{
    Limiter lim;
    lim.prepare (48000.0);
    lim.setSettings (0.0f, -1.0f, 100.0f);

    std::vector<float> l (2000), r (2000);
    for (size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = 0.25f * std::sin (0.01f * static_cast<float> (i));
    const auto input = l;
    lim.process (l.data(), r.data(), static_cast<int> (l.size()));

    const auto latency = static_cast<size_t> (lim.getLatency());
    for (size_t i = latency; i < l.size(); ++i)
        REQUIRE (l[i] == Catch::Approx (input[i - latency]).margin (1.0e-6));
}
