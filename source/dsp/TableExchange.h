#pragma once

#include "SlotExchange.h"
#include "WavetableSet.h"

namespace worldtable::dsp
{

/** Lock-free hand-off of wavetables from the builder thread to the audio thread
    (spec/03-architecture.md, hand-off C). Four slots: the current table, the one fading out, a
    pending one and one being built. The mechanism is SlotExchange (ADR-0003). */
using TableExchange = SlotExchange<WavetableSet, 4>;

} // namespace worldtable::dsp
