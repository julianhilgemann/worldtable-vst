#pragma once

#include "LowResonator.h"
#include "PlayVoices.h"
#include "SkySound.h"
#include "fx/Filter.h"

#include <cstdint>

namespace worldtable::dsp
{

class ScopeFifo;   // dsp/Instrument.h

/** Everything that sounds beside the curtain, and the filter after it (T3.7, spec/05):

    ```
    SkySound's bank × drone gain ─┐
    PlayVoices (Play mode) ───────┼─► Filter ─► (Drive … in dsp::Instrument)
    LowResonator ─────────────────┘
    ```
    It is SkySound's companion, so it runs on the sky's clock: per tick it reads the curtain (pan,
    presence, displacement, emission) for the played notes, the bass note for the low resonator
    and the activity for the filter; per tick segment it mixes and filters in place. That keeps
    Play mode and the filter as block-size independent as the curtain (rule 5).

    Mode: `play` glides a mix from drone to Play over kModeSeconds (rule 4). The drone gain goes to
    `droneUnder` in Play mode; the low resonator follows the chord root (drone) or the lowest held
    note (Play), folded into the low band from SkySound's applied floor (T3.8). The note events are
    Play mode's (the instrument routes MIDI here only then).

    prepare() on the message thread; everything else is real-time safe. */
class Sources final : public SkySound::Companion
{
public:
    static constexpr double kModeSeconds = 2.0;          ///< drone ↔ Play
    static constexpr double kSkySlewSeconds = 2.0;       ///< the activity the filter and bow follow
    static constexpr double kFilterSkyOctaves = 3.0;     ///< at Sky 1 and activity 1
    static constexpr double kLowDroneCrossfade = 4.0;    ///< s, to a new chord root
    static constexpr double kBowPerBed = 2.7;            ///< a held note is bowed harder than the bed
    static constexpr double kLowBowPerBed = 1.4;         ///< the low under held notes (T3.7 renders)
    static constexpr double kDisplacementForFullDetuneKm = 160.0;   ///< SkySound's
    static constexpr double kDriftKnotSeconds = 20.0;

    struct Settings
    {
        bool play = false;
        double droneUnder = 0.0;          ///< the drone's level in Play mode
        PlayVoices::Settings voices;      ///< bow and strike are set from `bed` and `strike` below
        double bed = 0.3, strike = 1.0;   ///< the exciter's (Storm-scaled), for the notes and the low
        double detuneCents = 3.0, driftCents = 4.0;
        double lowLevel = 0.5;
        LowResonator::Settings low;
        Filter::Settings filter;
        double filterSky = 0.3;           ///< 0 … 1
    };

    void prepare (double sampleRate, uint32_t seed, const Settings&) noexcept;
    void setSettings (const Settings&) noexcept;

    /** T6.12: where the sources' mix goes before the tone stage (the editor's input spectrum), or
        nullptr. Read-only: a mono copy of what reaches the filter, lock-free and allocation-free. */
    void setInputTap (ScopeFifo* fifo) noexcept { inputTap = fifo; }

    /** The low band's floor before the first tick (after it, every tick reads SkySound's bands). */
    void setLowFloor (int floorNote) noexcept { low.setFloor (floorNote); }

    //== Play mode events (audio thread, between samples) =============================================
    void noteOn (int note, float velocity) noexcept;
    void noteOff (int note) noexcept;
    void sustain (bool down) noexcept   { voices.sustain (down); }
    void allNotesOff() noexcept         { voices.allNotesOff(); }
    void allSoundOff() noexcept         { voices.allSoundOff(); }

    //== SkySound::Companion =========================================================================
    void skyTicked (const sky::SkyModel&, std::span<const sky::SkyEvent>, const SkySound&, int tickSamples) noexcept override;
    void renderSamples (float* left, float* right, int numSamples) noexcept override;

    //== State =========================================================================================
    const PlayVoices& playVoices() const noexcept  { return voices; }
    const LowResonator& lowResonator() const noexcept { return low; }
    const Filter& filter() const noexcept          { return flt; }
    double playMix() const noexcept                { return mix; }      ///< 0 drone … 1 Play
    double droneGain() const noexcept              { return drone; }
    double skyActivity() const noexcept            { return activity; } ///< slewed
    int guard() noexcept                           { return voices.guard() + low.guard(); }

private:
    void readCurtain (const sky::SkyModel&, const SkySound&) noexcept;

    Settings settings;
    double sampleRate = 48000.0;
    uint32_t seed = 1;

    PlayVoices voices;
    LowResonator low;
    Filter flt;
    SkyNodes curtain;

    double mix = 0.0;                    ///< drone → Play, per tick
    double drone = 1.0, droneStep = 0.0; ///< the bank's gain, ramped per sample over the tick
    int droneRampLeft = 0;
    double activity = 0.0;
    bool primed = false;
    double redWeight = 1.0;              ///< the curtain's mean red weight (0.5 … 1.5)
    double lowEnv = 0.0;                 ///< Play mode: the low's envelope (gate: a note held)
    double lowSwellStart = -1.0, lowSwellAmp = 0.0;   ///< drone mode: the onset swell
    uint64_t ticks = 0;
    ScopeFifo* inputTap = nullptr;       ///< T6.12, owned by the Instrument
};

} // namespace worldtable::dsp
