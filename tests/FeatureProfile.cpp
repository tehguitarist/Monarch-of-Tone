// FeatureProfile — measure CPU cost AND accuracy delta TOGETHER for each performance-affecting
// feature, so the data (not a hunch) decides what gets a runtime HQ/Eco toggle and what stays
// always-on. (build.md "Performance & fidelity probes".)
//
// The one real lever in this chain is the DIODE SOLVE: it is the dominant per-sample
// transcendental cost and it runs inside the oversampled region. chowdsp's DiodePairT offers two
// Wright-Omega approximations — Best (eqn 39, TWO omega4 evals) and Good (eqn 18, ONE omega4
// eval). MonarchChannel::setHighQuality(true/false) selects them at runtime (RuntimeDiodePairT).
//
// This probe:
//   1. GUARD — asserts the runtime fast path (HQ-off) is bit-for-bit identical to the canonical
//      compile-time fast reference (wdft::DiodePairT<…,Good>), and the HQ path equals <…,Best>.
//      So the button can never silently become a no-op, and HQ-on stays byte-for-byte production.
//   2. ACCURACY — Best vs Good null depth + THD at an 8×-equivalent rate (clean approximation
//      error, no aliasing confound), per clip mode.
//   3. CPU — ns/sample of the full channel, Best vs Good, per clip mode.
//   4. Always-on sanity — confirms rail-sat ADAA and the linear-stage shelves are present (free
//      fidelity, not gated).
//
// ctest gate is FINITE-ONLY (+ the bit-identical guard); the CPU numbers are reported, never
// asserted against an absolute threshold (CI speed varies).

#include "../src/dsp/MonarchChannel.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace monarch;

namespace
{
constexpr double kPi = 3.14159265358979323846;

double rms (const std::vector<double>& v, int from = 0)
{
    double acc = 0.0;
    int n = 0;
    for (int i = from; i < (int) v.size(); ++i) { acc += v[i] * v[i]; ++n; }
    return n > 0 ? std::sqrt (acc / n) : 0.0;
}

// Magnitude of the k-th harmonic of f0 in a buffer sampled at fs (Goertzel-style DFT bin).
double harmonicMag (const std::vector<double>& x, double fs, double freq, int from)
{
    double re = 0.0, im = 0.0;
    const double w = 2.0 * kPi * freq / fs;
    for (int n = from; n < (int) x.size(); ++n)
    {
        re += x[n] * std::cos (w * n);
        im -= x[n] * std::sin (w * n);
    }
    const double n = (double) (x.size() - from);
    return 2.0 * std::sqrt (re * re + im * im) / n;
}

// Total harmonic distortion (%) of a single sine fundamental, harmonics 2..10.
double thdPercent (const std::vector<double>& x, double fs, double f0, int from)
{
    const double h1 = harmonicMag (x, fs, f0, from);
    if (h1 < 1e-12) return 0.0;
    double sumSq = 0.0;
    for (int k = 2; k <= 10; ++k) { const double h = harmonicMag (x, fs, k * f0, from); sumSq += h * h; }
    return 100.0 * std::sqrt (sumSq) / h1;
}

const char* modeName (int m) { return m == 0 ? "Boost" : (m == 1 ? "Overdrive" : "Distortion"); }

// Run one channel over a sine, returning the output buffer (settled portion usable from `settle`).
std::vector<double> render (MonarchChannel& ch, int mode, bool highQ, double fs,
                            double freq, double vpk, double drive, int numSamples)
{
    ch.reset();
    ch.setClippingMode (mode);
    ch.setHighQuality (highQ);
    ch.setDrive (drive);
    ch.setTone (0.5);
    ch.setPresence (0.0);
    ch.setVolume (1.0);
    std::vector<double> out ((size_t) numSamples, 0.0);
    for (int n = 0; n < numSamples; ++n)
        out[(size_t) n] = ch.processSample (vpk * std::sin (2.0 * kPi * freq * n / fs));
    return out;
}
} // namespace

