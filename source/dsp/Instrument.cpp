#include "Instrument.h"

#include "sky/SkyParams.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace worldtable::dsp
{

namespace
{
float param (const std::atomic<float>* p, float fallback = 0.0f) noexcept
{
    return p != nullptr ? p->load (std::memory_order_relaxed) : fallback;
}

int paramInt (const std::atomic<float>* p, int fallback = 0) noexcept
{
    return p != nullptr ? static_cast<int> (std::lround (p->load (std::memory_order_relaxed))) : fallback;
}

/** The band edges a voicing used, for the display (T3.8). */
void putEdges (ChordState& s, const Bands& bands) noexcept
{
    for (int i = 0; i < BandEdges::kCount; ++i)
        s.edges[static_cast<size_t> (i)] = static_cast<int16_t> (bands.edges.edge (i));
    s.blend = static_cast<float> (bands.edges.blend);
}

using mod::Target;

/** A macro knob as a grid source: 2·(m − 0.5), −1 … 1 (dsp/mod/ModMatrix.h). */
float macroSource (float m) noexcept { return 2.0f * (m - 0.5f); }

mod::SlowLfo::Shape lfoShapeOf (int index) noexcept
{
    return static_cast<mod::SlowLfo::Shape> (std::clamp (index, 0, mod::SlowLfo::kNumShapes - 1));
}

mod::Envelope::Trigger triggerOf (int index) noexcept
{
    return static_cast<mod::Envelope::Trigger> (std::clamp (index, 0, 2));
}
} // namespace

void Instrument::OnsetSwell::tick() noexcept
{
    if (t < 0.0)
        return;
    t += mod::kTickSeconds;
    if (t < kOnsetAttack)
    {
        value = from + (1.0 - from) * (t / kOnsetAttack);
        return;
    }
    value = std::exp (-(t - kOnsetAttack) / kOnsetTau);
    if (value < 1.0e-6)
    {
        value = 0.0;
        t = -1.0;
    }
}

Instrument::Instrument (params::ParamPointers& pointers, sky::SkyRuntime& sky) : p (pointers), runtime (sky)
{
    matrix.reset (mod::defaultDepths());   // until the processor publishes the session's grid
}

//==================================================================================================
void Instrument::prepare (double rate, int maximumBlockSize)
{
    (void) maximumBlockSize;   // every stage processes in place: no internal buffering needed
    prepared = false;          // (the LFOs take their shapes at once, not crossfaded)
    sampleRate = rate > 0.0 ? rate : 48000.0;
    numNodes = sky::SkyParams {}.nodes;

    // The track was published by the feed: pick it up and apply its start time and speed so the
    // phase below is the Journey's first phase (the render tool does the same after a seek).
    runtime.process (0, nullptr);

    readTarget (target);
    controls = target;

    // T4.2: the grid's sources before the first tick, and the grid evaluated on them (the depths
    // as they are, no glide), so the first block already plays the modulated settings
    {
        const int slot = depthsIn.take();
        if (slot >= 0)
        {
            matrix.reset (depthsIn.slot (slot));
            depthsIn.release (slot);
        }
        else
        {
            matrix.reset (matrix.target());
        }
    }
    for (size_t i = 0; i < lfos.size(); ++i)
        lfos[i].prepare (seed, static_cast<uint32_t> (i));
    env.reset();
    onsetSwell = {};
    firesTaken = fireRequests.load (std::memory_order_acquire);
    configureMotion (controls);
    frame = {};
    {
        auto sources = sourceValues (0);
        sources[mod::Source::activity] = 0.0f;
        mod::ModMatrix::evaluate (matrix.depths(), sources, basesFrom (controls), frame);
    }
    modTicks = 0;
    modTicked = false;

    configureHarmony (controls);

    SkySound::Settings settings = settingsFrom (controls, frame);
    settings.voicesPerNode = 1;
    skySound.prepare (sampleRate, numNodes, seed, settings, skySound.harmony().chord());
    front.prepare (sampleRate, seed, sourcesFrom (controls, frame));
    front.setLowFloor (skySound.bands().edges.floor);
    front.setInputTap (&inputScope);
    skySound.setCompanion (&front);
    lastPlay = controls.play;

    phaser.prepare (sampleRate);
    chorus.prepare (sampleRate);
    delay.prepare (sampleRate);
    reverb.prepare (sampleRate);
    autoGain.prepare (sampleRate);
    autoGainHeld = controls.play;
    {
        dsp::AutoGain::Settings ag;   // prepare() resets them (T3.5a)
        ag.hold = autoGainHeld;
        autoGain.setSettings (ag);
    }
    dcL.prepare (sampleRate);
    dcR.prepare (sampleRate);
    limiter.prepare (sampleRate);
    limiter.setSettings (0.0f, controls.ceiling, controls.release);

    masterGain = dbToGain (controls.masterDb);
    applyControls();
    fxTo = fxOf (frame);
    fxFrom = fxTo;
    fxMoving = false;
    setEffects (fxTo);
    tickLength = intoTick = 0;

    telemetry.peakL.store (0.0f);
    telemetry.peakR.store (0.0f);
    telemetry.gainReduction.store (1.0f);
    telemetry.autoGain.store (1.0f);
    telemetry.reverbLevel.store (0.0f);
    telemetry.breath.store (0.0f);
    telemetry.lowNote.store (-1);
    telemetry.lowLevel.store (0.0f);
    telemetry.eqCutHz.store (front.eq().cutHz());
    telemetry.playMix.store (controls.play ? 1.0f : 0.0f);
    telemetry.phaserPhase.store (phaser.lfoPhase());

    prepared = true;
    publishedPlay = ! controls.play;   // force the first publish
    publishChord();
    publishPlay();
    publishModulation();
}

void Instrument::configureHarmony (const Controls& c) noexcept
{
    auto& h = skySound.harmony();
    h.setSeed (seed);
    h.setKey (c.key);
    h.setMood (static_cast<Mood> (std::clamp (c.mood, 0, kNumMoods - 1)));
    h.setScale (static_cast<Scale> (std::clamp (c.scale, 0, kNumScales - 1)));
    h.setChordIndex (c.shape);
    h.setTranspose (c.transpose);
    h.setProgression (static_cast<double> (frame[Target::progression]));
    h.start (static_cast<Phase> (runtime.model().current().phase));

    lastKey = c.key;
    lastMood = c.mood;
    lastScale = c.scale;
    lastShape = c.shape;
    lastTranspose = c.transpose;
    lastProgression = static_cast<float> (h.progression());
}

//==================================================================================================
void Instrument::readTarget (Controls& c) const noexcept
{
    c.key = paramInt (p.key, 2) % 12;
    c.mood = paramInt (p.mood, 0);
    c.scale = paramInt (p.scale, 0);
    c.shape = paramInt (p.chord_shape, 0);
    c.transpose = paramInt (p.transpose, 0);
    c.progression = param (p.progression, 1.0f);
    c.detune = param (p.detune, 3.0f);
    c.drift = param (p.drift, 4.0f);
    c.width = param (p.width, 0.9f);

    c.level[0] = param (p.red_level, 1.0f);
    c.level[1] = param (p.green_level, 0.7f);
    c.level[2] = param (p.violet_level, 0.4f);
    c.t60[0] = param (p.red_decay, 20.0f);
    c.t60[1] = param (p.green_decay, 6.0f);
    c.t60[2] = param (p.violet_decay, 2.5f);

    c.storm = param (p.macro_storm, 0.5f);
    c.breath = param (p.macro_breath, 0.35f);
    c.glow = param (p.macro_glow, 0.5f);
    c.space = param (p.macro_space, 0.5f);
    c.driftMacro = param (p.macro_drift, 0.5f);

    c.phRate = param (p.ph_rate, 0.05f);
    c.phDepth = param (p.ph_depth, 0.6f);
    c.phFeedback = param (p.ph_feedback, 0.3f);
    c.phMix = param (p.ph_mix, 0.4f);
    c.phStages = paramInt (p.ph_stages, 6);
    c.chRate = param (p.ch_rate, 0.1f);
    c.chDepth = param (p.ch_depth, 3.0f);
    c.chMix = param (p.ch_mix, 0.25f);
    c.dlyTime = param (p.dly_time, 1.8f);
    c.dlyFb = param (p.dly_fb, 0.35f);
    c.dlyTone = param (p.dly_tone, 0.5f);
    c.dlyMix = param (p.dly_mix, 0.25f);
    c.dlyPingPong = param (p.dly_pingpong, 1.0f) > 0.5f;
    c.revSize = param (p.rev_size, 0.85f);
    c.revDecay = param (p.rev_decay, 9.0f);
    c.revDamp = param (p.rev_damp, 0.35f);
    c.revPreDelay = param (p.rev_predelay, 20.0f);
    c.revShimmer = param (p.rev_shimmer, 0.2f);
    c.revMod = param (p.rev_mod, 0.4f);
    c.revMix = param (p.rev_mix, 0.35f);

    c.ceiling = param (p.lim_ceiling, -1.0f);
    c.release = param (p.lim_release, 200.0f);
    c.masterDb = param (p.master, 0.0f);

    c.play = param (p.play, 0.0f) > 0.5f;
    c.playAttack = param (p.play_attack, 0.01f);
    c.playRelease = param (p.play_release, 2.5f);
    c.playVelocity = param (p.play_velocity, 0.6f);
    c.playDrone = param (p.play_drone, 0.0f);
    c.lowLevel = param (p.low_level, 0.5f);
    c.lowDecay = param (p.low_decay, 8.0f);

    const BandEdges e;
    c.bands.edges.floor = paramInt (p.band_floor, e.floor);
    c.bands.edges.x1 = paramInt (p.band_x1, e.x1);
    c.bands.edges.x2 = paramInt (p.band_x2, e.x2);
    c.bands.edges.x3 = paramInt (p.band_x3, e.x3);
    c.bands.edges.ceiling = paramInt (p.band_ceiling, e.ceiling);
    c.bands.edges.blend = param (p.band_blend, static_cast<float> (e.blend));
    c.bands.tones[0] = static_cast<Tones> (paramInt (p.red_tones, static_cast<int> (Tones::rootFifth)));
    c.bands.tones[1] = static_cast<Tones> (paramInt (p.green_tones, static_cast<int> (Tones::chord)));
    c.bands.tones[2] = static_cast<Tones> (paramInt (p.violet_tones, static_cast<int> (Tones::colours)));
    c.bands.shift[0] = paramInt (p.red_shift, 0);
    c.bands.shift[1] = paramInt (p.green_shift, 0);
    c.bands.shift[2] = paramInt (p.violet_shift, 0);

    // the EQ (T3.9): the defaults are Eq::Settings' (a test keeps parameters.yaml in step)
    const Eq::Settings eq;
    const auto band = [&eq] (int b) -> const Eq::Band& { return eq.bands[static_cast<size_t> (b)]; };
    c.eq.bands[0] = { param (p.eq1_freq, band (0).hz), param (p.eq1_gain, 0.0f), Eq::kShelfQ, param (p.eq1_drive, band (0).drive) };
    c.eq.bands[1] = { param (p.eq2_freq, band (1).hz), param (p.eq2_gain, 0.0f), param (p.eq2_q, band (1).q), param (p.eq2_drive, band (1).drive) };
    c.eq.bands[2] = { param (p.eq3_freq, band (2).hz), param (p.eq3_gain, 0.0f), param (p.eq3_q, band (2).q), param (p.eq3_drive, band (2).drive) };
    c.eq.bands[3] = { param (p.eq4_freq, band (3).hz), 0.0f, Eq::kCutQ, param (p.eq4_drive, band (3).drive) };

    // T4.2: the LFOs and the envelope
    c.lfoRate[0] = param (p.lfo1_rate, 0.12f);
    c.lfoRate[1] = param (p.lfo2_rate, 0.03f);
    c.lfoShape[0] = paramInt (p.lfo1_shape, 0);
    c.lfoShape[1] = paramInt (p.lfo2_shape, 2);
    c.envAttack = param (p.env_attack, 0.1f);
    c.envDecay = param (p.env_decay, 0.6f);
    c.envSustain = param (p.env_sustain, 0.35f);
    c.envRelease = param (p.env_release, 1.5f);
    c.envTrigger = paramInt (p.env_trigger, 0);
}

void Instrument::snapControls (const Controls& t) noexcept
{
    // Chord controls snap: Harmony turns a change into a crossfaded chord change.
    controls.key = t.key;
    controls.mood = t.mood;
    controls.scale = t.scale;
    controls.shape = t.shape;
    controls.transpose = t.transpose;
    controls.bands = t.bands;   // SkySound waits until they are still, then crossfades (T3.8)
    controls.phStages = t.phStages;
    controls.dlyPingPong = t.dlyPingPong;
    // modes snap: Sources glides the mode
    controls.play = t.play;
    // the EQ snaps here and glides per sample itself (frequencies in log, τ 20 ms): a move then
    // lands on the same samples whatever the host's block size (T3.9)
    controls.eq = t.eq;
    // T4.2: the LFO keeps its phase through a rate change and crossfades a shape change; the
    // envelope's times act from its next tick on, continuously
    controls.lfoRate = t.lfoRate;
    controls.lfoShape = t.lfoShape;
    controls.envAttack = t.envAttack;
    controls.envDecay = t.envDecay;
    controls.envSustain = t.envSustain;
    controls.envRelease = t.envRelease;
    controls.envTrigger = t.envTrigger;
}

void Instrument::glideControls (const Controls& t, int numSamples) noexcept
{
    const float k = numSamples > 0
        ? 1.0f - std::exp (-static_cast<float> (numSamples) / static_cast<float> (kGlideSeconds * sampleRate))
        : 1.0f;
    const auto glide = [k] (float& v, float to) noexcept { v += (to - v) * k; };

    glide (controls.progression, t.progression);
    glide (controls.detune, t.detune);
    glide (controls.drift, t.drift);
    glide (controls.width, t.width);
    for (size_t l = 0; l < kNumLayers; ++l)
    {
        glide (controls.level[l], t.level[l]);
        glide (controls.t60[l], t.t60[l]);
    }
    glide (controls.storm, t.storm);
    glide (controls.breath, t.breath);
    glide (controls.glow, t.glow);
    glide (controls.space, t.space);
    glide (controls.driftMacro, t.driftMacro);

    glide (controls.phRate, t.phRate);
    glide (controls.phDepth, t.phDepth);
    glide (controls.phFeedback, t.phFeedback);
    glide (controls.phMix, t.phMix);
    glide (controls.chRate, t.chRate);
    glide (controls.chDepth, t.chDepth);
    glide (controls.chMix, t.chMix);
    glide (controls.dlyTime, t.dlyTime);
    glide (controls.dlyFb, t.dlyFb);
    glide (controls.dlyTone, t.dlyTone);
    glide (controls.dlyMix, t.dlyMix);
    glide (controls.revSize, t.revSize);
    glide (controls.revDecay, t.revDecay);
    glide (controls.revDamp, t.revDamp);
    glide (controls.revPreDelay, t.revPreDelay);
    glide (controls.revShimmer, t.revShimmer);
    glide (controls.revMod, t.revMod);
    glide (controls.revMix, t.revMix);
    glide (controls.ceiling, t.ceiling);
    glide (controls.release, t.release);
    glide (controls.masterDb, t.masterDb);
    glide (controls.playAttack, t.playAttack);
    glide (controls.playRelease, t.playRelease);
    glide (controls.playVelocity, t.playVelocity);
    glide (controls.playDrone, t.playDrone);
    glide (controls.lowLevel, t.lowLevel);
    glide (controls.lowDecay, t.lowDecay);
}

SkySound::Settings Instrument::settingsFrom (const Controls& c, const mod::Frame& f) const noexcept
{
    SkySound::Settings s;
    s.voicesPerNode = 1;
    s.bands = c.bands;

    // T4.2: the levels, detune, drift and width arrive modulated (Breath and Glow on the levels,
    // Drift on detune and drift: dsp/mod/ModMatrix.h's default grid)
    s.level[0] = f[Target::redLevel];
    s.level[1] = f[Target::greenLevel];
    s.level[2] = f[Target::violetLevel];
    for (size_t l = 0; l < kNumLayers; ++l)
        s.t60[l] = c.t60[l];
    s.detuneCents = f[Target::detune];
    s.driftCents = f[Target::drift];

    // Strike (Storm's route): the exciter's bed, rise and strike, where Storm's factor sat
    const Exciter::Settings unit;
    const double strike = f[Target::excite];
    s.exciter.bed = unit.bed * strike;
    s.exciter.rise = unit.rise * strike;
    s.exciter.strike = unit.strike * strike;
    s.width = f[Target::width];
    return s;
}

Sources::Settings Instrument::sourcesFrom (const Controls& c, const mod::Frame& f) const noexcept
{
    const auto sky = settingsFrom (c, f);
    Sources::Settings s;
    s.play = c.play;
    s.droneUnder = c.playDrone;
    s.voices.attack = c.playAttack;
    s.voices.release = c.playRelease;
    s.voices.velocity = c.playVelocity;
    s.voices.level = sky.level;
    s.voices.t60 = sky.t60;
    s.voices.transpose = c.transpose;
    s.bed = sky.exciter.bed;
    s.strike = sky.exciter.strike;
    s.detuneCents = sky.detuneCents;
    s.driftCents = sky.driftCents;
    s.lowLevel = f[Target::lowLevel];
    s.low.t60 = c.lowDecay;
    s.eq = c.eq;
    // T4.2: the gains, the cut (the sky opens it: Activity → Cut) and the drives, modulated
    s.eq.bands[0].gainDb = f[Target::eq1Gain];
    s.eq.bands[1].gainDb = f[Target::eq2Gain];
    s.eq.bands[2].gainDb = f[Target::eq3Gain];
    s.eq.bands[3].hz = f[Target::eq4Freq];
    s.eq.bands[0].drive = f[Target::eq1Drive];
    s.eq.bands[1].drive = f[Target::eq2Drive];
    s.eq.bands[2].drive = f[Target::eq3Drive];
    s.eq.bands[3].drive = f[Target::eq4Drive];
    return s;
}

/** Per block: what is not on the sky's clock (the chord's snapped controls, the output stage). */
void Instrument::applyControls() noexcept
{
    if (controls.play != autoGainHeld)
    {
        // Play mode: the player's dynamics stay theirs; the normaliser glides back to unity.
        autoGainHeld = controls.play;
        dsp::AutoGain::Settings ag;
        ag.hold = autoGainHeld;
        autoGain.setSettings (ag);
    }

    auto& h = skySound.harmony();
    if (controls.key != lastKey)              { h.setKey (controls.key); lastKey = controls.key; }
    if (controls.mood != lastMood)            { h.setMood (static_cast<Mood> (std::clamp (controls.mood, 0, kNumMoods - 1))); lastMood = controls.mood; }
    if (controls.scale != lastScale)          { h.setScale (static_cast<Scale> (std::clamp (controls.scale, 0, kNumScales - 1))); lastScale = controls.scale; }
    if (controls.shape != lastShape)          { h.setChordIndex (controls.shape); lastShape = controls.shape; }
    if (controls.transpose != lastTranspose)  { h.setTranspose (controls.transpose); lastTranspose = controls.transpose; }

    limiter.setSettings (0.0f, controls.ceiling, controls.release);
    masterGain = dbToGain (controls.masterDb);
}

//==================================================================================================
// T4.2: the grid on the sky's clock

void Instrument::configureMotion (const Controls& c) noexcept
{
    for (size_t i = 0; i < lfos.size(); ++i)
    {
        lfos[i].setRate (static_cast<double> (c.lfoRate[i]));
        lfos[i].setShape (lfoShapeOf (c.lfoShape[i]), prepared);   // before playback: at once
    }
    mod::Envelope::Settings es;
    es.attack = static_cast<double> (c.envAttack);
    es.decay = static_cast<double> (c.envDecay);
    es.sustain = static_cast<double> (c.envSustain);
    es.release = static_cast<double> (c.envRelease);
    es.trigger = triggerOf (c.envTrigger);
    env.setSettings (es);
}

mod::SourceValues Instrument::sourceValues (uint64_t tick) const noexcept
{
    mod::SourceValues v;
    v[mod::Source::storm] = macroSource (controls.storm);
    v[mod::Source::breath] = static_cast<float> (SkySound::breathCycleAt (tick)) * controls.breath;
    v[mod::Source::glow] = macroSource (controls.glow);
    v[mod::Source::space] = macroSource (controls.space);
    v[mod::Source::drift] = macroSource (controls.driftMacro);
    v[mod::Source::activity] = static_cast<float> (std::min (front.skyActivity(), 1.0));
    v[mod::Source::onset] = static_cast<float> (onsetSwell.value);
    v[mod::Source::lfo1] = lfos[0].value();
    v[mod::Source::lfo2] = lfos[1].value();
    v[mod::Source::envelope] = env.value();
    v.breathKnob = controls.breath;
    return v;
}

mod::Bases Instrument::basesFrom (const Controls& c) const noexcept
{
    mod::Bases b;
    b[Target::lowLevel] = c.lowLevel;
    b[Target::redLevel] = c.level[0];
    b[Target::greenLevel] = c.level[1];
    b[Target::violetLevel] = c.level[2];
    b[Target::detune] = c.detune;
    b[Target::drift] = c.drift;
    b[Target::width] = c.width;
    b[Target::excite] = mod::infoOf (Target::excite).min;   // no knob: the fixed base
    b[Target::eq1Gain] = c.eq.bands[0].gainDb;
    b[Target::eq2Gain] = c.eq.bands[1].gainDb;
    b[Target::eq3Gain] = c.eq.bands[2].gainDb;
    b[Target::eq4Freq] = c.eq.bands[3].hz;
    b[Target::eq1Drive] = c.eq.bands[0].drive;
    b[Target::eq2Drive] = c.eq.bands[1].drive;
    b[Target::eq3Drive] = c.eq.bands[2].drive;
    b[Target::eq4Drive] = c.eq.bands[3].drive;
    b[Target::phMix] = c.phMix;
    b[Target::chMix] = c.chMix;
    b[Target::dlyFb] = c.dlyFb;
    b[Target::dlyMix] = c.dlyMix;
    b[Target::revDecay] = c.revDecay;
    b[Target::revShimmer] = c.revShimmer;
    b[Target::revMix] = c.revMix;
    b[Target::progression] = c.progression;
    return b;
}

void Instrument::skyTicked (const sky::SkyModel& model, std::span<const sky::SkyEvent> events, int tickSamples) noexcept
{
    // 1. the controls glide on the tick grid; the activity slews over this tick
    glideControls (target, tickSamples);
    front.slewActivity (model, tickSamples);

    // 2. the moving sources: onsets swell Onset and fire the envelope (in Onset mode)
    for (const auto& e : events)
        if (e.type == sky::SkyEvent::Type::onset)
        {
            onsetSwell.trigger();
            env.onset();
        }
    onsetSwell.tick();
    configureMotion (controls);
    for (auto& l : lfos)
        l.tick();
    env.tick();

    // 3. the grid, on this tick's sources (the breath cycle of the tick SkySound is about to run)
    matrix.tick (sourceValues (skySound.ticks() + 1), basesFrom (controls), frame);
    applyFrame (false);
    ++modTicks;
    modTicked = true;

    // 4. the curtain and its companion, with the modulated settings
    skySound.tick (model, events, tickSamples);
    tickLength = tickSamples;
    intoTick = 0;
}

void Instrument::applyFrame (bool atOnce) noexcept
{
    skySound.setSettings (settingsFrom (controls, frame));
    front.setSettings (sourcesFrom (controls, frame));

    const float progression = frame[Target::progression];
    if (std::abs (progression - lastProgression) > 1.0e-4f)
    {
        skySound.harmony().setProgression (static_cast<double> (progression));
        lastProgression = progression;
    }

    // the effects glide over this tick from the last tick's values to these
    fxFrom = atOnce ? fxOf (frame) : fxTo;
    fxTo = fxOf (frame);
    fxMoving = fxFrom.differs (fxTo);
    setEffects (fxTo);   // when moving, renderEffects sets them on its grid
}

Instrument::FxValues Instrument::fxOf (const mod::Frame& f) const noexcept
{
    FxValues x;
    x.v = { f[Target::phMix], f[Target::chMix], f[Target::dlyFb], f[Target::dlyMix],
            f[Target::revDecay], f[Target::revShimmer], f[Target::revMix] };
    return x;
}

void Instrument::setEffects (const FxValues& x) noexcept
{
    Phaser::Settings ps;
    ps.rateHz = controls.phRate;
    ps.depth = controls.phDepth;
    ps.feedback = controls.phFeedback;
    ps.mix = x.v[0];
    ps.stages = controls.phStages;
    phaser.setSettings (ps);

    Chorus::Settings cs;
    cs.rateHz = controls.chRate;
    cs.depthMs = controls.chDepth;
    cs.mix = x.v[1];
    chorus.setSettings (cs);

    StereoDelay::Settings ds;
    ds.timeSeconds = controls.dlyTime;
    ds.feedback = x.v[2];
    ds.tone = controls.dlyTone;
    ds.mix = x.v[3];
    ds.pingPong = controls.dlyPingPong;
    delay.setSettings (ds);

    ShimmerReverb::Settings rs;
    rs.size = controls.revSize;
    rs.decaySeconds = x.v[4];
    rs.damping = controls.revDamp;
    rs.predelayMs = controls.revPreDelay;
    rs.shimmer = x.v[5];
    rs.modulation = controls.revMod;
    rs.mix = x.v[6];
    reverb.setSettings (rs);
}

void Instrument::renderSamples (int offset, int numSamples) noexcept
{
    skySound.render (offset, numSamples);
    renderEffects (outLeft + offset, outRight + offset, numSamples);
}

void Instrument::renderEffects (float* left, float* right, int numSamples) noexcept
{
    // A moving value glides linearly across the tick, set every kFxStepSamples counted from the
    // tick's first sample, so the pieces fall on the same samples at any block size (rule 5).
    for (int done = 0; done < numSamples;)
    {
        int piece = numSamples - done;
        if (fxMoving && tickLength > 0)
        {
            const int at = intoTick + done;
            const int next = std::min (tickLength, (at / kFxStepSamples + 1) * kFxStepSamples);
            piece = std::max (1, std::min (piece, next - at));
            const float w = std::min (1.0f, static_cast<float> (next) / static_cast<float> (tickLength));
            FxValues x;
            for (size_t i = 0; i < x.v.size(); ++i)
                x.v[i] = fxFrom.v[i] + (fxTo.v[i] - fxFrom.v[i]) * w;
            setEffects (x);
        }
        float* l = left + done;
        float* r = right + done;
        phaser.process (l, r, piece);
        chorus.process (l, r, piece);
        delay.process (l, r, piece);
        reverb.process (l, r, piece);
        done += piece;
    }
    intoTick += numSamples;
}

void Instrument::publishModulation() noexcept
{
    auto& t = modBuffer.back();
    t.frame = frame;
    for (size_t i = 0; i < lfos.size(); ++i)
        t.lfo[i] = lfos[i].view();
    t.envelope = env.view();
    t.tick = modTicks;
    modBuffer.publish();
}

//==================================================================================================
void Instrument::syncLatch() noexcept
{
    auto& h = skySound.harmony();

    // The latch and the chord hold reach the sound through the same path (T3.6): a chord that
    // differs from the one sounding is requested, urgent (the player means it). While a sky
    // request is already waiting and MIDI is not holding, leave it alone.
    Chord c;
    const bool latched = h.isLatched();
    if (h.poll (c) && ! sameChord (c, skySound.chord()) && (latched || ! h.hasPending()))
    {
        skySound.requestChord (c, true);
        h.discardPending();   // the sky's own pending (if any) is superseded; its target stays
    }
}

void Instrument::playEvent (const juce::MidiMessage& m) noexcept
{
    if (m.isNoteOn())
        front.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        front.noteOff (m.getNoteNumber());
    else if (m.isSustainPedalOn())
        front.sustain (true);
    else if (m.isSustainPedalOff())
        front.sustain (false);
    else if (m.isAllSoundOff())
        front.allSoundOff();
    else if (m.isAllNotesOff())
        front.allNotesOff();
}

void Instrument::renderSky (float* left, float* right, int numSamples) noexcept
{
    outLeft = left;
    outRight = right;
    skySound.setOutput (left, right);
    runtime.process (numSamples, this);
}

void Instrument::render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi, bool play) noexcept
{
    // Each event lands on its sample: render up to it, apply it, go on. The sky's tick grid is
    // absolute, so the split doesn't change the sound (rule 5). Every note reaches the envelope
    // there (T4.2: its Note trigger). In Play mode it plays; in drone mode it latches the chord,
    // the events of one sample together (T4.2: at their sample too, no longer at the block's start).
    auto& h = skySound.harmony();
    int pos = 0;
    bool latchPending = false;
    if (! play)
        syncLatch();   // as every block: a latch let go hands the sky's chord back
    for (const auto meta : midi)
    {
        const int at = std::clamp (meta.samplePosition, pos, numSamples);
        if (at > pos)
        {
            if (latchPending)
            {
                syncLatch();
                latchPending = false;
            }
            renderSky (left + pos, right + pos, at - pos);
            pos = at;
        }
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            env.noteOn (m.getNoteNumber());
        else if (m.isNoteOff())
            env.noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            env.allNotesOff();
        if (play)
        {
            playEvent (m);
        }
        else
        {
            if (m.isNoteOn())
                h.noteOn (m.getNoteNumber());
            else if (m.isNoteOff())
                h.noteOff (m.getNoteNumber());
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                h.allNotesOff();
            latchPending = true;
        }
    }
    if (latchPending)
        syncLatch();
    if (pos < numSamples)
        renderSky (left + pos, right + pos, numSamples - pos);
}

