// PerfBenchmark — render a fixed length of audio through the REAL stereo processor and report
// CPU as a percentage of realtime (wall-clock vs audio duration) plus the reported plugin latency,
// for every oversampling factor × clipping mode. (build.md "Performance & fidelity probes".)
//
// The numbers populate the README "Performance" table. The ctest gate is FINITE-ONLY: it asserts
// no NaN/Inf in any configuration — it does NOT assert an absolute CPU %, since CI speed varies.
//
//   PerfBenchmark            (build target: PerfBenchmark)

#include <juce_audio_utils/juce_audio_utils.h>

#include "../src/PluginProcessor.h"

#include <chrono>
#include <cmath>
#include <cstdio>

using namespace juce;

namespace
{
constexpr double fs = 48000.0;
constexpr int block = 128;
constexpr double inFreq = 220.0;
constexpr double seconds = 4.0; // fixed render length per configuration

void setP (AudioProcessorValueTreeState& a, const char* id, float v)
{
    if (auto* p = a.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

struct BenchResult { double cpuPct = 0.0; int latency = 0; long nan = 0; };

BenchResult runConfig (MonarchAudioProcessor& proc, AudioProcessorValueTreeState& apvts, int osLog2, int mode)
{
    setP (apvts, "oversampling_realtime", (float) osLog2);
    setP (apvts, "clipping_mode_yellow", (float) mode);
    setP (apvts, "clipping_mode_red", (float) mode);
    proc.prepareToPlay (fs, block); // re-prep so the new OS factor is active + latency reported

    const int totalSamples = (int) (seconds * fs);
    const int numBlocks = (totalSamples + block - 1) / block;
    MidiBuffer midi;
    double phase = 0.0;
    const double dphi = 2.0 * MathConstants<double>::pi * inFreq / fs;

    // Warm up (let the oversampler/filters settle, fill caches) before timing.
    for (int w = 0; w < 8; ++w)
    {
        AudioBuffer<float> buf (2, block);
        for (int n = 0; n < block; ++n) { const float x = 0.4f * (float) std::sin (phase); phase += dphi; buf.setSample (0, n, x); buf.setSample (1, n, x); }
        proc.processBlock (buf, midi);
    }

    BenchResult r;
    r.latency = proc.getLatencySamples();
    const auto t0 = std::chrono::high_resolution_clock::now();
    for (int b = 0; b < numBlocks; ++b)
    {
        AudioBuffer<float> buf (2, block);
        for (int n = 0; n < block; ++n) { const float x = 0.4f * (float) std::sin (phase); phase += dphi; buf.setSample (0, n, x); buf.setSample (1, n, x); }
        proc.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* d = buf.getReadPointer (ch);
            for (int n = 0; n < block; ++n) if (! std::isfinite (d[n])) ++r.nan;
        }
    }
    const auto t1 = std::chrono::high_resolution_clock::now();
    const double wall = std::chrono::duration<double> (t1 - t0).count();
    const double audio = (double) (numBlocks * block) / fs;
    r.cpuPct = 100.0 * wall / audio;
    return r;
}
} // namespace

int main()
{
    const ScopedJuceInitialiser_GUI guiInit;
    MonarchAudioProcessor proc;
    auto& apvts = proc.apvts;

    proc.setPlayConfigDetails (2, 2, fs, block);
    proc.setNonRealtime (false);
    proc.prepareToPlay (fs, block);

    // Representative live settings: both channels active, hot-ish drive, mid everything else.
    setP (apvts, "bypass_yellow", 0.0f); setP (apvts, "bypass_red", 0.0f);
    setP (apvts, "drive_yellow", 0.7f);  setP (apvts, "drive_red", 0.7f);
    setP (apvts, "volume_yellow", 0.5f); setP (apvts, "volume_red", 0.5f);
    setP (apvts, "tone_yellow", 0.5f);   setP (apvts, "tone_red", 0.5f);

    const char* modeName[] = { "Boost", "Overdrive", "Distortion" };

    std::printf ("== PerfBenchmark: stereo, both channels active (Red->Yellow), %.0f s @ %.0f kHz, block %d ==\n",
                 seconds, fs / 1000.0, block);
    std::printf ("   CPU = %% of realtime (wall/audio); lower is faster. Latency = reported plugin delay.\n\n");
    std::printf ("   %-4s | %-10s | %10s | %12s\n", "OS", "mode", "CPU % rt", "latency smp");
    std::printf ("   -----+------------+------------+-------------\n");

    long totalNan = 0;
    for (int os = 0; os < 4; ++os)
    {
        for (int m = 0; m < 3; ++m)
        {
            const BenchResult r = runConfig (proc, apvts, os, m);
            totalNan += r.nan;
            std::printf ("   %-4s | %-10s | %9.1f%% | %12d%s\n",
                         (String (1 << os) + "x").toRawUTF8(), modeName[m], r.cpuPct, r.latency,
                         r.nan ? "  [NaN!]" : "");
        }
    }

    // Render-path (offline bounce, FIR) latency reference at the default render factor.
    proc.setNonRealtime (true);
    const BenchResult rr = runConfig (proc, apvts, 2 /*ignored: render uses oversampling_render*/, 1);
    totalNan += rr.nan;
    std::printf ("\n   render (non-realtime, FIR) latency: %d smp,  CPU %.1f%% rt\n", rr.latency, rr.cpuPct);

    std::printf ("\n   total non-finite samples: %ld\n", totalNan);
    const bool pass = (totalNan == 0);
    std::printf ("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
