#include "Engine.h"
#include "Music.h"

namespace worldtable::dsp
{

namespace
{
inline int toInt (const std::atomic<float>* a) noexcept   { return static_cast<int> (std::lround (a->load (std::memory_order_relaxed))); }
inline float toFloat (const std::atomic<float>* a) noexcept { return a->load (std::memory_order_relaxed); }
} // namespace

struct Engine::Controls
{
    int mode, root, octave, scale, chord;
    float bpm;
    int euSteps, euPulses, euRotate, euRate, euRange, euNotes;
    float euGate, euMutate;

    float pos, detune, width, drive, levelDb;
    int unison, octaveShift;

    int fltType;
    float cutoff, reso, fltEnv, fltKey;
    Adsr::Settings ampEnv, filterEnv;

    float lfoRate[2], lfoAmt[2];
    int lfoShape[2], lfoDest[2];

    int dlyTime;
    float dlyFb, dlyTone, dlyMix;
    bool dlyPingPong;

    ShimmerReverb::Settings reverb;
    float limGain, limCeiling, limRelease, masterDb;
};

Engine::Engine (params::ParamPointers& paramsIn, TableExchange& exchangeIn)
    : p (paramsIn), exchange (exchangeIn)
{
}

void Engine::prepare (double rate, int)
{
    sampleRate = rate;

    for (size_t i = 0; i < voices.size(); ++i)
        voices[i].prepare (rate, 0x1234u + static_cast<uint32_t> (i) * 7919u);

    lfo1.prepare (rate, 0xA11CEu);
    lfo2.prepare (rate, 0xB0B5u);

    const double chunkRate = rate / kChunk;
    for (auto* s : { &posSmooth, &cutoffSmooth, &resoSmooth, &driveSmooth, &detuneSmooth, &levelSmooth,
                     &widthSmooth, &delayMixSmooth, &revMixSmooth, &masterSmooth })
        s->setTime (0.04f, chunkRate);

    delay.prepare (rate);
    reverb.prepare (rate);
    limiter.prepare (rate);

    seqRng.seed (0x5EED5u);
    for (auto& u : stepNoteU)
        u = seqRng.nextFloat();

    reset();

    // Snap smoothers to current values so the first chunk does not glide from zero.
    Controls c {};
    readControls (c);
    posSmooth.reset (c.pos);
    cutoffSmooth.reset (std::log2 (c.cutoff));
    resoSmooth.reset (c.reso);
    driveSmooth.reset (c.drive);
    detuneSmooth.reset (c.detune);
    levelSmooth.reset (dbToGain (c.levelDb));
    widthSmooth.reset (c.width);
    delayMixSmooth.reset (c.dlyMix);
    revMixSmooth.reset (c.reverb.mix);
    masterSmooth.reset (dbToGain (c.masterDb));
}

void Engine::reset()
{
    for (auto& v : voices)
        v.reset();
    delay.reset();
    reverb.reset();
    limiter.reset();
    numDroneNotes = 0;
    lastMode = -1;
    lastStepIndex = -1;
    clockPpq = 0.0;
    focusVoice = -1;
}

void Engine::readControls (Controls& c) noexcept
{
    c.mode = toInt (p.trig_mode);
    c.root = toInt (p.root);
    c.octave = toInt (p.octave);
    c.scale = toInt (p.scale);
    c.chord = toInt (p.chord);
    c.bpm = toFloat (p.bpm);

    c.euSteps = std::clamp (toInt (p.eu_steps), 2, 32);
    c.euPulses = toInt (p.eu_pulses);
    c.euRotate = toInt (p.eu_rotate);
    c.euRate = toInt (p.eu_rate);
    c.euGate = toFloat (p.eu_gate);
    c.euRange = toInt (p.eu_range);
    c.euNotes = toInt (p.eu_notes);
    c.euMutate = toFloat (p.eu_mutate);

    c.pos = toFloat (p.osc_pos);
    c.octaveShift = toInt (p.osc_octave);
    c.unison = toInt (p.osc_unison);
    c.detune = toFloat (p.osc_detune);
    c.width = toFloat (p.osc_width);
    c.drive = toFloat (p.osc_drive);
    c.levelDb = toFloat (p.osc_level);

    c.fltType = toInt (p.flt_type);
    c.cutoff = toFloat (p.flt_cutoff);
    c.reso = toFloat (p.flt_reso);
    c.fltEnv = toFloat (p.flt_env);
    c.fltKey = toFloat (p.flt_key);

    c.ampEnv = { toFloat (p.amp_a), toFloat (p.amp_d), toFloat (p.amp_s), toFloat (p.amp_r) };
    c.filterEnv = { toFloat (p.fenv_a), toFloat (p.fenv_d), toFloat (p.fenv_s), toFloat (p.fenv_r) };

    c.lfoRate[0] = toFloat (p.lfo1_rate);
    c.lfoShape[0] = toInt (p.lfo1_shape);
    c.lfoDest[0] = toInt (p.lfo1_dest);
    c.lfoAmt[0] = toFloat (p.lfo1_amt);
    c.lfoRate[1] = toFloat (p.lfo2_rate);
    c.lfoShape[1] = toInt (p.lfo2_shape);
    c.lfoDest[1] = toInt (p.lfo2_dest);
    c.lfoAmt[1] = toFloat (p.lfo2_amt);

    c.dlyTime = toInt (p.dly_time);
    c.dlyFb = toFloat (p.dly_fb);
    c.dlyTone = toFloat (p.dly_tone);
    c.dlyPingPong = toInt (p.dly_pingpong) != 0;
    c.dlyMix = toFloat (p.dly_mix);

    c.reverb.size = toFloat (p.rev_size);
    c.reverb.decaySeconds = toFloat (p.rev_decay);
    c.reverb.damping = toFloat (p.rev_damp);
    c.reverb.predelayMs = toFloat (p.rev_predelay);
    c.reverb.shimmer = toFloat (p.rev_shimmer);
    c.reverb.modulation = toFloat (p.rev_mod);
    c.reverb.mix = toFloat (p.rev_mix);

    c.limGain = toFloat (p.lim_gain);
    c.limCeiling = toFloat (p.lim_ceiling);
    c.limRelease = toFloat (p.lim_release);
    c.masterDb = toFloat (p.master);
}

void Engine::pickUpTables() noexcept
{
    // Finish a running fade first; a newer table waits (the builder may replace it meanwhile).
    if (previousSlot >= 0 && tableFade >= 1.0f)
    {
        exchange.release (previousSlot);
        previousSlot = -1;
    }

    if (previousSlot >= 0)
        return;

    const int next = exchange.take();
    if (next < 0)
        return;

    if (currentSlot < 0)
    {
        currentSlot = next;          // very first table: no fade needed
        tableFade = 1.0f;
        return;
    }

    previousSlot = currentSlot;
    currentSlot = next;
    tableFade = 0.0f;
    tableFadeInc = 1.0f / (kTableFadeSeconds * static_cast<float> (sampleRate));
}

void Engine::prepareChunk (const Controls& c, int n) noexcept
{
    ctx.numSamples = n;
    ctx.sampleRate = sampleRate;
    ctx.table = currentSlot >= 0 ? &exchange.slot (currentSlot) : nullptr;
    ctx.prevTable = previousSlot >= 0 ? &exchange.slot (previousSlot) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        tableFade = std::min (1.0f, tableFade + tableFadeInc);
        // equal-power-ish fade shape
        ctx.tableFade[static_cast<size_t> (i)] = tableFade * tableFade * (3.0f - 2.0f * tableFade);
    }

