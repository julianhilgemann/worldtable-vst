#include "ModMatrix.h"

// T4.2 step 1: STUBS, so the interface compiles and links while the owner reviews ModMatrix.h
// (CLAUDE.md rule 6). They return zeros: the tests in tests/unit/ModTests.cpp tagged [.t4.2-red]
// fail against them on purpose. The engine PR (step 2) implements them.

namespace worldtable::dsp::mod
{

void ModMatrix::reset (const Depths& depths) noexcept
{
    heard = depths;
    wanted = depths;
}

void ModMatrix::setDepths (const Depths& depths) noexcept
{
    wanted = depths;
}

void ModMatrix::tick (const SourceValues&, const Bases&, Frame& out) noexcept
{
    out = {};
}

bool ModMatrix::gliding() const noexcept
{
    return false;
}

void ModMatrix::evaluate (const Depths&, const SourceValues&, const Bases&, Frame& out) noexcept
{
    out = {};
}

float ModMatrix::apply (Target, float, float) noexcept
{
    return 0.0f;
}

} // namespace worldtable::dsp::mod
