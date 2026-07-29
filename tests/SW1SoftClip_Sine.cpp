// SW-1 soft clip — validation gate Step 5a.
//
// Stage 2 inverting amp with the MA856 soft-clip diodes (one DiodePairT, n_eff≈3.024) in the
// feedback (R10 ∥ [R11 + diodes]). Verifies:
//   - small-signal gain ≈ −22 (linear, diodes off),
//   - SYMMETRIC clipping (|out+| ≈ |out−|),
//   - soft compression with onset where the output ≈ 1.58 V (v1.4 P9 step 2, 2026-07-29: the
//     parallel-string Is doubling lowers the ≈1.64 V single-string clamp by n_eff·Vt·ln2 ≈ 54 mV),
//   - softer than a hard clamp (output keeps growing slowly above onset), no NaN.

#include "../src/dsp/SW1SoftClip.h"

#include <cmath>
#include <cstdio>

namespace
{
constexpr double fs = 96000.0;

// Drive a 1 kHz sine (≫ 159 Hz → full passband); return signed small-signal gain and the
// positive / negative output peaks.
struct Peaks
{
    double outPos, outNeg, signedGain;
};

Peaks measure (monarch::SW1SoftClip& s, double amp)
{
    s.reset();
    const double f = 1000.0;
    const int N = (int) fs, settle = (int) (fs * 0.2);
    double pos = 0.0, neg = 0.0, xy = 0.0, xx = 0.0;
    for (int n = 0; n < N; ++n)
    {
        const double x = amp * std::sin (2.0 * M_PI * f * (double) n / fs);
        const double y = s.processSample (x);
        if (n > settle)
        {
            pos = std::max (pos, y);
            neg = std::min (neg, y);
            xy += x * y;
            xx += x * x;
        }
    }
    return { pos, -neg, xy / xx };
}
} // namespace

int main()
{
    monarch::SW1SoftClip stage;
    stage.prepare (fs);

    // Small-signal gain (diodes off): output ≪ threshold.
    const auto small = measure (stage, 0.002);
    const double ssGain = small.signedGain;

    std::printf ("SW-1 soft clip (fs=%.0f)\n", fs);
    std::printf ("  small-signal gain: %.2f  (target −22, inverting)\n", ssGain);
    std::printf ("\n  transfer curve (1 kHz):\n");
    std::printf ("    %8s  %9s  %9s  %8s  %8s\n", "in_pk[V]", "out+[V]", "out-[V]", "gain", "sym%%");

    bool nanSeen = false, symOk = true;
    double kneeOut = 0.0, kneeFound = -1.0;
    const double absGain = std::abs (ssGain);
    for (double a : { 0.002, 0.02, 0.05, 0.075, 0.1, 0.2, 0.5, 1.0, 2.0 })
    {
        const auto p = measure (stage, a);
        const double outAvg = 0.5 * (p.outPos + p.outNeg);
        const double sym = 100.0 * std::abs (p.outPos - p.outNeg) / std::max (1e-9, outAvg);
        const double g = outAvg / a;
        if (std::isnan (outAvg))
            nanSeen = true;
        if (a >= 0.05 && sym > 5.0)
            symOk = false; // asymmetric clipping = FAIL
        // soft-knee onset: first level where incremental gain drops to ~70% of small-signal.
        if (kneeFound < 0.0 && g < 0.7 * absGain)
        {
            kneeFound = a;
            kneeOut = outAvg;
        }
        std::printf ("    %8.3f  %9.4f  %9.4f  %8.2f  %7.2f\n", a, p.outPos, p.outNeg, g, sym);
    }

    // Heavy-clip compression: gain at 1 V input must be far below small-signal (clipping).
    const auto heavy = measure (stage, 1.0);
    const double heavyGain = 0.5 * (heavy.outPos + heavy.outNeg) / 1.0;
    // Softness: output at 2 V input should still exceed output at 0.5 V (soft, not hard-clamped).
    const auto at2 = measure (stage, 2.0);
    const auto at05 = measure (stage, 0.5);
    const bool soft = 0.5 * (at2.outPos + at2.outNeg) > 0.5 * (at05.outPos + at05.outNeg) + 0.05;

    std::printf ("\n  soft-knee onset: output ≈ %.3f V at in=%.3f V (target ≈1.58 V)\n", kneeOut, kneeFound);
    std::printf ("  heavy-clip gain (1 V in): %.2f  (≪ small-signal ⇒ clipping)\n", heavyGain);

    const bool gainOk = std::abs (absGain - 22.0) < 1.5;          // small-signal ≈ 22×
    const bool invOk = ssGain < 0.0;                             // inverting
    const bool clipOk = std::abs (heavyGain) < 0.5 * absGain;     // strong compression
    const bool onsetOk = kneeFound > 0.0 && kneeOut > 1.05 && kneeOut < 2.1; // ≈1.58 V ballpark (P9 step 2)
    const bool pass = gainOk && invOk && symOk && clipOk && onsetOk && soft && ! nanSeen;

    std::printf ("\n  gain≈−22: %s | inverting: %s | symmetric: %s | clips: %s | onset≈1.58V: %s | soft: %s | no NaN: %s\n",
                 gainOk ? "ok" : "FAIL", invOk ? "ok" : "FAIL", symOk ? "ok" : "FAIL",
                 clipOk ? "ok" : "FAIL", onsetOk ? "ok" : "FAIL", soft ? "ok" : "FAIL",
                 nanSeen ? "FAIL" : "ok");
    std::printf ("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
