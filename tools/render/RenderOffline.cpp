// Headless offline render: Worldtable processor → stereo WAV, plus signal statistics.
//
//   WorldtableRender --out renders/drone.wav --seconds 45 [--rate 48000] [--live]
//                    [--set trig_mode=Euclid --set wt_source=4 --set flt_cutoff=900 ...]
//
// Values for --set are real parameter values (Hz, s, dB …); choices accept the
// index or the choice name. Without --live only the bundled factory world is used
// (deterministic, no network).
//
// The sky's sound (T3.1–T3.3, not in the plug-in yet): the Sky Model playing a bundled day through
// SkySound and the spec's chain (T3.5a): Drive → tape delay → shimmer reverb → AutoGain → DC →
// limiter. Phaser/Chorus and the shimmer's fifth are T3.5b. A held-chord run keeps the sketch's
// 20 s pre-roll so the resonators already ring; a progression run starts at `from`. 2 s fade-in,
// 3 s fade-out.
//
//   WorldtableRender --sky --out renders/T3.1-storm.wav [--day 2024-05-10] [--from 17:20]
//                    [--speed 24] [--seconds 300] [--voices 64|128] [--seed 1] [--gain 0] [--dry]
//                    [--mood bright|bittersweet|nocturnal] [--root D] [--chord 0..2]
//                    [--transpose 0] [--register 0] [--spread 2] [--progression 0..1]

#include "PluginProcessor.h"
#include "data/SkyDays.h"
#include "dsp/Harmony.h"
#include "dsp/SkySound.h"
#include "dsp/Drive.h"
#include "dsp/fx/AutoGain.h"
#include "dsp/fx/Chorus.h"
#include "dsp/fx/Limiter.h"
#include "dsp/fx/Phaser.h"
#include "dsp/fx/StereoDelay.h"
#include "dsp/fx/ShimmerReverb.h"
#include "sky/SkyFeed.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <chrono>
#include <ctime>
#include <iostream>

namespace
{
int fail (const juce::String& message)
{
    std::cerr << "error: " << message << "\n";
    return 1;
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
    double registerSemitones = 0.0, spread = 2.0;
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
    settings.registerSemitones = o.registerSemitones;
    settings.spread = o.spread;

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

    dsp::Drive drive;
    drive.prepare (o.rate);
    drive.setDrive (0.15f);

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
            for (int k = 0; k < n; ++k)
            {
                l[k] = drive.process (l[k]);
                r[k] = drive.process (r[k]);
            }
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
                                                                               : " (drive -> phaser -> chorus -> delay -> shimmer -> autogain -> dc -> limiter)") << "\n"
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
    bool live = false, skyMode = false, secondsGiven = false;
    juce::StringArray sets;
    SkyRenderOptions sky;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        const auto next = [&]() { return i + 1 < argc ? juce::String (argv[++i]) : juce::String(); };
        if (a == "--out")          out = juce::File::getCurrentWorkingDirectory().getChildFile (next());
        else if (a == "--seconds") { seconds = next().getDoubleValue(); secondsGiven = true; }
        else if (a == "--rate")    rate = next().getDoubleValue();
        else if (a == "--set")     sets.add (next());
        else if (a == "--live")    live = true;
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
        else if (a == "--register") sky.registerSemitones = next().getDoubleValue();
        else if (a == "--spread")  sky.spread = next().getDoubleValue();
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

    worldtable::PluginProcessor processor (live);
    for (const auto& s : sets)
        if (! setParameter (processor.getValueTreeState(), s))
            return fail ("unknown parameter in --set " + s);

    constexpr int block = 512;
    processor.setPlayConfigDetails (0, 2, rate, block);
    processor.prepareToPlay (rate, block);

    if (live)
    {
        // give the data thread time to fetch and build
        for (int i = 0; i < 100 && processor.getWorldFeed().getStatus().state != "live"
                                && processor.getWorldFeed().getStatus().state != "offline"; ++i)
            juce::Thread::sleep (100);
        juce::Thread::sleep (300);
        std::cout << "feed: " << processor.getWorldFeed().getStatus().state << " - "
                  << processor.getWorldFeed().getStatus().message << "\n";
    }

    const auto total = static_cast<int> (seconds * rate);
    juce::AudioBuffer<float> result (2, total);
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;

    const auto t0 = std::chrono::steady_clock::now();
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        buffer.setSize (2, n, false, false, true);
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
    const auto info = processor.getWorldFeed().getTableInfo();

    std::cout << "table:   " << (info ? info->station + " " + info->place + " (" + info->origin + ", "
                                        + juce::String (info->preview.numFrames) + " frames)" : juce::String ("none")) << "\n"
              << "length:  " << seconds << " s @ " << rate << " Hz\n"
              << "peak:    " << juce::Decibels::gainToDecibels (peak, -200.0f) << " dBFS\n"
              << "rms:     " << juce::Decibels::gainToDecibels (static_cast<float> (rms), -200.0f) << " dBFS\n"
              << "maxStep: " << maxDelta << "\n"
              << "nonFinite samples: " << nonFinite << "\n"
              << "cpu:     " << juce::String (100.0 * elapsed / seconds, 2) << " % of real time\n";

    auto writer = createWav (out, rate);
    if (writer == nullptr)
        return fail ("cannot write " + out.getFullPathName());

    writer->writeFromAudioSampleBuffer (result, 0, total);
    writer.reset();
    std::cout << "wrote:   " << out.getFullPathName() << "\n";
    return nonFinite == 0 ? 0 : 2;
}