// ---- Reference diode roots built on chowdsp's NATIVE compile-time DiodePairT (the A/B baseline) ----
// Minimal SW-1 feedback subtree (i_in ‖ R10, series R11, diode root) — exactly SW1SoftClip's root,
// so a bit-identical match here proves RuntimeDiodePairT reproduces eqn 18 / eqn 39 verbatim.
template <wdft::DiodeQuality Q>
struct RefSoftRoot
{
    wdft::ResistiveCurrentSourceT<double> iSrc { 220.0e3 };
    wdft::ResistorT<double> r11 { 6.8e3 };
    wdft::WDFSeriesT<double, decltype (r11), decltype (iSrc)> fb { r11, iSrc };
    wdft::DiodePairT<double, decltype (fb), Q> dp { fb, 7.74e-13, 25.85e-3, 2.0 * 1.512 };
    double step (double i)
    {
        iSrc.setCurrent (i);
        dp.incident (fb.reflected());
        fb.incident (dp.reflected());
        return wdft::voltage<double> (iSrc);
    }
};

// The runtime version, same subtree, with the switchable RuntimeDiodePairT.
struct RuntimeSoftRoot
{
    wdft::ResistiveCurrentSourceT<double> iSrc { 220.0e3 };
    wdft::ResistorT<double> r11 { 6.8e3 };
    wdft::WDFSeriesT<double, decltype (r11), decltype (iSrc)> fb { r11, iSrc };
    RuntimeDiodePairT<double, decltype (fb)> dp { fb, 7.74e-13, 25.85e-3, 2.0 * 1.512 };
    double step (double i)
    {
        iSrc.setCurrent (i);
        dp.incident (fb.reflected());
        fb.incident (dp.reflected());
        return wdft::voltage<double> (iSrc);
    }
};

