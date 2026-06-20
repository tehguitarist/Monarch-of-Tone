// Volume pot audio taper — validation gate (Step 6/back-end).
//
// Verifies the VOL audio (A) taper wiper fraction pow(10, 1.8·(x−1)): full ≈ 0 dB, noon ≈ −18 dB,
// min ≈ −36 dB; passband flat (C11/R14 HPF corner 0.16 Hz is transparent in-band); no NaN.
// (Exponent 1.8 fitted to the real-pedal captures — see VolumePot.h; ideal-log would be 2.0/−20 dB.)

#include "../src/dsp/VolumePot.h"

#include <cmath>
#include <cstdio>

namespace
{
constexpr double fs = 96000.0;

double measureGainDB (monarch::VolumePot& v, double freq, double amp)
{
    v.reset();
    const int numSamples = (int) fs;
    const int settle = (int) (fs * 0.2);
    double peak = 0.0;
    for (int n = 0; n < numSamples; ++n)
    {
        const double x = amp * std::sin (2.0 * M_PI * freq * (double) n / fs);
        const double y = v.processSample (x);
        if (n > settle)
            peak = std::max (peak, std::abs (y));
    }
    return 20.0 * std::log10 (peak / amp);
}
} // namespace

int main()
{
    monarch::VolumePot vol;
    vol.prepare (fs);

    const double amp = 0.1;

    std::printf ("Volume pot audio taper (gain @ 1 kHz)\n");
    struct
    {
        double v, expectDB;
    } pts[] = { { 1.0, 0.0 }, { 0.75, -9.0 }, { 0.5, -18.0 }, { 0.25, -27.0 }, { 0.0, -36.0 } };

    bool taperOk = true, nanSeen = false;
    for (auto& p : pts)
    {
        vol.setVolume (p.v);
        const double g = measureGainDB (vol, 1000.0, amp);
        if (std::isnan (g) || std::isinf (g))
            nanSeen = true;
        const double err = g - p.expectDB;
        if (std::abs (err) > 1.0)
            taperOk = false;
        std::printf ("  vol=%.2f : %7.2f dB (expect %+.0f, err %+.2f)\n", p.v, g, p.expectDB, err);
    }

    // Passband flatness: the C11/R14 HPF (0.16 Hz) must be transparent across the audio band.
    vol.setVolume (1.0);
    const double gLow = measureGainDB (vol, 100.0, amp);
    const double gHigh = measureGainDB (vol, 10000.0, amp);
    const bool flatOk = std::abs (gLow - gHigh) < 0.1 && std::abs (gHigh) < 0.1;
    std::printf ("\n  passband @100Hz: %.3f dB, @10kHz: %.3f dB (expect ~0, flat)\n", gLow, gHigh);

    const bool pass = taperOk && flatOk && ! nanSeen;
    std::printf ("\n  taper ±1dB: %s | passband flat: %s | no NaN: %s\n",
                 taperOk ? "ok" : "FAIL", flatOk ? "ok" : "FAIL", nanSeen ? "FAIL" : "ok");
    std::printf ("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
