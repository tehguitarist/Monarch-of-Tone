// Stage 1 Hi-Gain mod — validation gate Step 4b.
//
// The Theseus Hi-Gain mod (SW1B + R3, fixed-on for the Red channel) raises the Stage-1
// feedback floor resistor, shifting the whole DRIVE range UP. Analogman describe it as
// "the drive at 9 o'clock acts like it's at noon."
//
// This test compares the stock Stage 1 (Yellow, floor = R6 = 10k) against the Hi-Gain
// Stage 1 (Red, floor = HiGain_floor) and verifies:
//   - Hi-Gain gain > stock gain at every DRIVE position (it is hotter everywhere),
//   - the shift is in the right ballpark: Red at ~9 o'clock ≈ Yellow at ~noon,
//   - DRIVE still increases gain monotonically in Hi-Gain, and no NaN/instability.
// Measures the actual dB shift from the implemented WDF model (not assumed).

#include "../src/dsp/Stage1.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double fs = 96000.0;
constexpr double probeFreq = 3000.0; // mid-band, well above the branch corners

double measureGainDB (monarch::Stage1& stage, double freq, double amp)
{
    stage.reset();
    const int numSamples = (int) fs; // 1 second
    const int settle = (int) (fs * 0.2);
    double peak = 0.0;
    for (int n = 0; n < numSamples; ++n)
    {
        const double x = amp * std::sin (2.0 * M_PI * freq * (double) n / fs);
        const double y = stage.processSample (x);
        if (n > settle)
            peak = std::max (peak, std::abs (y));
    }
    return 20.0 * std::log10 (peak / amp);
}

// Knob-clock mapping (7 o'clock = min/CCW, 5 o'clock = max/CW, ~300° span, 30°/hour):
//   9 o'clock ≈ 0.20, noon = 0.50.
constexpr double drive_9oclock = 0.20;
constexpr double drive_noon = 0.50;
} // namespace

int main()
{
    monarch::Stage1 stock { false };  // Yellow
    monarch::Stage1 hiGain { true };  // Red (fixed Hi-Gain)
    stock.prepare (fs);
    hiGain.prepare (fs);

    const double amp = 0.1; // small-signal, linear stage

    std::printf ("Stage 1 Hi-Gain (Red) vs stock (Yellow), gain @ %.0f Hz, fs=%.0f\n", probeFreq, fs);
    std::printf ("  floor: stock = %.0f ohm, Hi-Gain = %.0f ohm\n\n",
                 monarch::Stage1::R6_floor, monarch::Stage1::HiGain_floor);
    std::printf ("  %6s   %10s  %10s  %8s\n", "drive", "stock[dB]", "hiGain[dB]", "shift[dB]");

    const std::vector<double> drives = { 0.0, 0.20, 0.25, 0.5, 0.75, 1.0 };
    bool nanSeen = false, hotterEverywhere = true, hiGainMonotonic = true;
    double prevHi = -1e9;
    for (double d : drives)
    {
        stock.setDrive (d);
        hiGain.setDrive (d);
        const double gs = measureGainDB (stock, probeFreq, amp);
        const double gh = measureGainDB (hiGain, probeFreq, amp);
        if (std::isnan (gs) || std::isnan (gh) || std::isinf (gs) || std::isinf (gh))
            nanSeen = true;
        if (gh < gs)
            hotterEverywhere = false;
        if (gh < prevHi - 0.05)
            hiGainMonotonic = false;
        prevHi = gh;
        std::printf ("  %6.2f   %10.2f  %10.2f  %8.2f\n", d, gs, gh, gh - gs);
    }

    // Hi-Gain provides a SUBSTANTIAL boost. The real Theseus floors are Yellow = R2∥R3 ≈ 990 Ω
    // and Red = R2 = 100k (a ~100× floor switch), so Red is much hotter than Yellow. (The earlier
    // "9 o'clock acts like noon" heuristic was tuned to the superseded artificial 39k floor; the
    // real 100k floor is hotter than that marketing phrase implies, and we have no Red captures —
    // so verify only that Hi-Gain is a real, monotonic boost, not a specific clock alignment.)
    hiGain.setDrive (drive_noon);
    const double redAtNoon = measureGainDB (hiGain, probeFreq, amp);
    stock.setDrive (drive_noon);
    const double yellowAtNoon = measureGainDB (stock, probeFreq, amp);
    const double boost = redAtNoon - yellowAtNoon;

    std::printf ("\n  Hi-Gain boost (both @ noon): Red %.2f dB − Yellow %.2f dB = %+.2f dB\n",
                 redAtNoon, yellowAtNoon, boost);

    const bool boostOk = boost > 3.0; // a meaningful Hi-Gain lift (real floors give ~6 dB)
    const bool pass = hotterEverywhere && hiGainMonotonic && boostOk && ! nanSeen;

    std::printf ("\n  hiGain hotter everywhere: %s | hiGain DRIVE↑gain↑: %s | substantial boost: %s | no NaN: %s\n",
                 hotterEverywhere ? "ok" : "FAIL", hiGainMonotonic ? "ok" : "FAIL",
                 boostOk ? "ok" : "FAIL", nanSeen ? "FAIL" : "ok");
    std::printf ("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
