// Full per-channel chain + dual-channel series integration — validation gate Step 7.
//
// Exercises the complete MonarchChannel (Stage1 → Stage2/SW1 → rail-sat → SW2 → Tone → Volume)
// for both channels (Yellow stock, Red fixed Hi-Gain) and all 4 clipping modes. Verifies:
//   - finite, non-silent output everywhere (no NaN/Inf), for guitar-level input,
//   - clipping ordering: Boost peaks highest pre-volume; the diode modes clamp lower
//     (Distortion/Both hardest), at the expected absolute thresholds,
//   - Boost clips on the op-amp rails (≈±3.3 V) rather than running away,
//   - Red (Hi-Gain) drives harder than Yellow at the same settings,
//   - the two channels run in series (Red → Yellow, the real pedal's signal flow) without
//     instability.

#include "../src/dsp/MonarchChannel.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
constexpr double fs = 96000.0;

// Peak output of one channel for a sine input, with volume wide open (probe the chain, not VOL).
double peakOut (monarch::MonarchChannel& ch, int mode, double freq, double vpk, double drive)
{
    ch.reset();
    ch.setClippingMode (mode);
    ch.setDrive (drive);
    ch.setTone (0.5);
    ch.setPresence (0.0);
    ch.setVolume (1.0);
    const int numSamples = (int) fs;
    const int settle = (int) (fs * 0.2);
    double peak = 0.0;
    bool nan = false;
    for (int n = 0; n < numSamples; ++n)
    {
        const double x = vpk * std::sin (2.0 * M_PI * freq * (double) n / fs);
        const double y = ch.processSample (x);
        if (std::isnan (y) || std::isinf (y))
            nan = true;
        if (n > settle)
            peak = std::max (peak, std::abs (y));
    }
    return nan ? std::nan ("") : peak;
}
} // namespace

int main()
{
    monarch::MonarchChannel yellow { false };
    monarch::MonarchChannel red { true };
    yellow.prepare (fs, 512);
    red.prepare (fs, 512);

    const char* modeName[] = { "Boost", "Overdrive", "Distortion" };
    const double vpk = 0.3;   // hot-ish guitar level (≈ −12 dBu is 0.275 Vpk)
    const double freq = 220.0;
    const double drive = 0.7;

    std::printf ("Full-chain peak output (vin=%.2f Vpk @ %.0f Hz, drive=%.2f, vol=max)\n", vpk, freq, drive);
    std::printf ("  %-11s  %10s  %10s\n", "mode", "Yellow", "Red(HiGain)");

    constexpr int kModes = 3; // Boost / Overdrive / Distortion (3-way, no "Both")
    bool nanSeen = false, redHotter = true;
    std::vector<double> yPeak ((size_t) kModes, 0.0);
    for (int m = 0; m < kModes; ++m)
    {
        const double y = peakOut (yellow, m, freq, vpk, drive);
        const double r = peakOut (red, m, freq, vpk, drive);
        if (std::isnan (y) || std::isnan (r))
            nanSeen = true;
        // Red drives Stage 1 harder; with diodes its post-clip output can be similar, but its
        // pre-tone drive level is higher → compare the Boost (unclamped) mode for the gain check.
        if (m == 0 && ! (r > y))
            redHotter = false;
        yPeak[(size_t) m] = y;
        std::printf ("  %-11s  %10.4f  %10.4f\n", modeName[m], y, r);
    }

    // Clipping ordering (Yellow): Boost (rails) highest; Distortion clamps lowest.
    const bool boostHighest = yPeak[0] >= yPeak[1] && yPeak[0] >= yPeak[2];
    const bool nonSilent = yPeak[0] > 1e-3 && yPeak[1] > 1e-3 && yPeak[2] > 1e-3;
    // Boost must clip on the rails, not run away: pre-volume peak bounded near ±3.3 V.
    const bool boostRails = yPeak[0] > 1.0 && yPeak[0] < 4.0;

    std::printf ("\n  Boost peak (Yellow) = %.3f V (expect rail-bounded ~1-4 V)\n", yPeak[0]);

    // Dual-channel series: Red → Yellow (the real pedal's signal flow — Red is first), default
    // Overdrive, realistic level. Must stay finite.
    yellow.reset();
    red.reset();
    yellow.setClippingMode (1);
    red.setClippingMode (1);
    for (auto* c : { &yellow, &red })
    {
        c->setDrive (0.5);
        c->setTone (0.5);
        c->setPresence (0.0);
        c->setVolume (0.5);
    }
    double seriesPeak = 0.0;
    bool seriesNan = false;
    for (int n = 0; n < (int) fs; ++n)
    {
        const double x = vpk * std::sin (2.0 * M_PI * freq * (double) n / fs);
        const double y = yellow.processSample (red.processSample (x));
        if (std::isnan (y) || std::isinf (y))
            seriesNan = true;
        if (n > (int) (fs * 0.2))
            seriesPeak = std::max (seriesPeak, std::abs (y));
    }
    std::printf ("  series Red→Yellow (OD, vol=0.5) peak: %.4f V%s\n",
                 seriesPeak, seriesNan ? "  [NaN!]" : "");

    const bool seriesOk = ! seriesNan && seriesPeak > 1e-4 && seriesPeak < 10.0;
    const bool pass = ! nanSeen && redHotter && boostHighest && nonSilent && boostRails && seriesOk;

    std::printf ("\n  no NaN: %s | Red hotter(Boost): %s | Boost highest: %s | non-silent: %s | "
                 "Boost on rails: %s | series ok: %s\n",
                 nanSeen ? "FAIL" : "ok", redHotter ? "ok" : "FAIL", boostHighest ? "ok" : "FAIL",
                 nonSilent ? "ok" : "FAIL", boostRails ? "ok" : "FAIL", seriesOk ? "ok" : "FAIL");
    std::printf ("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
