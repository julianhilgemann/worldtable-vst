// Headless offline render: Worldtable processor → stereo WAV, plus signal statistics.
//
//   WorldtableRender --out renders/instrument.wav --seconds 45 [--rate 48000]
//                    [--set progression=0 --set mood=Bittersweet --set eq1_drive=0.5 ...]
//                    [--midi notes.mid] [--block 512] [--bands]
//                    [--sweep band_x2=33:96[:t0:t1] ...]
//   WorldtableRender --bands-of renders/x.wav       (the octave bands of an existing render, T3.9)
//
// --midi plays a Standard MIDI File into the processor at its sample positions (all tracks, the
// file's tempo map); with --set play=1 that is Play mode (T3.7). --bands prints the mono mix's
// octave-band levels (31.5 Hz … 16 kHz), for the low end and the EQ.
// --sweep moves a parameter in a straight line from one value to another, between t0 and t1
// seconds (default: the whole render), on a 0.1 s grid; the blocks are cut at the grid, so the
// same sweep lands on the same samples whatever --block is (T3.8: the band edges).
// The default path renders the plug-in as it plays: the Sky Model's bundled Journey (10–11 May
// 2024, 24×), SkySound, the full FX chain, the chord hold and the MIDI latch (T3.6). Values for
// --set are real parameter values (Hz, s, dB …); choices accept the index or the choice name.
//
// The `--sky` path is the T3.1–T3.5b validation renderer: it drives SkySound and the chain
// directly from a chosen day, time and speed, without the plug-in's parameters. A held-chord run
// keeps the sketch's 20 s pre-roll so the resonators already ring; a progression run starts at
// `from`. 2 s fade-in, 3 s fade-out.
//
//   WorldtableRender --sky --out renders/T3.1-storm.wav [--day 2024-05-10] [--from 17:20]
//                    [--speed 24] [--seconds 300] [--voices 64|128] [--seed 1] [--gain 0] [--dry]
//                    [--mood bright|bittersweet|nocturnal] [--root D] [--chord 0..2]
//                    [--transpose 0] [--edges 24,36,50,73,91] [--blend 1.5] [--progression 0..1]

#include "PluginProcessor.h"
#include "data/SkyDays.h"
#include "dsp/Harmony.h"
#include "dsp/SkySound.h"
#include "dsp/fx/AutoGain.h"
#include "dsp/fx/Chorus.h"
#include "dsp/fx/Eq.h"
#include "dsp/fx/Limiter.h"
#include "dsp/fx/Phaser.h"
#include "dsp/fx/StereoDelay.h"
#include "dsp/fx/ShimmerReverb.h"
#include "sky/SkyFeed.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <chrono>
#include <ctime>
#include <iostream>
#include <optional>
#include <vector>

namespace
{
int fail (const juce::String& message)
{
    std::cerr << "error: " << message << "\n";
    return 1;
}

/** Every event of every track, in seconds, in time order. */
bool loadMidi (const juce::File& file, juce::MidiMessageSequence& out)
{
    juce::FileInputStream in (file);
    juce::MidiFile midi;
    if (! in.openedOk() || ! midi.readFrom (in))
        return false;
    midi.convertTimestampTicksToSeconds();
    for (int t = 0; t < midi.getNumTracks(); ++t)
        out.addSequence (*midi.getTrack (t), 0.0);
    out.sort();
    return true;
}

/** Octave-band RMS levels of the mono mix (dBFS), skipping the first `skipSeconds`. */
void printBands (const juce::AudioBuffer<float>& audio, double rate, double skipSeconds)
{
    static constexpr double kCentres[] = { 31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0 };
    const int total = audio.getNumSamples();
    const int skip = std::min (total, static_cast<int> (skipSeconds * rate));
    std::cout << "bands:  ";
    for (const double fc : kCentres)
    {
        if (fc > 0.45 * rate)
            break;
        juce::dsp::IIR::Filter<float> a, b;   // two band-passes in series: steeper skirts
        a.coefficients = juce::dsp::IIR::Coefficients<float>::makeBandPass (rate, static_cast<float> (fc), 1.41f);
        b.coefficients = a.coefficients;
        double sum = 0.0;
        for (int i = 0; i < total; ++i)
        {
            const float x = 0.5f * (audio.getSample (0, i) + audio.getSample (1, i));
            const float y = b.processSample (a.processSample (x));
            if (i >= skip)
                sum += static_cast<double> (y) * y;
        }
        const double rms = std::sqrt (sum / std::max (1, total - skip));
        std::cout << (fc < 1000.0 ? juce::String (fc, fc < 100.0 ? 1 : 0) : juce::String (fc / 1000.0, 0) + "k") << " "
                  << juce::String (juce::Decibels::gainToDecibels (static_cast<float> (rms), -200.0f), 2) << "  ";
    }
    std::cout << "\n";
}

bool setParameter (juce::AudioProcessorValueTreeState& apvts, const juce::String& assignment)
{
    const auto id = assignment.upToFirstOccurrenceOf ("=", false, false).trim();
    const auto value = assignment.fromFirstOccurrenceOf ("=", false, false).trim();

    auto* param = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (id));
    if (param == nullptr)
        return false;

