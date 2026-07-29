// Where does the per-sample CPU actually go? — v1.5 scoping probe.
//
// PerfBenchmark reports the whole plugin; FeatureProfile reports the whole channel. Neither can say
// which SPAN the time is in, and that is the only thing that decides whether an optimisation exists:
//
//   processPre  — Stage 1 (two one-port WDF solves) + IC_A rail-sat ADAA + driveMakeup + driveShelf
//                 (4 biquad/one-pole shelves). ALL LINEAR except the rail ADAA.
//   processClip — Stage 2 or the SW-1 diode root, sw1Ceil ADAA, IC_B rail-sat ADAA, DC block,
//                 SW-2 diode root, injectEvenHarmonic, odLowShelf. The genuinely nonlinear span.
//   processPost — Tone (3-port R-type adaptor) + Volume. ALL LINEAR.
//
// The whole channel currently runs oversampled (dsp.md "Linear stages run oversampled"), so the two
// linear spans are paid ×OS for a reason that is NOT antialiasing — they cannot alias. They are
// oversampled only to shrink the bilinear frequency warp. If they dominate, then the OS factor is
// buying warp accuracy at a price that a per-rate prewarp/correction might buy far cheaper, and that
// is a real optimisation. If processClip dominates, there is nothing to win without losing fidelity.
//
// Also times the v1.5 ADAA identity-region early-out, because that one is free: below the knee the
// map is the identity and first-order ADAA of the identity is (x + x_prev)/2, not x — a half-sample
// delay and a cos() rolloff nobody asked for. (Measured as a SEPARATE binary via -DEARLY_OUT, since
// the real change is inside the header; here it is approximated by timing the map alone.)
//
// Header-only: DSP headers, no JUCE. ~1 s compile.
//   clang++ -std=c++17 -O2 -I. -isystem libs/chowdsp_wdf/include analysis/perf_split_probe.cpp \
//       -o analysis/.cache/perf_split_probe && analysis/.cache/perf_split_probe
#include "src/dsp/MonarchChannel.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double baseRate = 48000.0;
constexpr int osFactor = 8;                          // the expensive end, where the split matters most
constexpr double clipRate = baseRate * osFactor;     // the channel is prepped at the OS rate
constexpr int nSamples = 1 << 21;                    // ~2 M samples per span, per mode

// A signal that actually exercises the nonlinearities: hot enough to clip, swept so no branch is
// predicted away, and never so hot that everything pins on a rail for the whole run.
std::vector<double> makeInput()
{
    std::vector<double> v ((size_t) nSamples);
    double ph = 0.0;
    for (int i = 0; i < nSamples; ++i)
    {
        const double t = (double) i / clipRate;
        const double f = 220.0 * std::pow (2.0, 1.5 * std::sin (2.0 * M_PI * 0.7 * t)); // 78 Hz-622 Hz
        ph += 2.0 * M_PI * f / clipRate;
        v[(size_t) i] = 0.45 * std::sin (ph) * (0.6 + 0.4 * std::sin (2.0 * M_PI * 1.3 * t));
    }
    return v;
}

double nsPerSample (double seconds) { return seconds * 1.0e9 / (double) nSamples; }

// Keep the compiler from deleting a span whose result is unused.
volatile double sink = 0.0;

struct Split { double pre = 0.0, clip = 0.0, post = 0.0; };

Split timeSpans (int mode, const std::vector<double>& in)
{
    monarch::MonarchChannel ch { false };
    ch.prepareLinear (clipRate);
    ch.prepareClip (clipRate);
    ch.setDrive (0.7);
    ch.setTone (0.5);
    ch.setVolume (0.5);
    ch.setPresence (0.0);
    ch.setClippingMode (mode);

    using clk = std::chrono::steady_clock;
    Split s;

    // processPre alone. Its output is fed forward so the later spans see realistic levels.
    std::vector<double> nodeG ((size_t) nSamples);
    auto t0 = clk::now();
    for (int i = 0; i < nSamples; ++i)
        nodeG[(size_t) i] = ch.processPre (in[(size_t) i]);
    s.pre = std::chrono::duration<double> (clk::now() - t0).count();
    sink += nodeG.back();

    std::vector<double> hc ((size_t) nSamples);
    t0 = clk::now();
    for (int i = 0; i < nSamples; ++i)
        hc[(size_t) i] = ch.processClip (nodeG[(size_t) i]);
    s.clip = std::chrono::duration<double> (clk::now() - t0).count();
    sink += hc.back();

    t0 = clk::now();
    double acc = 0.0;
    for (int i = 0; i < nSamples; ++i)
        acc += ch.processPost (hc[(size_t) i]);
    s.post = std::chrono::duration<double> (clk::now() - t0).count();
    sink += acc;

    return s;
}
} // namespace

int main()
{
    const auto in = makeInput();
    const char* names[3] = { "Boost", "Overdrive", "Distortion" };

    std::printf ("== perf_split_probe: per-sample cost by SPAN, channel prepped at %.0f kHz (8x) ==\n",
                 clipRate / 1000.0);
    std::printf ("   drive 0.70, swept 78-622 Hz at 0.45 V pk, %d samples per span per mode.\n\n",
                 nSamples);
    std::printf ("   %-11s %9s %9s %9s %9s   %s\n", "mode", "pre ns", "clip ns", "post ns", "total",
                 "LINEAR share (pre+post)");
    std::printf ("   %-11s %9s %9s %9s %9s   %s\n", "-----------", "--------", "--------", "--------",
                 "--------", "-----------------------");

    for (int mode = 0; mode < 3; ++mode)
    {
        timeSpans (mode, in); // warm caches / branch predictors, discard
        const auto s = timeSpans (mode, in);
        const double pre = nsPerSample (s.pre), clip = nsPerSample (s.clip), post = nsPerSample (s.post);
        const double tot = pre + clip + post;
        std::printf ("   %-11s %9.2f %9.2f %9.2f %9.2f   %5.1f %%\n", names[mode], pre, clip, post, tot,
                     100.0 * (pre + post) / tot);
    }

    std::printf ("\n   The linear share is what the OS factor multiplies for WARP accuracy only —\n"
                 "   those spans cannot alias. Everything in `clip` is oversampled for a real reason.\n");
    return sink == 12345.6789 ? 1 : 0;
}
