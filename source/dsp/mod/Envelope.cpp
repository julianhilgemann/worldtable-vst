#include "Envelope.h"

// T4.2 step 1: STUBS (see ModMatrix.cpp). Outputs are zeros; the engine PR implements them.

namespace worldtable::dsp::mod
{

void Envelope::reset() noexcept
{
    now = Stage::idle;
    out = 0.0f;
    f = 0.0;
    releaseFrom = 0.0;
    holdTicks = 0;
    stageDone = false;
    heldByNote = false;
    firePending = false;
    releasePending = false;
    keys = {};
}

void Envelope::setSettings (const Settings& s) noexcept
{
    settings = s;
}

void Envelope::onset() noexcept {}
void Envelope::noteOn (int) noexcept {}
void Envelope::noteOff (int) noexcept {}
void Envelope::allNotesOff() noexcept {}
void Envelope::fire() noexcept {}

float Envelope::tick() noexcept
{
    return out;
}

float Envelope::progress() const noexcept
{
    return 0.0f;
}

Envelope::View Envelope::view() const noexcept
{
    return {};
}

} // namespace worldtable::dsp::mod