    float real = value.getFloatValue();
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
    {
        const int idx = choice->choices.indexOf (value, true);
        real = idx >= 0 ? static_cast<float> (idx) : value.getFloatValue();
    }
    else if (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
    {
        real = (value == "true" || value == "on" || value.getIntValue() != 0) ? 1.0f : 0.0f;
    }

    param->setValueNotifyingHost (param->convertTo0to1 (real));
    return true;
}

constexpr double kSweepStepSeconds = 0.1;

/** --sweep id=from:to[:t0:t1]: a straight line from `from` (at t0) to `to` (at t1), seconds. */
struct Sweep
{
    juce::String id;
    double from = 0.0, to = 0.0, t0 = 0.0, t1 = 0.0;

    /** The value at `t`, or nothing outside [t0, t1]. */
    std::optional<double> valueAt (double t) const
    {
        if (t < t0 - 1.0e-9 || t > t1 + 1.0e-9)
            return std::nullopt;
        return from + (to - from) * (t1 > t0 ? (t - t0) / (t1 - t0) : 1.0);
    }
};

bool parseSweep (const juce::String& text, double seconds, Sweep& out)
{
    out.id = text.upToFirstOccurrenceOf ("=", false, false).trim();
    const auto parts = juce::StringArray::fromTokens (text.fromFirstOccurrenceOf ("=", false, false), ":", "");
    if (out.id.isEmpty() || (parts.size() != 2 && parts.size() != 4))
        return false;
    out.from = parts[0].getDoubleValue();
    out.to = parts[1].getDoubleValue();
    out.t0 = parts.size() == 4 ? parts[2].getDoubleValue() : 0.0;
    out.t1 = parts.size() == 4 ? parts[3].getDoubleValue() : seconds;
    return out.t1 >= out.t0;
}

std::unique_ptr<juce::AudioFormatWriter> createWav (const juce::File& out, double rate)
{
    out.getParentDirectory().createDirectory();
    out.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (out.createOutputStream());
    if (stream == nullptr)
        return {};
    juce::WavAudioFormat wav;
    return wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                            .withSampleRate (rate)
                                            .withNumChannels (2)
                                            .withBitsPerSample (24));
}

struct SkyRenderOptions
{
    juce::String day = worldtable::data::kDefaultSkyDay;
    juce::String from = "00:00";        // UT on the day's first date, or a full ISO time
    double speed = 24.0, seconds = 300.0, rate = 48000.0, gainDb = 0.0;
    int voices = 64;
    uint32_t seed = 1;
    bool dry = false;
    // The held chord (T3.2): the sketch's default is bright, root D, chord 0 → Dmaj7.
    worldtable::dsp::Mood mood = worldtable::dsp::Mood::bright;
    int root = 2, chord = 0, transpose = 0;
    worldtable::dsp::BandEdges edges;   // T3.8: the default bands
    double progression = 0.0;           // 0: hold the chord; 1: the sky writes the progression
};