void Instrument::publishPlay() noexcept
{
    const int n = front.playVoices().view (playView);
    PlayState s {};
    s.count = n;
    for (int i = 0; i < n; ++i)
    {
        const auto& v = playView[static_cast<size_t> (i)];
        s.notes[static_cast<size_t> (i)] = static_cast<int16_t> (v.note);
        s.levels[static_cast<size_t> (i)] = v.level;
        s.held[static_cast<size_t> (i)] = v.held ? 1 : 0;
    }
    playBuffer.write (s);
}

void Instrument::publishChord() noexcept
{
    const bool play = controls.play;
    if (play)
    {
        // Play mode: the readout names the held notes (the latch's rule) and the voicing view shows
        // their partials, the layers in small.
        std::array<uint64_t, 2> mask {};
        int held = 0;
        const int n = front.playVoices().view (playView);
        for (int i = 0; i < n; ++i)
        {
            const auto& v = playView[static_cast<size_t> (i)];
            if (! v.held || v.note < 0 || v.note > 127)
                continue;
            mask[static_cast<size_t> (v.note / 64)] |= uint64_t { 1 } << (v.note % 64);
            ++held;
        }
        const auto& bands = skySound.bands();
        if (chordPublished && publishedPlay && mask == publishedHeld && bands == publishedBands)
            return;

        ChordState s {};
        s.play = true;
        putEdges (s, bands);
        s.held = held;
        s.changes = skySound.chordChanges();
        s.tick = static_cast<uint32_t> (skySound.ticks());
        s.key = skySound.harmony().key();
        Chord c;
        if (chordFromNotes (mask[0], mask[1], false, c))
        {
            const auto name = nameOf (c);
            s.label = name.label;
            s.roman = name.roman;
            s.key = c.key;
        }
        int k = 0;
        for (int note = 0; note < 128 && 2 * k + 1 < ChordState::kMaxNotes; ++note)
        {
            if (((mask[static_cast<size_t> (note / 64)] >> (note % 64)) & 1u) == 0)
                continue;
            // every layer gets 2 entries per note, so the view weighs them alike
            const auto put = [&s, k] (int layer, int a, int b)
            {
                s.notes[static_cast<size_t> (layer)][static_cast<size_t> (2 * k)] = static_cast<int16_t> (a);
                s.notes[static_cast<size_t> (layer)][static_cast<size_t> (2 * k + 1)] = static_cast<int16_t> (b);
            };
            put (0, note - 12, note - 12);
            put (1, note, note + 12);
            put (2, note + 19, note + 19);
            ++k;
        }
        s.voices = 2 * k;
        chordBuffer.write (s);
        chordPublished = true;
        publishedPlay = true;
        publishedHeld = mask;
        publishedBands = bands;
        return;
    }

    const bool latched = skySound.harmony().isLatched();
    const bool pending = skySound.hasPendingChord();
    const auto& bands = skySound.bankBands (skySound.currentBank());
    if (chordPublished && ! publishedPlay && publishedChanges == skySound.chordChanges()
        && publishedLatched == latched && publishedPending == pending && bands == publishedBands)
        return;

    const auto& chord = skySound.chord();
    ChordState s {};
    const auto name = nameOf (chord);
    s.label = name.label;
    s.roman = name.roman;
    s.key = chord.key;
    s.function = static_cast<int> (skySound.harmony().function());
    s.shape = skySound.harmony().chordIndex();
    s.voices = skySound.voices();
    s.bank = skySound.currentBank();
    s.latched = latched;
    s.pending = pending;
    s.changes = skySound.chordChanges();
    s.tick = static_cast<uint32_t> (skySound.ticks());
    putEdges (s, bands);
    const int v = std::min (s.voices, ChordState::kMaxNotes);
    for (int l = 0; l < kNumLayers; ++l)
        for (int i = 0; i < v; ++i)
            s.notes[static_cast<size_t> (l)][static_cast<size_t> (i)] =
                static_cast<int16_t> (skySound.note (s.bank, static_cast<Layer> (l), i));

    chordBuffer.write (s);
    chordPublished = true;
    publishedPlay = false;
    publishedChanges = s.changes;
    publishedLatched = latched;
    publishedPending = pending;
    publishedBands = bands;
}