    // smoothed controls (chunk rate)
    const float pos = posSmooth.next (c.pos);
    const float cutoffLog = cutoffSmooth.next (std::log2 (std::max (20.0f, c.cutoff)));
    ctx.cutoffHz = std::exp2 (cutoffLog);
    ctx.damping = resonanceToDamping (resoSmooth.next (c.reso));
    ctx.detuneCents = 80.0f * detuneSmooth.next (c.detune);
    ctx.width = widthSmooth.next (c.width);
    const float baseDrive = driveSmooth.next (c.drive);
    ctx.level = 0.5f * levelSmooth.next (dbToGain (c.levelDb));
    ctx.unison = c.unison;
    ctx.octaveShift = c.octaveShift;
    ctx.filterType = c.fltType;
    ctx.envAmountOct = 6.0f * c.fltEnv;
    ctx.keyTrack = c.fltKey;
    ctx.ampEnv = c.ampEnv;
    ctx.filterEnv = c.filterEnv;

    // LFOs (per sample) → destinations
    lfo1.setRate (c.lfoRate[0]);
    lfo1.setShape (c.lfoShape[0]);
    lfo2.setRate (c.lfoRate[1]);
    lfo2.setShape (c.lfoShape[1]);

    float driveMod = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float v[2] = { lfo1.next(), lfo2.next() };
        float posMod = 0.0f, pitchSemis = 0.0f, cutOct = 0.0f, amp = 1.0f, pan = 0.0f;

