// Stage 1 Hi-Gain mod — validation gate Step 4b.
//
// The Hi-Gain mod (fixed-on for the Red channel) raises the Stage-1 feedback floor resistor,
// shifting the whole DRIVE range UP. Stage1.h tunes the floor as a deliberate voicing choice over
// the literal R2=100k mod (measured as too hot; no Red captures exist). The shift is +⅙ of the
// knob sweep: Red at drive d behaves like Yellow at (d + 1/6) — clearly the hotter channel without
// a big gain jump (an earlier +⅓ tame A/B'd as still too hot).
//
// This test compares the stock Stage 1 (Yellow, floor = R6_floor ≈ 990 Ω) against the Hi-Gain
// Stage 1 (Red, floor = HiGain_floor ≈ 17.7 k) and verifies:
//   - Hi-Gain gain > stock gain at every DRIVE position (it is hotter everywhere),
//   - the +⅙-knob alignment: Red @d ≈ Yellow @(d + 1/6),
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

// Knob-clock mapping. The DRIVE knob's real sweep (PedalFace: 1.25π→2.75π) is 270° ≈ 7:30→4:30,
// i.e. 9 clock-hours over [0,1], noon = 0.5. So 9:00 = 1.5/9, 3:00 = 7.5/9.
constexpr double drive_9oclock = 1.5 / 9.0;  // ≈ 0.167
constexpr double drive_noon = 0.50;
constexpr double drive_3oclock = 7.5 / 9.0;  // ≈ 0.833
constexpr double hiGainShift = 1.0 / 6.0;    // Red runs +⅙ knob hotter than Yellow
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

    const std::vector<double> drives = { 0.0, drive_9oclock, 0.25, 0.5, drive_3oclock, 1.0 };
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

    // Hi-Gain is TAMED to +⅙ knob (Stage1.h HiGain_floor): Red at drive d behaves like Yellow at
    // (d + 1/6). Verify the alignment at two interior points (Red@9:00 ≈ Yellow@(9:00+⅙) and
    // Red@noon ≈ Yellow@(noon+⅙)); no Red captures exist, so this IS the spec. The shift is a pure
    // resistance offset, so the two stages match to well under a dB.
    auto gainAt = [&] (monarch::Stage1& st, double d) { st.setDrive (d); return measureGainDB (st, probeFreq, amp); };
    const double redAt9 = gainAt (hiGain, drive_9oclock);
    const double yellowAt9plus = gainAt (stock, drive_9oclock + hiGainShift);
    const double redAtNoon = gainAt (hiGain, drive_noon);
    const double yellowAtNoonPlus = gainAt (stock, drive_noon + hiGainShift);
    const double align9 = redAt9 - yellowAt9plus;
    const double alignNoon = redAtNoon - yellowAtNoonPlus;

    std::printf ("\n  +1/6-knob alignment (tamed Hi-Gain):\n");
    std::printf ("    Red @9:00 %.2f dB  vs  Yellow @9:00+1/6 %.2f dB  → Δ %+.2f dB\n", redAt9, yellowAt9plus, align9);
    std::printf ("    Red @noon %.2f dB  vs  Yellow @noon+1/6 %.2f dB  → Δ %+.2f dB\n", redAtNoon, yellowAtNoonPlus, alignNoon);

    const bool alignOk = std::abs (align9) < 0.6 && std::abs (alignNoon) < 0.6;
    const bool pass = hotterEverywhere && hiGainMonotonic && alignOk && ! nanSeen;

    std::printf ("\n  hiGain hotter everywhere: %s | hiGain DRIVE↑gain↑: %s | Red@d≈Yellow@(d+1/6): %s | no NaN: %s\n",
                 hotterEverywhere ? "ok" : "FAIL", hiGainMonotonic ? "ok" : "FAIL",
                 alignOk ? "ok" : "FAIL", nanSeen ? "FAIL" : "ok");
    std::printf ("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
