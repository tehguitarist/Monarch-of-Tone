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
    // ⚠ 0.01 is NOT small enough here and this table was wrong at 4–8 kHz for years (found
    // 2026-07-30, v1.5 step 3). Stage 1's gain peaks near 4 kHz, so at 0.01 FS pin7 already
    // reaches ~0.7 V — past `sw1CeilKneeV` (0.5 V) in the Overdrive mode this section runs in —
    // and the "small-signal FR" is then being read through the soft clipper, in exactly the
    // presence band the warp shelf is pivoted for. The tell was that a candidate shelf's measured
    // contribution matched its analytic response to 0.01 dB at 4x and 0.17 at 2x but was off by
    // 1.64 dB at 1x/8 kHz, NON-monotonically in frequency — a filter-model error cannot do that.
    // At 5e-4 the whole chain is linear and the analytic model reproduces every cell.
    // Third instance in this file of the same rule ((b)'s floor-limited metric, (c2)'s level):
    // check the instrument is valid across the axis you are sweeping before reading it.
    const double vSmall = 5.0e-4;
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

    // ---------------------------------------------------------------------------------------
    // (c) Is ADAA still buying anything at 4x/8x? — the v1.5 CPU question, asked on fidelity.
    //
    //     ADAA and oversampling are two ways to buy the SAME thing (suppressed aliasing from a
    //     hard-ish knee), and the plugin currently pays for both: 25 ns/sample in Boost, 37 in OD,
    //     25 in Dist — 18–22 % of the channel (analysis/perf_split_probe.cpp). If the decimation
    //     filter already removes what ADAA removes at 4x/8x, then ADAA belongs at 1x/2x only, and
    //     that is decided here rather than by a hunch.
    //
    //     TWO axes, because ADAA does two things and they must not be read as one (this is exactly
    //     the v1.5 finding — first-order ADAA of the IDENTITY map is (x+x₋₁)/2, so below the knee it
    //     is a half-sample delay and a |cos(πf/fs_os)| rolloff, NOT a no-op):
    //       (c1) aliasing — what ADAA is FOR. Boost, hot: the rails are the only nonlinearity there.
    //       (c2) small-signal top octave — what ADAA COSTS, i.e. the identity-region droop the
    //            `warp*` shelf has been unknowingly absorbing. Small signal = rails never engage, so
    //            any difference here is purely the midpoint filter.
    // ---------------------------------------------------------------------------------------
    // (b)'s broadband residual is NOT a usable alias metric for this question — it moves only
    // −47.6 → −48.8 dB from 1x to 8x, i.e. it is floor-limited by things that are not aliasing
    // (the DC block's settling, the even-harmonic injection's envelopes, Goertzel leakage). A
    // difference of a few tenths measured on that floor would prove nothing either way.
    //
    // So aliasing is measured at NAMED fold bins instead: 9 kHz into 48 kHz puts harmonics 3,4,5,6,7
    // at 27/36/45/54/63 kHz, which fold to 21/12/3/6/15 kHz — none of which coincides with 9 or
    // 18 kHz, so every one of those bins is aliasing and nothing else. The metric validates itself:
    // it must fall steeply with the OS factor, and it does (~34 dB from 1x to 8x).
    const double fAl = 9000.0;
    const double aliasBins[] = { 3000.0, 6000.0, 12000.0, 15000.0, 21000.0 };
    const int nAl = (int) (sizeof (aliasBins) / sizeof (aliasBins[0]));

    std::printf ("\n== OSFidelity (c1): ADAA on vs off — ALIASING at named fold bins ==\n");
    std::printf ("   Boost, %.0f Hz at %.2f V pk (hot), drive 0.85; bins 3/6/12/15/21 kHz.\n", fAl, vClip);
    std::printf ("   %-6s | %10s %10s | %9s\n", "OS", "alias on", "alias off", "d alias");
    std::printf ("   -------+-----------------------+----------\n");
    setP (apvts, "clipping_mode_yellow", 0.0f); setP (apvts, "clipping_mode_red", 0.0f); // Boost

    auto aliasAt = [&]
    {
        auto y = renderSine (proc, fAl, vClip, Nal);
        checkFinite (y);
        const double fund = mag (y, fAl, settle);
        double ms = 0.0;
        for (int i = 0; i < nAl; ++i) { const double a = mag (y, aliasBins[i], settle); ms += 0.5 * a * a; }
        return (fund > 1e-15) ? 20.0 * std::log10 (std::sqrt (ms) / fund) : -200.0;
    };

    for (int o = 0; o < 4; ++o)
    {
        prep (proc, osFactors[o]); proc.setAdaaEnabled (true);
        const double aOn = aliasAt();
        prep (proc, osFactors[o]); proc.setAdaaEnabled (false);
        const double aOff = aliasAt();
        prep (proc, osFactors[o]); proc.setAdaaEnabled (true); // leave production state restored
        std::printf ("   %-6s | %+9.1f  %+9.1f  | %+8.2f\n", osName[o], aOn, aOff, aOff - aOn);
    }
    std::printf ("   (d alias > 0 = ADAA is still suppressing aliasing at that rate. ~0 = the\n"
                 "    decimation filter already covers it and ADAA is being paid for nothing.\n"
                 "    Sanity check on the metric itself: 'alias on' must fall steeply with OS.)\n");

    const double hfFreqs[] = { 4000, 8000, 12000, 16000 };
    // ⚠ Level matters here, and `vSmall` (0.01) is NOT small enough at the drive 0.85 (c1) leaves
    // set: Stage 1's gain peaks in the low kHz, so at 4 and 8 kHz pin7 still reaches the 2.4/3.6 V
    // rail knee and the cell stops being an identity-region measurement at all. That is why the
    // pre-early-out table read +0.09 / +2.14 at 4 / 8 kHz against an arithmetic 1.21 / 5.00 while
    // matching 12.04 / 24.08 exactly at 12 / 16 kHz, where Stage 1's response has fallen away —
    // two invalid cells that looked like data. At 5e-4 every stage is inside the linear region at
    // every frequency, and the whole table then agrees with the arithmetic to 0.01 dB.
    // Same rule as (b)'s floor-limited alias metric one section up: check the instrument is valid
    // across the axis you are sweeping before reading a number off it.
    const double vTiny = 5.0e-4;
    const int nHf = (int) (sizeof (hfFreqs) / sizeof (hfFreqs[0]));
    std::printf ("\n== OSFidelity (c2): ADAA on vs off — small-signal FR, off MINUS on (dB) ==\n");
    std::printf ("   %-6s", "Hz");
    for (int f = 0; f < nHf; ++f) std::printf (" %8.0f", hfFreqs[f]);
    std::printf ("\n");
    for (int o = 0; o < 4; ++o)
    {
        std::printf ("   %-6s", osName[o]);
        for (int f = 0; f < nHf; ++f)
        {
            prep (proc, osFactors[o]); proc.setAdaaEnabled (true);
            auto yOn = renderSine (proc, hfFreqs[f], vTiny, Nfr);
            prep (proc, osFactors[o]); proc.setAdaaEnabled (false);
            auto yOff = renderSine (proc, hfFreqs[f], vTiny, Nfr);
            checkFinite (yOn); checkFinite (yOff);
            const double mOn = mag (yOn, hfFreqs[f], settle), mOff = mag (yOff, hfFreqs[f], settle);
            const double dB = (mOn > 1e-15 && mOff > 1e-15) ? 20.0 * std::log10 (mOff / mOn) : 0.0;
            std::printf (" %+8.2f", dB);
        }
        std::printf ("\n");
    }
    proc.setAdaaEnabled (true);
    std::printf ("   (positive = ADAA-on is DARKER there, i.e. the identity-region midpoint droop.\n"
                 "    Predicted per stage at 16 kHz: 6.02 dB at 1x, 1.25 at 2x, 0.30 at 4x, 0.07 at 8x,\n"
                 "    and it CASCADES over the stages in the path — so 2x is ~3x that per-stage figure.\n"
                 "    Whatever appears here is what the `warp*` shelf has been absorbing: refit it and\n"
                 "    `hfTrim` in ONE pass if ADAA moves, or the same HF excess gets corrected twice.)\n");

    std::printf ("\n%s\n", anyNaN ? "FAIL" : "PASS");
    return anyNaN ? 1 : 0;
}