        for (int l = 0; l < 2; ++l)
        {
            const float amt = c.lfoAmt[l];
            switch (c.lfoDest[l])
            {
                case params::LfoDest::kPosition: posMod += amt * 0.5f * v[l]; break;
                case params::LfoDest::kCutoff:   cutOct += amt * 4.0f * v[l]; break;
                case params::LfoDest::kPitch:    pitchSemis += amt * 2.0f * v[l]; break;
                case params::LfoDest::kDrive:    if (i == 0) driveMod += amt * 0.5f * v[l]; break;
                case params::LfoDest::kAmp:      amp *= 1.0f - std::abs (amt) * 0.5f * (1.0f - v[l] * (amt >= 0 ? 1.0f : -1.0f)); break;
                case params::LfoDest::kPan:      pan += amt * v[l]; break;
                default: break;
            }
        }

        const auto s = static_cast<size_t> (i);
        ctx.position[s] = std::clamp (pos + posMod, 0.0f, 1.0f);
        ctx.pitchRatio[s] = pitchSemis != 0.0f ? std::exp2 (pitchSemis / 12.0f) : 1.0f;
        ctx.cutoffOct[s] = cutOct;
        ctx.ampMod[s] = amp;
        ctx.panMod[s] = std::clamp (pan, -1.0f, 1.0f);
    }
    ctx.drive = std::clamp (baseDrive + driveMod, 0.0f, 1.0f);
}

int Engine::noteFromPool (const Controls& c, float u) const noexcept
{
    const int rootNote = 12 * (c.octave + 1) + c.root;
    const int range = std::clamp (c.euRange, 1, 3);

    int degrees[24];
    int count = 0;

    if (c.euNotes == params::EuNotes::kRoot)
    {
        for (int o = 0; o < range; ++o)
            degrees[count++] = o * 12;
        const int idx = std::min (count - 1, static_cast<int> (u * static_cast<float> (count)));
        return rootNote + degrees[idx];
    }

    if (c.euNotes == params::EuNotes::kChord)
    {
        const auto& chord = music::chordDef (c.chord);
        for (int o = 0; o < range; ++o)
            for (int k = 0; k < chord.size && count < 24; ++k)
                degrees[count++] = music::degreeToSemitone (c.scale, chord.degrees[static_cast<size_t> (k)]) + 12 * o;
    }
    else
    {
        const auto& scale = music::scaleDef (c.scale);
        for (int d = 0; d < scale.size * range && count < 23; ++d)
            degrees[count++] = music::degreeToSemitone (c.scale, d);
        degrees[count++] = 12 * range;
    }

    const int idx = std::clamp (static_cast<int> (u * static_cast<float> (count)), 0, count - 1);
    return std::clamp (rootNote + degrees[idx], 0, 127);
}

