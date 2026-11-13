#include "Envelope.h"

#include <algorithm>
#include <cmath>

namespace worldtable::dsp::mod
{

namespace
{
constexpr double kDone = 1.0 - 1.0e-9;   ///< a stage within this of its end has ended (float sums)
constexpr int kHoldTicks = static_cast<int> (Envelope::kHoldSeconds / Envelope::kTickSeconds + 0.5);

double seconds (double t) noexcept
{
    return std::isfinite (t) ? std::clamp (t, Envelope::kMinSeconds, Envelope::kMaxSeconds) : Envelope::kMinSeconds;
}
} // namespace

void Envelope::reset() noexcept
{
    now = Stage::idle;
    out = 0.0f;
    level = 0.0;
    f = 0.0;
    releaseFrom = 0.0;
    holdTicks = 0;
    stageDone = false;
    heldByNote = false;
    firePending = false;
    fireByNote = false;
    releasePending = false;
    keys = {};
}

void Envelope::setSettings (const Settings& s) noexcept
{
    settings.attack = seconds (s.attack);
    settings.decay = seconds (s.decay);
    settings.sustain = std::isfinite (s.sustain) ? std::clamp (s.sustain, 0.0, 1.0) : 0.0;
    settings.release = seconds (s.release);
    settings.trigger = s.trigger;
}

//==================================================================================================
void Envelope::onset() noexcept
{
    if (settings.trigger != Trigger::onset)
        return;
    firePending = true;
    fireByNote = false;
}

void Envelope::noteOn (int note) noexcept
{
    const int n = std::clamp (note, 0, 127);
    keys[static_cast<size_t> (n / 64)] |= uint64_t { 1 } << (n % 64);
    if (settings.trigger != Trigger::note)
        return;
    firePending = true;
    fireByNote = true;
}

void Envelope::noteOff (int note) noexcept
{
    const int n = std::clamp (note, 0, 127);
    keys[static_cast<size_t> (n / 64)] &= ~(uint64_t { 1 } << (n % 64));
    if (! anyKey())
        releasePending = true;
}

void Envelope::allNotesOff() noexcept
{
    keys = {};
    releasePending = true;
}

void Envelope::fire() noexcept
{
    firePending = true;
    fireByNote = false;
}

void Envelope::startRelease() noexcept
{
    now = Stage::release;
    releaseFrom = level;
    f = 0.0;
    stageDone = false;
    heldByNote = false;
}

//==================================================================================================
float Envelope::tick() noexcept
{
    // 1. What happened since the last tick. Leaving Note mode lets go of a key's hold.
    if (heldByNote && settings.trigger != Trigger::note)
        startRelease();

    if (firePending)
    {
        // a (re)trigger rises from where the envelope is: never down to 0 first
        firePending = false;
        now = Stage::attack;
        stageDone = false;
        holdTicks = 0;
        heldByNote = fireByNote;
        // a note shorter than a tick: one tick of attack now, the release on the next
        if (! heldByNote || anyKey())
            releasePending = false;
    }
    else if (releasePending)
    {
        releasePending = false;
        if (heldByNote && ! anyKey() && now != Stage::idle)
            startRelease();
    }

    // 2. A stage that ended on the last tick hands over to the next.
    if (stageDone)
    {
        stageDone = false;
        switch (now)
        {
            case Stage::attack:  now = Stage::decay; f = 0.0; break;
            case Stage::decay:   now = Stage::sustain; holdTicks = 0; break;
            case Stage::sustain: startRelease(); break;
            case Stage::release: now = Stage::idle; heldByNote = false; break;
            case Stage::idle:    break;
        }
    }

    // 3. One tick of the stage.
    const double s = settings.sustain;
    switch (now)
    {
        case Stage::idle:
            level = 0.0;
            break;
        case Stage::attack:
            level += kTickSeconds / settings.attack;
            if (level >= kDone)
            {
                level = 1.0;
                stageDone = true;
            }
            break;
        case Stage::decay:
            f += kTickSeconds / settings.decay;
            if (f >= kDone)
            {
                f = 1.0;
                stageDone = true;
            }
            level = 1.0 - (1.0 - s) * (1.0 - (1.0 - f) * (1.0 - f));
            break;
        case Stage::sustain:
            level = s;
            if (! heldByNote && ++holdTicks >= kHoldTicks)
                stageDone = true;
            break;
        case Stage::release:
            f += kTickSeconds / settings.release;
            if (f >= kDone)
            {
                f = 1.0;
                stageDone = true;
            }
            level = releaseFrom * (1.0 - f) * (1.0 - f);
            break;
    }
    out = static_cast<float> (level);
    return out;
}

float Envelope::progress() const noexcept
{
    switch (now)
    {
        case Stage::attack:  return out;
        case Stage::decay:
        case Stage::release: return static_cast<float> (f);
        case Stage::sustain: return heldByNote ? 0.0f : std::min (1.0f, static_cast<float> (holdTicks) / static_cast<float> (kHoldTicks));
        case Stage::idle:    break;
    }
    return 0.0f;
}

Envelope::View Envelope::view() const noexcept
{
    View v;
    v.stage = now;
    v.progress = progress();
    v.value = out;
    v.held = heldByNote && now == Stage::sustain;
    return v;
}

} // namespace worldtable::dsp::mod
