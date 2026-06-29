// OSFidelity — how close the low oversampling factors (1x/2x/4x), the common DAW live case, sit
// to the 8x reference, on two axes: (a) small-signal frequency response (the linear-stage
// bilinear top-octave warp that shrinks with OS), and (b) harmonic-vs-aliasing under clipping
// (intended distortion is preserved at low OS; the unwanted near-Nyquist aliasing is what OS
// removes). (build.md "Performance & fidelity probes".)
//
// ctest gate is FINITE-ONLY (no NaN/Inf at any factor); the dB deltas are reported, not asserted.
//
//   OSFidelity               (build target: OSFidelity)

#include <juce_audio_utils/juce_audio_utils.h>

#include "../src/PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace juce;

namespace
{
constexpr double fs = 48000.0;
constexpr int block = 256;
constexpr double kPi = 3.14159265358979323846;

void setP (AudioProcessorValueTreeState& a, const char* id, float v)
{
    if (auto* p = a.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// Render `numSamples` of a mono sine at `freq`/`vpk` through the processor (ch0), discarding the
// first `settle` samples of the returned buffer is the caller's job.
std::vector<double> renderSine (MonarchAudioProcessor& proc, double freq, double vpk, int numSamples)
{
    MidiBuffer midi;
    std::vector<double> out;
    out.reserve ((size_t) numSamples);
    double phase = 0.0;
    const double dphi = 2.0 * kPi * freq / fs;
    int produced = 0;
    while (produced < numSamples)
    {
        AudioBuffer<float> buf (2, block);
        for (int n = 0; n < block; ++n)
        {
            const float x = (float) (vpk * std::sin (phase));
            phase += dphi;
            buf.setSample (0, n, x);
            buf.setSample (1, n, x);
        }
        proc.processBlock (buf, midi);
        const float* d = buf.getReadPointer (0);
        for (int n = 0; n < block && produced < numSamples; ++n, ++produced)
            out.push_back ((double) d[n]);
    }
    return out;
}

// Goertzel magnitude at `freq` over the settled region (assumes ~integer cycles in [from,end)).
double mag (const std::vector<double>& x, double freq, int from)
{
    double re = 0.0, im = 0.0;
    const double w = 2.0 * kPi * freq / fs;
    for (int n = from; n < (int) x.size(); ++n) { re += x[n] * std::cos (w * n); im -= x[n] * std::sin (w * n); }
    const double n = (double) (x.size() - from);
    return 2.0 * std::sqrt (re * re + im * im) / n;
}
double rmsFrom (const std::vector<double>& x, int from)
{
    double acc = 0.0; int n = 0;
    for (int i = from; i < (int) x.size(); ++i) { acc += x[i] * x[i]; ++n; }
    return n ? std::sqrt (acc / n) : 0.0;
}

void prep (MonarchAudioProcessor& proc, int osLog2)
{
    setP (proc.apvts, "oversampling_realtime", (float) osLog2);
    proc.prepareToPlay (fs, block);
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

    setP (apvts, "bypass_yellow", 0.0f); setP (apvts, "bypass_red", 0.0f);
    setP (apvts, "tone_yellow", 0.5f);   setP (apvts, "tone_red", 0.5f);
    setP (apvts, "presence_yellow", 0.0f); setP (apvts, "presence_red", 0.0f);
    setP (apvts, "volume_yellow", 0.5f); setP (apvts, "volume_red", 0.5f);

    const int osFactors[] = { 0, 1, 2, 3 }; // 1x,2x,4x,8x
    const char* osName[] = { "1x", "2x", "4x", "8x" };

    bool anyNaN = false;
    auto checkFinite = [&] (const std::vector<double>& v) { for (double x : v) if (! std::isfinite (x)) anyNaN = true; };

    // ---------------------------------------------------------------------------------------
    // (a) Small-signal frequency response vs the 8x reference. Drive low so the chain is linear;
    //     this isolates the linear-stage bilinear top-octave warp (largest at 1x, ~0 by 8x).
    // ---------------------------------------------------------------------------------------
    setP (apvts, "clipping_mode_yellow", 1.0f); setP (apvts, "clipping_mode_red", 1.0f);
    setP (apvts, "drive_yellow", 0.5f); setP (apvts, "drive_red", 0.5f);
    const double vSmall = 0.01; // tiny → linear (no clipping, no rail-sat)
    const double freqs[] = { 100, 250, 500, 1000, 2000, 4000, 8000, 12000, 16000 };
    const int nF = (int) (sizeof (freqs) / sizeof (freqs[0]));
    const int Nfr = (int) fs; // 1 s → 1 Hz bins, integer cycles for integer freqs
    const int settle = (int) (fs * 0.2);

    // Reference: 8x magnitudes.
    prep (proc, 3);
    std::vector<double> ref8 ((size_t) nF, 0.0);
    for (int f = 0; f < nF; ++f) { auto y = renderSine (proc, freqs[f], vSmall, Nfr); checkFinite (y); ref8[(size_t) f] = mag (y, freqs[f], settle); }

    std::printf ("== OSFidelity (a): small-signal FR deviation from 8x (dB), drive 0.5 ==\n");
    std::printf ("   %-6s", "Hz");
    for (int f = 0; f < nF; ++f) std::printf (" %7.0f", freqs[f]);
    std::printf ("\n");
    for (int o = 0; o < 3; ++o) // 1x,2x,4x
    {
        prep (proc, osFactors[o]);
        std::printf ("   %-6s", osName[o]);
        for (int f = 0; f < nF; ++f)
        {
            auto y = renderSine (proc, freqs[f], vSmall, Nfr);
            checkFinite (y);
            const double m = mag (y, freqs[f], settle);
            const double dB = (m > 1e-15 && ref8[(size_t) f] > 1e-15) ? 20.0 * std::log10 (m / ref8[(size_t) f]) : 0.0;
            std::printf (" %+7.2f", dB);
        }
        std::printf ("\n");
    }

    // ---------------------------------------------------------------------------------------
    // (b) Harmonic vs aliasing under clipping. Drive a clipping sine whose high harmonics fold;
    //     report the intended harmonic distortion (should be ~constant across OS = preserved
    //     character) and the in-band aliasing floor (should DROP as OS rises).
    // ---------------------------------------------------------------------------------------
    setP (apvts, "clipping_mode_yellow", 1.0f); setP (apvts, "clipping_mode_red", 1.0f); // Overdrive
    setP (apvts, "drive_yellow", 0.85f); setP (apvts, "drive_red", 0.85f);
    const double f0 = 2400.0;  // harmonics at 4.8/7.2/9.6/12/14.4/16.8/19.2/21.6 kHz; above fold at low OS
    const double vClip = 0.5;  // hot → real clipping
    const int Nal = (int) fs;  // integer cycles for 2400 Hz

    std::printf ("\n== OSFidelity (b): clipping %.0f Hz, drive 0.85 — harmonic vs aliasing ==\n", f0);
    std::printf ("   %-6s | %12s | %14s\n", "OS", "harmonic dB", "aliasing dB");
    std::printf ("   -------+--------------+----------------\n");
    for (int o = 0; o < 4; ++o)
    {
        prep (proc, osFactors[o]);
        auto y = renderSine (proc, f0, vClip, Nal);
        checkFinite (y);
        const double total = rmsFrom (y, settle);
        // Sum harmonic energy at k*f0 below Nyquist (the intended distortion).
        double harmMS = 0.0;
        double fund = 0.0;
        for (int k = 1; k * f0 < fs * 0.5 - 1.0; ++k)
        {
            const double h = mag (y, k * f0, settle);
            if (k == 1) fund = h;
            harmMS += 0.5 * h * h; // sine power
        }
        const double harmRms = std::sqrt (harmMS);
        const double aliasRms = std::sqrt (std::max (0.0, total * total - harmMS));
        const double harmDb = (fund > 1e-15) ? 20.0 * std::log10 (harmRms / fund) : -200.0;
        const double aliasDb = (fund > 1e-15) ? 20.0 * std::log10 (aliasRms / fund) : -200.0;
        std::printf ("   %-6s | %+11.1f  | %+13.1f\n", osName[o], harmDb, aliasDb);
    }

    std::printf ("\n   (harmonic dB ~constant = distortion character preserved; aliasing dB should fall with OS.)\n");
    std::printf ("\n%s\n", anyNaN ? "FAIL" : "PASS");
    return anyNaN ? 1 : 0;
}