void Engine::startNote (int note, float velocity, Voice::Owner owner, int64_t gateSamples) noexcept
{
    int best = -1;

    for (int i = 0; i < kNumVoices; ++i)
        if (! voices[static_cast<size_t> (i)].isActive())
        {
            best = i;
            break;
        }

    if (best < 0)
    {
        // steal: quietest releasing voice, else the oldest
        float quietest = 2.0f;
        for (int i = 0; i < kNumVoices; ++i)
        {
            const auto& v = voices[static_cast<size_t> (i)];
            if (v.isReleasing() && v.getAmpLevel() < quietest)
            {
                quietest = v.getAmpLevel();
                best = i;
            }
        }
    }

    if (best < 0)
    {
        uint64_t oldest = UINT64_MAX;
        for (int i = 0; i < kNumVoices; ++i)
            if (voices[static_cast<size_t> (i)].getAge() < oldest)
            {
                oldest = voices[static_cast<size_t> (i)].getAge();
                best = i;
            }
    }

    voices[static_cast<size_t> (best)].start (note, velocity, owner, gateSamples, ++voiceAge);
    focusVoice = best;
}

void Engine::releaseOwner (Voice::Owner owner) noexcept
{
    for (auto& v : voices)
        if (v.isActive() && ! v.isReleasing() && v.getOwner() == owner)
            v.release();
}

void Engine::updateDrone (const Controls& c) noexcept
{
    if (c.mode != params::TrigMode::kDrone)
    {
        if (numDroneNotes > 0)
        {
            releaseOwner (Voice::Owner::drone);
            numDroneNotes = 0;
        }
        return;
    }

    std::array<int, 8> wanted {};
    const auto& chord = music::chordDef (c.chord);
    const int rootNote = 12 * (c.octave + 1) + c.root;
    for (int k = 0; k < chord.size; ++k)
        wanted[static_cast<size_t> (k)] = std::clamp (rootNote + music::degreeToSemitone (c.scale, chord.degrees[static_cast<size_t> (k)]), 0, 127);

    bool same = numDroneNotes == chord.size;
    for (int k = 0; same && k < chord.size; ++k)
        same = wanted[static_cast<size_t> (k)] == droneNotes[static_cast<size_t> (k)];

    if (same)
        return;

    // Re-voice: old notes release while new ones swell in (their envelopes overlap).
    // Equal-power velocity: a 1-note and a 5-note drone sit at the same loudness.
    releaseOwner (Voice::Owner::drone);
    const float velocity = std::min (1.0f, 1.2f / std::sqrt (static_cast<float> (chord.size)));
    for (int k = 0; k < chord.size; ++k)
        startNote (wanted[static_cast<size_t> (k)], velocity, Voice::Owner::drone, -1);

    droneNotes = wanted;
    numDroneNotes = chord.size;
}