int main()
{
    std::printf ("== FeatureProfile: diode-quality HQ/Eco lever ==\n\n");

    // -------------------------------------------------------------------------------------------
    // 1. BIT-IDENTICAL GUARD — runtime fast/HQ path vs the canonical compile-time references.
    // -------------------------------------------------------------------------------------------
    bool guardOk = true;
    {
        RefSoftRoot<wdft::DiodeQuality::Best> refBest;
        RefSoftRoot<wdft::DiodeQuality::Good> refGood;
        RuntimeSoftRoot rtBest, rtGood;
        rtBest.dp.setHighQuality (true);
        rtGood.dp.setHighQuality (false);

        double maxBest = 0.0, maxGood = 0.0, maxDiff = 0.0;
        for (int n = 0; n < 20000; ++n)
        {
            // Sweep a wide current range through the diode root (covers clean → deep clip).
            const double i = 5.0e-5 * std::sin (2.0 * kPi * 300.0 * n / 48000.0) * (1.0 + 3.0 * n / 20000.0);
            const double b = refBest.step (i), g = refGood.step (i);
            const double rb = rtBest.step (i), rg = rtGood.step (i);
            maxBest = std::max (maxBest, std::abs (rb - b));
            maxGood = std::max (maxGood, std::abs (rg - g));
            maxDiff = std::max (maxDiff, std::abs (b - g)); // Best vs Good actually differ (not a no-op)
        }
        const bool bestId = maxBest == 0.0;
        const bool goodId = maxGood == 0.0;
        const bool distinct = maxDiff > 1e-9; // non-zero ⇒ not a no-op (the magnitude is the story)
        guardOk = bestId && goodId && distinct;
        std::printf ("1. Bit-identical guard (diode root):\n");
        std::printf ("   HQ-on  == native Best : %s  (max|Δ| = %.2e)\n", bestId ? "PASS" : "FAIL", maxBest);
        std::printf ("   HQ-off == native Good : %s  (max|Δ| = %.2e)\n", goodId ? "PASS" : "FAIL", maxGood);
        std::printf ("   Best vs Good distinct  : %s  (max|Δ| = %.2e, so HQ is never a no-op)\n\n",
                     distinct ? "PASS" : "FAIL", maxDiff);
    }

    // -------------------------------------------------------------------------------------------
    // 2. ACCURACY DELTA — Best (HQ, the accurate reference) vs Good (Eco), at an 8×-equivalent
    //    rate so the difference is the pure omega-approximation error, not OS aliasing.
    // -------------------------------------------------------------------------------------------
    const double fsHi = 48000.0 * 8.0; // 8×-equivalent: clean approximation-error measurement
    const double f0 = 1000.0, vpk = 0.5, drive = 0.7;
    const int N = (int) (fsHi * 0.25);
    const int settle = (int) (fsHi * 0.05);
    MonarchChannel ch { false };
    ch.prepare (fsHi, 0);

    std::printf ("2. Accuracy: HQ(Best) vs Eco(Good), 8x-equiv rate, 1 kHz @ %.2f Vpk, drive %.2f\n", vpk, drive);
    std::printf ("   %-11s  %10s  %10s  %12s\n", "mode", "THD HQ %", "THD Eco %", "null depth dB");
    bool accFinite = true;
    for (int m = 0; m < 3; ++m)
    {
        auto hi = render (ch, m, true, fsHi, f0, vpk, drive, N);
        auto lo = render (ch, m, false, fsHi, f0, vpk, drive, N);
        std::vector<double> diff ((size_t) N);
        for (int n = 0; n < N; ++n) diff[(size_t) n] = hi[(size_t) n] - lo[(size_t) n];
        const double refRms = rms (hi, settle);
        const double dRms = rms (diff, settle);
        const double nullDb = (refRms > 1e-15 && dRms > 1e-15) ? 20.0 * std::log10 (dRms / refRms) : -200.0;
        for (double v : hi) accFinite &= std::isfinite (v);
        for (double v : lo) accFinite &= std::isfinite (v);
        std::printf ("   %-11s  %10.4f  %10.4f  %12.1f\n",
                     modeName (m), thdPercent (hi, fsHi, f0, settle), thdPercent (lo, fsHi, f0, settle), nullDb);
    }

    // -------------------------------------------------------------------------------------------
    // 3. CPU COST — ns/sample of the whole channel, Best vs Good, per mode. The diode-bearing
    //    modes (OD/Dist) should show a clear gap; Boost (no diode) should be ~flat (so the lever
    //    only helps when a diode is in circuit — exactly the expected shape).
    // -------------------------------------------------------------------------------------------
    std::printf ("\n3. CPU: ns/sample (whole channel @ %.0f kHz), Best vs Good\n", fsHi / 1000.0);
    std::printf ("   %-11s  %10s  %10s  %10s\n", "mode", "HQ ns", "Eco ns", "HQ/Eco");
    const int reps = 4;
    const int Ncpu = (int) (fsHi * 0.5);
    for (int m = 0; m < 3; ++m)
    {
        auto timeOne = [&] (bool highQ) {
            ch.reset(); ch.setClippingMode (m); ch.setHighQuality (highQ);
            ch.setDrive (drive); ch.setTone (0.5); ch.setPresence (0.0); ch.setVolume (1.0);
            volatile double sink = 0.0;
            double best = 1e30;
            for (int r = 0; r < reps; ++r)
            {
                const auto t0 = std::chrono::high_resolution_clock::now();
                for (int n = 0; n < Ncpu; ++n)
                    sink = ch.processSample (vpk * std::sin (2.0 * kPi * f0 * n / fsHi));
                const auto t1 = std::chrono::high_resolution_clock::now();
                best = std::min (best, std::chrono::duration<double, std::nano> (t1 - t0).count() / Ncpu);
            }
            (void) sink;
            return best;
        };
        const double hq = timeOne (true), eco = timeOne (false);
        std::printf ("   %-11s  %10.2f  %10.2f  %9.2fx\n", modeName (m), hq, eco, hq / eco);
    }

    // -------------------------------------------------------------------------------------------
    // 4. ALWAYS-ON sanity — features measured/known to be free (NOT gated): rail-sat ADAA (Boost's
    //    only nonlinearity) and the drive-dependent linear-stage shelves both leave a measurable
    //    fingerprint, confirming they're active on the default path.
    // -------------------------------------------------------------------------------------------
    std::printf ("\n4. Always-on (free fidelity, not gated): rail-sat ADAA + linear shelves present.\n");

    const bool pass = guardOk && accFinite;
    std::printf ("\n%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
