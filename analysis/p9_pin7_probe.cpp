// v1.4 P9 step 3 — pin7 level probe / SW-1-ceiling fit engine (analysis/p9_ceiling_fit.py).
//
// Two jobs. (1) Measure what voltage pin7 actually reaches in Overdrive across the `comp` level
// steps: the ceiling has to be chosen in VOLTS, and nothing had ever measured the node it acts on.
// (2) Be the fit loop's engine — it pulls in the DSP headers ONLY (no JUCE), so a candidate
// ceiling is a ~1 s recompile rather than a plugin rebuild plus a 44-capture render. Because it
// runs the real MonarchChannel, nothing about the nonlinearity is reimplemented for the fit.
//
// Reports NodeG (post-rail-sat/driveMakeup/shelf — what the clipper sees) and node_HC, which in OD
// IS pin7: SW-2 is off, so processClip returns pin7 after the ceiling, rail-sat, DC block and the
// even-harmonic injection. Its rise per drive is what p9_ceiling_fit.py scores against the pedal's;
// the `probe-render` column of `static` is the check that this stands in for a real render (OD's
// chain after pin7 is linear at 1 kHz, so the two rises must agree — measured within 0.33 dB).
#include "src/dsp/MonarchChannel.h"

#include <cmath>
#include <cstdio>

int main()
{
    constexpr double fs = 384000.0; // 8x of 48k — the render path's rate
    const double drives[] = { 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 1.0 };
    const double levelsDb[] = { -30.0, -27.0, -24.0, -21.0, -18.0, -15.0, -12.0, -9.0, -6.0, -3.0 };
    constexpr double circuitVoltsPerFS = 0.87;

    printf("%-6s %-7s %9s %9s %9s\n", "drive", "lvlFS", "nodeG_pk", "pin7_pk", "pin7_rms");
    for (double d : drives)
    {
        monarch::MonarchChannel ch { false };
        ch.prepareLinear (fs);
        ch.prepareClip (fs);
        ch.setClippingMode (1); // Overdrive
        ch.setDrive (d);
        ch.setTone (0.5);
        ch.setVolume (0.5);
        ch.setPresence (0.0);
        ch.setSupplyVoltage (9.0);

        for (double ldb : levelsDb)
        {
            ch.reset();
            const double amp = std::pow (10.0, ldb / 20.0) * circuitVoltsPerFS;
            const int N = (int) (fs * 0.30), settle = (int) (fs * 0.15);
            double gPk = 0.0, pPk = 0.0, pSq = 0.0;
            long n2 = 0;
            for (int n = 0; n < N; ++n)
            {
                const double x = amp * std::sin (2.0 * M_PI * 1000.0 * (double) n / fs);
                const double g = ch.processPre (x);
                const double hc = ch.processClip (g);
                if (n > settle)
                {
                    gPk = std::max (gPk, std::abs (g));
                    pPk = std::max (pPk, std::abs (hc));
                    pSq += hc * hc;
                    ++n2;
                }
            }
            printf("%-6.2f %-7.0f %9.3f %9.3f %9.3f\n", d, ldb, gPk, pPk, std::sqrt (pSq / (double) n2));
        }
        printf("\n");
    }
    return 0;
}