void Engine::runEuclid (const Controls& c, int numSamples, double ppqPerSample) noexcept
{
    const double stepQ = music::rateToQuarterNotes (c.euRate);
    const double endPpq = clockPpq + ppqPerSample * numSamples;

    if (c.mode != params::TrigMode::kEuclid)
    {
        lastStepIndex = static_cast<int64_t> (std::floor (endPpq / stepQ));
        return;
    }

    // every step boundary inside [clockPpq, endPpq) (quantised to this chunk)
    auto step = static_cast<int64_t> (std::ceil (clockPpq / stepQ - 1.0e-9));
    for (int guard = 0; static_cast<double> (step) * stepQ < endPpq && guard < 8; ++step, ++guard)
    {
        if (step == lastStepIndex)
            continue;
        lastStepIndex = step;

        const int steps = c.euSteps;
        const int inPattern = static_cast<int> (((step % steps) + steps) % steps);

        if (inPattern == 0)
            for (int s = 0; s < steps; ++s)
                if (seqRng.nextFloat() < c.euMutate)
                    stepNoteU[static_cast<size_t> (s)] = seqRng.nextFloat();

        telemetry.euclidStep.store (inPattern, std::memory_order_relaxed);

        if (music::euclidHit (inPattern, steps, std::min (c.euPulses, steps), c.euRotate))
        {
            const int note = noteFromPool (c, stepNoteU[static_cast<size_t> (inPattern)]);
            const float vel = inPattern == 0 ? 1.0f : 0.72f + 0.2f * seqRng.nextFloat();
            const auto gate = static_cast<int64_t> (std::max (1.0, c.euGate * stepQ / ppqPerSample));
            startNote (note, vel, Voice::Owner::euclid, gate);
            telemetry.euclidHits.fetch_add (1, std::memory_order_relaxed);
        }
    }
}

void Engine::handleMidi (const juce::MidiMessage& m) noexcept
{
    if (m.isNoteOn())
    {
        startNote (m.getNoteNumber(), m.getFloatVelocity(), Voice::Owner::midi, -1);
    }
    else if (m.isNoteOff())
    {
        for (auto& v : voices)
            if (v.isActive() && ! v.isReleasing() && v.getOwner() == Voice::Owner::midi && v.getNote() == m.getNoteNumber())
                v.release();
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        releaseOwner (Voice::Owner::midi);
    }
}

void Engine::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const HostTime& host) noexcept
{
    const int total = buffer.getNumSamples();
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    Controls c {};
    readControls (c);

    currentBpm = host.bpm > 0.0 ? host.bpm : static_cast<double> (c.bpm);
    const double ppqPerSample = currentBpm / 60.0 / sampleRate;

    if (host.isPlaying && host.hasPpq)
        clockPpq = host.ppq;

    if (c.mode != lastMode)
    {
        if (lastMode == params::TrigMode::kEuclid)
            releaseOwner (Voice::Owner::euclid);
        lastMode = c.mode;
    }

    auto midiIt = midi.begin();

    for (int start = 0; start < total; start += kChunk)
    {
        const int n = std::min (kChunk, total - start);

        pickUpTables();

        // MIDI events in this chunk (applied at chunk start: ≤ 1.3 ms quantisation)
        for (; midiIt != midi.end() && (*midiIt).samplePosition < start + n; ++midiIt)
            handleMidi ((*midiIt).getMessage());

        updateDrone (c);
        runEuclid (c, n, ppqPerSample);
        clockPpq += ppqPerSample * n;

        prepareChunk (c, n);

        std::fill (mixL.begin(), mixL.begin() + n, 0.0f);
        std::fill (mixR.begin(), mixR.begin() + n, 0.0f);

        if (ctx.table != nullptr)
            for (auto& v : voices)
                v.render (ctx, mixL.data(), mixR.data());

        if (tableFade >= 1.0f && previousSlot >= 0)
            pickUpTables();   // release the faded-out slot promptly

        // FX
        const double dq = music::delayToQuarterNotes (c.dlyTime);
        delay.setSettings ({ static_cast<float> (dq * 60.0 / currentBpm), c.dlyFb, c.dlyTone,
                             delayMixSmooth.next (c.dlyMix), c.dlyPingPong });
        delay.process (mixL.data(), mixR.data(), n);

        auto rs = c.reverb;
        rs.mix = revMixSmooth.next (c.reverb.mix);
        reverb.setSettings (rs);
        reverb.process (mixL.data(), mixR.data(), n);

        limiter.setSettings (c.limGain, c.limCeiling, c.limRelease);
        minGain = std::min (minGain, limiter.process (mixL.data(), mixR.data(), n));

        // Output trim, NaN guard, metering
        const float master = masterSmooth.next (dbToGain (c.masterDb));
        bool bad = false;
        for (int i = 0; i < n; ++i)
        {
            const float l = mixL[static_cast<size_t> (i)] * master;
            const float r = mixR[static_cast<size_t> (i)] * master;
            bad |= ! std::isfinite (l) || ! std::isfinite (r);
            outL[start + i] = l;
            if (outR != nullptr)
                outR[start + i] = r;
            peakL = std::max (peakL, std::abs (l));
            peakR = std::max (peakR, std::abs (r));
        }

        if (bad)
        {
            // Should never happen; if it does, recover silently instead of screaming.
            std::fill (outL + start, outL + start + n, 0.0f);
            if (outR != nullptr)
                std::fill (outR + start, outR + start + n, 0.0f);
            for (auto& v : voices)
                v.reset();
            delay.reset();
            reverb.reset();
            limiter.reset();
            numDroneNotes = 0;
        }

        scope.push (outL + start, outR != nullptr ? outR + start : outL + start, n);
    }

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, total);

    publishTelemetry (c);
}

