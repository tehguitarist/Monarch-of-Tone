// Offline render: runs a WAV through the real MonarchAudioProcessor (Yellow channel only, Red
// bypassed) at given knob settings, for A/B validation against the captured King of Tone.
// Uses the exact plugin signal path (calibration + render-quality oversampling).
//
//   PedalRender in.wav out.wav <drive0..1> <tone0..1> <vol0..1> <pres0..1> <clip:0Boost|1OD|2Dist>
//
// Knob "position/10" maps linearly to the 0..1 param (drive/tone are B-taper). Build target
// `PedalRender`. Mono in/out, 48 kHz expected.

#include <juce_audio_utils/juce_audio_utils.h>

#include "../src/PluginProcessor.h"

using namespace juce;

static void setParam (AudioProcessorValueTreeState& apvts, const char* id, float value)
{
    if (auto* p = apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

int main (int argc, char* argv[])
{
    if (argc < 8)
    {
        std::fprintf (stderr, "usage: PedalRender in.wav out.wav drive tone vol pres clip\n");
        return 2;
    }
    const ScopedJuceInitialiser_GUI guiInit;

    const File inFile (String (argv[1]).startsWith ("/") ? String (argv[1]) : File::getCurrentWorkingDirectory().getChildFile (argv[1]).getFullPathName());
    const File outFile (String (argv[2]).startsWith ("/") ? String (argv[2]) : File::getCurrentWorkingDirectory().getChildFile (argv[2]).getFullPathName());
    const float drive = (float) String (argv[3]).getDoubleValue();
    const float tone = (float) String (argv[4]).getDoubleValue();
    const float vol = (float) String (argv[5]).getDoubleValue();
    const float pres = (float) String (argv[6]).getDoubleValue();
    const int clip = String (argv[7]).getIntValue();

    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (inFile.createInputStream()));
    if (reader == nullptr)
    {
        std::fprintf (stderr, "cannot read %s\n", inFile.getFullPathName().toRawUTF8());
        return 1;
    }
    const int numSamples = (int) reader->lengthInSamples;
    const double fs = reader->sampleRate;
    AudioBuffer<float> buf (1, numSamples);
    reader->read (&buf, 0, numSamples, 0, true, false); // mono (left channel)

    MonarchAudioProcessor proc;
    auto& apvts = proc.apvts;
    setParam (apvts, "drive_yellow", drive);
    setParam (apvts, "tone_yellow", tone);
    setParam (apvts, "volume_yellow", vol);
    setParam (apvts, "presence_yellow", pres);
    setParam (apvts, "clipping_mode_yellow", (float) clip);
    setParam (apvts, "bypass_yellow", 0.0f);
    setParam (apvts, "bypass_red", 1.0f);   // Yellow only — Red passes dry
    setParam (apvts, "input_trim", 0.0f);
    setParam (apvts, "output_trim", 0.0f);

    constexpr int blockSize = 512;
    proc.setPlayConfigDetails (1, 1, fs, blockSize);
    proc.setNonRealtime (true); // render path → render-quality (8x FIR) oversampling
    proc.prepareToPlay (fs, blockSize);

    MidiBuffer midi;
    for (int start = 0; start < numSamples; start += blockSize)
    {
        const int n = jmin (blockSize, numSamples - start);
        AudioBuffer<float> sub (buf.getArrayOfWritePointers(), 1, start, n);
        proc.processBlock (sub, midi);
    }

    outFile.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<FileOutputStream> os (outFile.createOutputStream());
    std::unique_ptr<AudioFormatWriter> writer (wav.createWriterFor (os.get(), fs, 1, 24, {}, 0));
    if (writer == nullptr)
    {
        std::fprintf (stderr, "cannot write %s\n", outFile.getFullPathName().toRawUTF8());
        return 1;
    }
    os.release(); // writer owns it now
    writer->writeFromAudioSampleBuffer (buf, 0, numSamples);
    writer.reset();

    std::printf ("rendered %s  (drive %.2f tone %.2f vol %.2f pres %.2f clip %d, %d smp @ %.0f Hz)\n",
                 outFile.getFullPathName().toRawUTF8(), drive, tone, vol, pres, clip, numSamples, fs);
    return 0;
}
