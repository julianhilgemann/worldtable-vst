// Headless offline render: Worldtable processor → stereo WAV, plus signal statistics.
//
//   WorldtableRender --out renders/drone.wav --seconds 45 [--rate 48000] [--live]
//                    [--set trig_mode=Euclid --set wt_source=4 --set flt_cutoff=900 ...]
//
// Values for --set are real parameter values (Hz, s, dB …); choices accept the
// index or the choice name. Without --live only the bundled factory world is used
// (deterministic, no network).

#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <chrono>
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
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;

    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile ("renders/render.wav");
    double seconds = 30.0, rate = 48000.0;
    bool live = false;
    juce::StringArray sets;

    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        const auto next = [&]() { return i + 1 < argc ? juce::String (argv[++i]) : juce::String(); };
        if (a == "--out")          out = juce::File::getCurrentWorkingDirectory().getChildFile (next());
        else if (a == "--seconds") seconds = next().getDoubleValue();
        else if (a == "--rate")    rate = next().getDoubleValue();
        else if (a == "--set")     sets.add (next());
        else if (a == "--live")    live = true;
        else return fail ("unknown argument " + a);
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

    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (out.createOutputStream());
    if (stream == nullptr)
        return fail ("cannot write " + out.getFullPathName());

    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (rate)
                                                   .withNumChannels (2)
                                                   .withBitsPerSample (24));
    if (writer == nullptr)
        return fail ("cannot create WAV writer");

    writer->writeFromAudioSampleBuffer (result, 0, total);
    writer.reset();
    std::cout << "wrote:   " << out.getFullPathName() << "\n";
    return nonFinite == 0 ? 0 : 2;
}