bool parseMood (const juce::String& text, worldtable::dsp::Mood& mood)
{
    const auto t = text.trim().toLowerCase();
    if (t == "bright")      { mood = worldtable::dsp::Mood::bright; return true; }
    if (t == "bittersweet") { mood = worldtable::dsp::Mood::bittersweet; return true; }
    if (t == "nocturnal")   { mood = worldtable::dsp::Mood::nocturnal; return true; }
    return false;
}

/** "D", "F#", "Bb" or 0–11. */
int parseRoot (const juce::String& text)
{
    static const char* names[] = { "C", "C#", "DB", "D", "D#", "EB", "E", "F", "F#", "GB", "G", "G#", "AB", "A", "A#", "BB", "B" };
    static const int pcs[] = { 0, 1, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 8, 9, 10, 10, 11 };
    auto up = text.trim().toUpperCase().replace ("\xE2\x99\xAD", "B").replace ("\xE2\x99\xAF", "#");
    if (up.isEmpty())
        return -1;
    if (up.containsOnly ("0123456789"))
    {
        const int pc = up.getIntValue();
        return pc >= 0 && pc < 12 ? pc : -1;
    }
    for (size_t i = 0; i < std::size (names); ++i)
        if (up == names[i])
            return pcs[i];
    return -1;
}