//==================================================================================================
void Instrument::process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi) noexcept
{
    const int n = buffer.getNumSamples();
    if (n <= 0)
        return;

    const int channels = buffer.getNumChannels();
    if (! prepared || channels < 2)
    {
        buffer.clear();
        return;
    }

    readTarget (target);
    snapControls (target);   // the rest glides on the sky's ticks (skyTicked)

    // T4.2: a newer grid, and the Fire button
    if (const int slot = depthsIn.take(); slot >= 0)
    {
        matrix.setDepths (depthsIn.slot (slot));
        depthsIn.release (slot);
    }
    if (const auto fires = fireRequests.load (std::memory_order_acquire); fires != firesTaken)
    {
        firesTaken = fires;
        env.fire();
    }
    modTicked = false;

    applyControls();

    float* left = buffer.getWritePointer (0);
    float* right = buffer.getWritePointer (1);

    // Play mode (T3.7). Entering it lets go of the drone's MIDI latch (the sky's chord returns
    // under the notes); leaving it releases every played note.
    if (controls.play != lastPlay)
    {
        if (controls.play)
            skySound.harmony().allNotesOff();
        else
            front.allNotesOff();
        lastPlay = controls.play;
    }

    if (controls.play)
        syncLatch();   // a latch just cleared still hands the sky's chord back to the drone
    render (left, right, n, midi, controls.play);   // (drone mode syncs the latch there)   // the sky's ticks run the grid and the effects

    autoGain.process (left, right, n);
    for (int i = 0; i < n; ++i)
    {
        left[i] = dcL.process (left[i]);
        right[i] = dcR.process (right[i]);
    }
    juce::FloatVectorOperations::multiply (left, masterGain, n);
    juce::FloatVectorOperations::multiply (right, masterGain, n);
    const float minGain = limiter.process (left, right, n);

    float peakL = 0.0f, peakR = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        peakL = std::max (peakL, std::abs (left[i]));
        peakR = std::max (peakR, std::abs (right[i]));
    }
    telemetry.peakL.store (peakL);
    telemetry.peakR.store (peakR);
    telemetry.gainReduction.store (minGain);
    telemetry.autoGain.store (autoGain.currentGain());
    telemetry.reverbLevel.store (reverb.getWetLevel());
    telemetry.breath.store (static_cast<float> (skySound.breathCycle()));
    telemetry.lowNote.store (front.lowResonator().note());
    telemetry.lowLevel.store (static_cast<float> (front.lowResonator().outputGain()));
    telemetry.eqCutHz.store (front.eq().cutHz());
    telemetry.playMix.store (static_cast<float> (front.playMix()));
    telemetry.phaserPhase.store (phaser.lfoPhase());
    scope.push (left, right, n);

    publishChord();
    publishPlay();
    if (modTicked)
        publishModulation();
}

} // namespace worldtable::dsp
