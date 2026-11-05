#include "dsp/Adsr.h"
#include "dsp/Music.h"
#include "dsp/TableExchange.h"
#include "dsp/fx/Limiter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace worldtable;
using namespace worldtable::dsp;

namespace
{
std::string pattern (int steps, int pulses, int rotation)
{
    std::string s;
    for (int i = 0; i < steps; ++i)
        s += music::euclidHit (i, steps, pulses, rotation) ? 'x' : '.';
    return s;
}
} // namespace

TEST_CASE ("Euclidean rhythms", "[music]")
{
    CHECK (pattern (8, 3, 0) == "x..x..x.");     // tresillo
    CHECK (pattern (8, 3, 1) == "..x..x.x");     // rotated by one step
    CHECK (pattern (4, 4, 0) == "xxxx");
    CHECK (pattern (4, 9, 0) == "xxxx");          // pulses ≥ steps
    CHECK (pattern (5, 0, 0) == ".....");

    for (int steps = 2; steps <= 32; ++steps)
        for (int pulses = 1; pulses <= steps; ++pulses)
        {
            int count = 0;
            for (int i = 0; i < steps; ++i)
                count += music::euclidHit (i, steps, pulses, 3) ? 1 : 0;
            REQUIRE (count == pulses);
        }
}

TEST_CASE ("Scales and chords", "[music]")
{
    using params::Scale;
    CHECK (music::degreeToSemitone (Scale::kDorian, 2) == 3);
    CHECK (music::degreeToSemitone (Scale::kDorian, 7) == 12);
    CHECK (music::degreeToSemitone (Scale::kDorian, -1) == -2);
    CHECK (music::degreeToSemitone (Scale::kPentaMinor, 5) == 12);
    CHECK (music::degreeToSemitone (Scale::kPentaMinor, 6) == 15);

    // "Open" chord in D Dorian from D3 = D3 A3 E4 F4
    const auto& open = music::chordDef (params::Chord::kOpen);
    std::vector<int> notes;
    for (int k = 0; k < open.size; ++k)
        notes.push_back (50 + music::degreeToSemitone (Scale::kDorian, open.degrees[static_cast<size_t> (k)]));
    CHECK (notes == std::vector<int> { 50, 57, 64, 65 });
}

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

TEST_CASE ("ADSR reaches its stages on time", "[adsr]")
{
    Adsr env;
    env.prepare (1000.0);
    env.setSettings ({ 0.1f, 0.2f, 0.5f, 0.3f });
    env.noteOn();

    int samplesToPeak = 0;
    while (env.getStage() == Adsr::Stage::attack && samplesToPeak < 1000)
    {
        env.next();
        ++samplesToPeak;
    }
    CHECK (samplesToPeak == Catch::Approx (100).margin (2));

    for (int i = 0; i < 1000; ++i)
        env.next();
    CHECK (env.getLevel() == Catch::Approx (0.5f).margin (1.0e-3));

    env.noteOff();
    int samplesToSilence = 0;
    while (env.isActive() && samplesToSilence < 5000)
    {
        env.next();
        ++samplesToSilence;
    }
    // −60 dB after 0.3 s from 0.5, idle at −80 dB from 1.0 → a bit later
    CHECK (samplesToSilence > 300);
    CHECK (samplesToSilence < 450);
}