int renderSky (const juce::File& out, const SkyRenderOptions& o)
{
    using namespace worldtable;
    constexpr double kPreRollSeconds = 20.0, kFadeInSeconds = 2.0, kFadeOutSeconds = 3.0;
    constexpr int block = 512;

    sky::SkyRuntime runtime;
    sky::SkyFeed feed (runtime);
    const sky::SkyParams params;
    if (! feed.load (o.day, o.seed, params))
        return fail ("unknown day " + o.day);

    const double startUtc = o.from.containsChar ('T')
        ? static_cast<double> (juce::Time::fromISO8601 (o.from + (o.from.endsWithChar ('Z') ? "" : "Z")).toMilliseconds()) / 1000.0
        : feed.dayStartUtc() + 3600.0 * o.from.upToFirstOccurrenceOf (":", false, false).getIntValue()
                             + 60.0 * o.from.fromFirstOccurrenceOf (":", false, false).getIntValue();

    // A progression run starts exactly at `from` (as the golden does); the held-chord run keeps the
    // 20 s pre-roll so its resonators are already ringing.
    const double preRollDuration = o.progression > 0.0 ? 0.0 : kPreRollSeconds;
    const double modelPerTick = sky::SkyModel::kTickSeconds * o.speed;
    const auto wantTicks = static_cast<int64_t> (std::lround (preRollDuration / sky::SkyModel::kTickSeconds));
    const auto preTicks = o.speed > 0.0 ? std::min (wantTicks, static_cast<int64_t> ((startUtc - feed.dayStartUtc()) / modelPerTick))
                                        : wantTicks;
    runtime.seek (startUtc - static_cast<double> (preTicks) * modelPerTick);
    runtime.setSpeed (o.speed);
    runtime.prepare (o.rate);
    runtime.process (0, nullptr);   // apply the seek, so the phase at `from` is known

    dsp::SkySound sound;
    dsp::SkySound::Settings settings;
    settings.voicesPerNode = o.voices > params.nodes ? 2 : 1;
    settings.bands.edges = o.edges;

    auto& harmony = sound.harmony();
    harmony.setSeed (o.seed);
    harmony.setMood (o.mood);
    harmony.setKey (o.root);
    harmony.setChordIndex (o.chord);
    harmony.setTranspose (o.transpose);
    harmony.setProgression (o.progression);
    harmony.start (static_cast<dsp::Phase> (runtime.model().current().phase));
    const dsp::Chord startChord = harmony.chord();
    const dsp::ChordName startName = dsp::nameOf (startChord);
    sound.prepare (o.rate, params.nodes, o.seed, settings, startChord);

    dsp::Eq eq;   // T3.9: the plug-in's EQ at its defaults (flat, the cut open, the default drives)
    eq.prepare (o.rate);

    dsp::Phaser phaser;
    phaser.prepare (o.rate);
    dsp::Phaser::Settings ps;
    ps.rateHz = 0.05f;
    ps.depth = 0.6f;
    ps.feedback = 0.3f;
    ps.mix = 0.4f;
    ps.stages = 6;
    phaser.setSettings (ps);

    dsp::Chorus chorus;
    chorus.prepare (o.rate);
    dsp::Chorus::Settings cs;
    cs.rateHz = 0.1f;
    cs.depthMs = 3.0f;
    cs.mix = 0.25f;
    chorus.setSettings (cs);

    dsp::StereoDelay delay;
    delay.prepare (o.rate);
    dsp::StereoDelay::Settings ds;
    ds.timeSeconds = 1.8f;
    ds.feedback = 0.35f;
    ds.tone = 0.5f;
    ds.mix = 0.25f;
    delay.setSettings (ds);

    dsp::ShimmerReverb reverb;
    reverb.prepare (o.rate);
    dsp::ShimmerReverb::Settings rs;
    rs.size = 0.85f;
    rs.decaySeconds = 9.0f;
    rs.damping = 0.35f;
    rs.predelayMs = 20.0f;
    rs.shimmer = 0.2f;
    rs.modulation = 0.4f;
    rs.mix = 0.35f;
    reverb.setSettings (rs);

    dsp::AutoGain autoGain;
    autoGain.prepare (o.rate);

    dsp::DcBlocker dcL, dcR;
    dcL.prepare (o.rate);
    dcR.prepare (o.rate);

    dsp::Limiter limiter;
    limiter.prepare (o.rate);
    limiter.setSettings (0.0f, -1.0f, 200.0f);
    const auto gain = static_cast<float> (juce::Decibels::decibelsToGain (o.gainDb));

    juce::AudioBuffer<float> buffer (2, block);
    double soundSeconds = 0.0, fxSeconds = 0.0;
    const auto timed = [] (double& total, auto&& fn)
    {
        const auto t0 = std::chrono::steady_clock::now();
        fn();
        total += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    };
    const auto renderBlock = [&] (int n)
    {
        float* l = buffer.getWritePointer (0);
        float* r = buffer.getWritePointer (1);
        timed (soundSeconds, [&] { sound.process (runtime, l, r, n); });
        if (o.dry)
            return;
        timed (fxSeconds, [&]
        {
            eq.process (l, r, n);
            phaser.process (l, r, n);
            chorus.process (l, r, n);
            delay.process (l, r, n);
            reverb.process (l, r, n);
            autoGain.process (l, r, n);
            for (int k = 0; k < n; ++k)
            {
                l[k] = dcL.process (l[k]);
                r[k] = dcR.process (r[k]);
            }
            juce::FloatVectorOperations::multiply (l, gain, n);
            juce::FloatVectorOperations::multiply (r, gain, n);
            limiter.process (l, r, n);
        });
    };

    for (auto pre = static_cast<int64_t> (std::llround (static_cast<double> (preTicks) * o.rate / sky::SkyModel::kTicksPerSecond)); pre > 0;)
    {
        const int n = static_cast<int> (std::min<int64_t> (block, pre));
        renderBlock (n);
        pre -= n;
    }
    const auto preRollSeconds = soundSeconds + fxSeconds;
    soundSeconds = fxSeconds = 0.0;

    auto writer = createWav (out, o.rate);
    if (writer == nullptr)
        return fail ("cannot write " + out.getFullPathName());

    const auto total = static_cast<int64_t> (std::llround (o.seconds * o.rate));
    const double fadeIn = kFadeInSeconds * o.rate, fadeOut = kFadeOutSeconds * o.rate;
    double sumSq = 0.0;
    float peak = 0.0f, maxDelta = 0.0f, prevL = 0.0f, prevR = 0.0f;
    int64_t nonFinite = 0;
    const auto chordsBefore = sound.chordChanges();
    const auto onsetsBefore = runtime.model().current().onsetCount;
    for (int64_t at = 0; at < total; at += block)
    {
        const int n = static_cast<int> (std::min<int64_t> (block, total - at));
        buffer.setSize (2, n, false, false, true);
        renderBlock (n);
        for (int ch = 0; ch < 2; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            for (int k = 0; k < n; ++k)
            {
                const auto i = static_cast<double> (at + k);
                const double fade = std::min ({ 1.0, i < fadeIn ? 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * i / fadeIn) : 1.0,
                                                static_cast<double> (total) - i < fadeOut
                                                    ? 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (static_cast<double> (total) - i) / fadeOut) : 1.0 });
                d[k] = static_cast<float> (d[k] * fade);
                if (! std::isfinite (d[k])) { ++nonFinite; continue; }
                sumSq += static_cast<double> (d[k]) * d[k];
                peak = std::max (peak, std::abs (d[k]));
                float& prev = ch == 0 ? prevL : prevR;
                if (at + k > 0)
                    maxDelta = std::max (maxDelta, std::abs (d[k] - prev));
                prev = d[k];
            }
        }
        writer->writeFromAudioSampleBuffer (buffer, 0, n);
    }
    writer.reset();

    const auto rms = std::sqrt (sumSq / (2.0 * static_cast<double> (total)));
    const auto hhmm = [] (double utc)
    {
        const auto when = static_cast<std::time_t> (std::floor (utc));
        std::tm parts {};
       #if JUCE_WINDOWS
        gmtime_s (&parts, &when);
       #else
        gmtime_r (&when, &parts);
       #endif
        char text[32] {};
        std::strftime (text, sizeof (text), "%Y-%m-%d %H:%M", &parts);
        return std::string (text);
    };
    std::cout << "sky:     " << o.day << " from " << hhmm (startUtc) << " to " << hhmm (runtime.model().modelTimeUtc())
              << " UT at " << o.speed << "x, seed " << o.seed << ", " << sound.voices() << " voices per layer\n"
              << "chord:   " << startName.label.data() << " (" << startName.roman.data() << ") start, progression "
              << juce::String (o.progression, 2) << "; "
              << (sound.chordChanges() - chordsBefore) << " changes -> "
              << dsp::nameOf (sound.chord()).label.data() << ", "
              << (runtime.model().current().onsetCount - onsetsBefore) << " onsets in the file\n"
              << "length:  " << o.seconds << " s @ " << o.rate << " Hz" << (o.dry ? " (dry)"
                                                                               : " (eq -> phaser -> chorus -> delay -> shimmer -> autogain -> dc -> limiter)") << "\n"
              << "peak:    " << juce::Decibels::gainToDecibels (peak, -200.0f) << " dBFS\n"
              << "rms:     " << juce::Decibels::gainToDecibels (static_cast<float> (rms), -200.0f) << " dBFS\n"
              << "maxStep: " << maxDelta << "\n"
              << "nonFinite samples: " << nonFinite << "\n"
              << "resets:  " << sound.resets() << "\n"
              << "cpu:     sky + sound " << juce::String (100.0 * soundSeconds / o.seconds, 2) << " % of real time"
              << (o.dry ? juce::String() : ", fx chain " + juce::String (100.0 * fxSeconds / o.seconds, 2) + " %")
              << " (pre-roll " << juce::String (preRollSeconds, 2) << " s)\n"
              << "wrote:   " << out.getFullPathName() << "\n";
    return nonFinite == 0 ? 0 : 2;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;

    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile ("renders/render.wav");
    double seconds = 30.0, rate = 48000.0;
    bool skyMode = false, secondsGiven = false, bands = false;
    int block = 512;
    juce::File midiFile;
    juce::StringArray sets, sweepArgs;
    SkyRenderOptions sky;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        const auto next = [&]() { return i + 1 < argc ? juce::String (argv[++i]) : juce::String(); };
        if (a == "--out")          out = juce::File::getCurrentWorkingDirectory().getChildFile (next());
        else if (a == "--seconds") { seconds = next().getDoubleValue(); secondsGiven = true; }
        else if (a == "--rate")    rate = next().getDoubleValue();
        else if (a == "--set")     sets.add (next());
        else if (a == "--midi")    midiFile = juce::File::getCurrentWorkingDirectory().getChildFile (next());
        else if (a == "--block")   block = juce::jlimit (16, 8192, next().getIntValue());
        else if (a == "--bands")   bands = true;
        else if (a == "--sky")     skyMode = true;
        else if (a == "--day")     sky.day = next();
        else if (a == "--from")    sky.from = next();
        else if (a == "--speed")   sky.speed = next().getDoubleValue();
        else if (a == "--voices")  sky.voices = next().getIntValue();
        else if (a == "--seed")    sky.seed = static_cast<uint32_t> (next().getLargeIntValue());
        else if (a == "--gain")    sky.gainDb = next().getDoubleValue();
        else if (a == "--dry")     sky.dry = true;
        else if (a == "--mood")
        {
            if (! parseMood (next(), sky.mood))
                return fail ("--mood must be bright, bittersweet or nocturnal");
        }
        else if (a == "--root")
        {
            sky.root = parseRoot (next());
            if (sky.root < 0)
                return fail ("--root must be a pitch class 0-11 or a note name (C, F#, Bb, ...)");
        }
        else if (a == "--chord")   sky.chord = juce::jlimit (0, 2, next().getIntValue());
        else if (a == "--transpose") sky.transpose = next().getIntValue();
        else if (a == "--edges")
        {
            const auto parts = juce::StringArray::fromTokens (next(), ",", "");
            if (parts.size() != 5)
                return fail ("--edges takes five MIDI notes: floor,x1,x2,x3,ceiling");
            sky.edges.floor = parts[0].getIntValue();
            sky.edges.x1 = parts[1].getIntValue();
            sky.edges.x2 = parts[2].getIntValue();
            sky.edges.x3 = parts[3].getIntValue();
            sky.edges.ceiling = parts[4].getIntValue();
        }
        else if (a == "--blend")   sky.edges.blend = next().getDoubleValue();
        else if (a == "--sweep")   sweepArgs.add (next());
        else if (a == "--bands-of")
        {
            // the octave bands of a render already made (to compare with one from another build)
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true));
            if (reader == nullptr || reader->numChannels < 2)
                return fail ("cannot read a stereo WAV from " + file.getFullPathName());
            juce::AudioBuffer<float> audio (2, static_cast<int> (reader->lengthInSamples));
            reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
            std::cout << "file:    " << file.getFileName() << ", " << audio.getNumSamples() / reader->sampleRate << " s\n";
            printBands (audio, reader->sampleRate, std::min (2.0, 0.25 * audio.getNumSamples() / reader->sampleRate));
            return 0;
        }
        else if (a == "--progression") sky.progression = juce::jlimit (0.0, 1.0, next().getDoubleValue());
        else return fail ("unknown argument " + a);
    }

    if (skyMode)
    {
        sky.rate = rate;
        if (secondsGiven)
            sky.seconds = seconds;
        return renderSky (out, sky);
    }

    worldtable::PluginProcessor processor (false);
    for (const auto& s : sets)
        if (! setParameter (processor.getValueTreeState(), s))
            return fail ("unknown parameter in --set " + s);

    juce::MidiMessageSequence events;
    if (midiFile != juce::File() && ! loadMidi (midiFile, events))
        return fail ("cannot read MIDI file " + midiFile.getFullPathName());

    std::vector<Sweep> sweeps;
    for (const auto& s : sweepArgs)
    {
        Sweep sw;
        if (! parseSweep (s, seconds, sw) || processor.getValueTreeState().getParameter (sw.id) == nullptr)
            return fail ("--sweep wants id=from:to[:t0:t1] with a known parameter, not " + s);
        sweeps.push_back (sw);
    }

    processor.setPlayConfigDetails (0, 2, rate, block);
    processor.prepareToPlay (rate, block);

    const auto total = static_cast<int> (seconds * rate);
    juce::AudioBuffer<float> result (2, total);
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;

    const auto t0 = std::chrono::steady_clock::now();
    const auto grid = std::max (1, static_cast<int> (std::lround (kSweepStepSeconds * rate)));
    int nextEvent = 0;
    for (int pos = 0, n = 0; pos < total; pos += n)
    {
        n = std::min (block, total - pos);
        if (! sweeps.empty())
        {
            if (pos % grid == 0)
                for (const auto& sw : sweeps)
                    if (const auto value = sw.valueAt (pos / rate))
                        setParameter (processor.getValueTreeState(), sw.id + "=" + juce::String (*value));
            n = std::min (n, grid - pos % grid);   // the next grid point starts a block
        }
        buffer.setSize (2, n, false, false, true);
        midi.clear();
        for (; nextEvent < events.getNumEvents(); ++nextEvent)
        {
            const auto& m = events.getEventPointer (nextEvent)->message;
            const auto at = static_cast<int> (std::llround (m.getTimeStamp() * rate));
            if (at >= pos + n)
                break;
            midi.addEvent (m, std::max (0, at - pos));
        }
        processor.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            result.copyFrom (ch, pos, buffer, ch, 0, n);
    }
    const auto elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    processor.releaseResources();

    // statistics
    double sumSq = 0.0;
    float peak = 0.0f, maxDelta = 0.0f;
    int nonFinite = 0;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* d = result.getReadPointer (ch);
        for (int i = 0; i < total; ++i)
        {
            if (! std::isfinite (d[i])) { ++nonFinite; continue; }
            sumSq += static_cast<double> (d[i]) * d[i];
            peak = std::max (peak, std::abs (d[i]));
            if (i > 0)
                maxDelta = std::max (maxDelta, std::abs (d[i] - d[i - 1]));
        }
    }
    const auto rms = std::sqrt (sumSq / (2.0 * total));
    const auto name = processor.getInstrument().currentChordName();
    if (! sweeps.empty())
    {
        // where the band sweep left the instrument (T3.8)
        const auto& b = processor.getInstrument().sound().bands();
        std::cout << "bands:   applied edges " << b.edges.floor << " " << b.edges.x1 << " " << b.edges.x2 << " "
                  << b.edges.x3 << " " << b.edges.ceiling << ", blend " << b.edges.blend << "; "
                  << processor.getInstrument().chordChanges() << " chord changes and re-voicings\n";
    }

    std::cout << "journey: " << processor.getSkyFeed().dayId() << ", " << processor.getInstrument().voices()
              << " voices per layer, chord " << juce::String::fromUTF8 (name.label.data())
              << " (" << juce::String::fromUTF8 (name.roman.data()) << ")\n"
              << "length:  " << seconds << " s @ " << rate << " Hz\n"
              << "peak:    " << juce::Decibels::gainToDecibels (peak, -200.0f) << " dBFS\n"
              << "rms:     " << juce::Decibels::gainToDecibels (static_cast<float> (rms), -200.0f) << " dBFS\n"
              << "maxStep: " << maxDelta << "\n"
              << "nonFinite samples: " << nonFinite << "\n"
              << "cpu:     " << juce::String (100.0 * elapsed / seconds, 2) << " % of real time\n";
    if (bands)
        printBands (result, rate, std::min (2.0, 0.25 * seconds));

    auto writer = createWav (out, rate);
    if (writer == nullptr)
        return fail ("cannot write " + out.getFullPathName());

    writer->writeFromAudioSampleBuffer (result, 0, total);
    writer.reset();
    std::cout << "wrote:   " << out.getFullPathName() << "\n";
    return nonFinite == 0 ? 0 : 2;
}