void Engine::publishTelemetry (const Controls&) noexcept
{
    auto& t = telemetry;

    int active = 0;
    std::array<uint32_t, 4> mask {};
    for (auto& v : voices)
        if (v.isActive())
        {
            ++active;
            const int note = v.getNote();
            mask[static_cast<size_t> (note >> 5)] |= 1u << (note & 31);
        }
    for (size_t i = 0; i < 4; ++i)
        t.noteMask[i].store (mask[i], std::memory_order_relaxed);
    t.activeVoices.store (active, std::memory_order_relaxed);

    if (focusVoice < 0 || ! voices[static_cast<size_t> (focusVoice)].isActive())
    {
        focusVoice = -1;
        for (int i = 0; i < kNumVoices; ++i)
            if (voices[static_cast<size_t> (i)].isActive())
                focusVoice = i;
    }

    if (focusVoice >= 0)
    {
        const auto& v = voices[static_cast<size_t> (focusVoice)];
        t.ampEnv.store (v.getAmpLevel(), std::memory_order_relaxed);
        t.filterEnv.store (v.getFilterLevel(), std::memory_order_relaxed);
        t.cutoffHz.store (v.getLastCutoff() * std::exp2 (ctx.cutoffOct[0]), std::memory_order_relaxed);
    }
    else
    {
        t.ampEnv.store (0.0f, std::memory_order_relaxed);
        t.filterEnv.store (0.0f, std::memory_order_relaxed);
        t.cutoffHz.store (ctx.cutoffHz * std::exp2 (ctx.cutoffOct[0]), std::memory_order_relaxed);
    }

    t.position.store (ctx.position[0], std::memory_order_relaxed);
    t.lfo1.store (lfo1.getValue(), std::memory_order_relaxed);
    t.lfo2.store (lfo2.getValue(), std::memory_order_relaxed);
    t.lfo1Phase.store (static_cast<float> (lfo1.getPhase()), std::memory_order_relaxed);
    t.lfo2Phase.store (static_cast<float> (lfo2.getPhase()), std::memory_order_relaxed);
    t.tableFade.store (tableFade, std::memory_order_relaxed);
    t.reverbLevel.store (reverb.getWetLevel(), std::memory_order_relaxed);
    t.bpm.store (currentBpm, std::memory_order_relaxed);

    // Meters are peak-hold until the UI collects them (exchange to 0).
    t.peakL.store (std::max (peakL, t.peakL.load (std::memory_order_relaxed)), std::memory_order_relaxed);
    t.peakR.store (std::max (peakR, t.peakR.load (std::memory_order_relaxed)), std::memory_order_relaxed);
    t.gainReduction.store (std::min (minGain, t.gainReduction.load (std::memory_order_relaxed)), std::memory_order_relaxed);
    peakL = peakR = 0.0f;
    minGain = 1.0f;
}

} // namespace worldtable::dsp
