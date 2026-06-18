// Tone stage frequency response — validation gate Step 6.
//
// Passive TONE/Presence network (circuit.md Section 11). Verifies:
//   - it is a treble-cut tone control: HF gain RISES as TONE rises (tone=0 darkest → tone=1
//     brightest), monotonically; low-frequency passband is ~flat and tone-independent-ish,
//   - the Presence trim reduces the hi-cut: HF gain RISES as Presence rises (default 0 = full
//     hi-cut = stock Bluesbreaker),
//   - passband level is sane (passive network: ≤ 0 dB, not silent), no NaN/instability.
// Measures the actual response from the implemented WDF model (not assumed).

#include "../src/dsp/ToneStage.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr double fs = 96000.0;

double measureGainDB (monarch::ToneStage& stage, double freq, double amp)
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
} // namespace

int main()
{
    monarch::ToneStage stage;
    stage.prepare (fs);

    const double amp = 0.1; // small-signal, linear stage
    const std::vector<double> freqs = { 100, 200, 500, 1000, 2000, 5000, 10000 };
    const std::vector<double> tones = { 0.0, 0.5, 1.0 };
    constexpr double hf = 5000.0; // HF probe for treble-cut / presence checks

    std::printf ("Tone stage frequency response (presence=0, fs=%.0f)\n", fs);
    std::printf ("  %8s", "freq[Hz]");
    for (double t : tones)
        std::printf ("  tone=%.1f", t);
    std::printf ("\n");

    bool nanSeen = false;
    double passbandRef = 0.0; // tone=0.5 @ 200 Hz
    for (double f : freqs)
    {
        std::printf ("  %8.0f", f);
        for (double t : tones)
        {
            stage.setPresence (0.0);
            stage.setTone (t);
            const double g = measureGainDB (stage, f, amp);
            if (std::isnan (g) || std::isinf (g))
                nanSeen = true;
            if (f == 200.0 && t == 0.5)
                passbandRef = g;
            std::printf ("  %7.2f", g);
        }
        std::printf ("\n");
    }

    // Treble-cut: HF (5 kHz) gain must rise monotonically with TONE.
    std::printf ("\n  treble-cut (gain @ %.0f Hz vs TONE, presence=0):\n", hf);
    double prev = -1e9;
    bool toneMonotonic = true;
    for (double t : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        stage.setPresence (0.0);
        stage.setTone (t);
        const double g = measureGainDB (stage, hf, amp);
        std::printf ("    tone=%.2f : %7.2f dB\n", t, g);
        if (g < prev - 0.05)
            toneMonotonic = false;
        prev = g;
    }

    // Presence: HF (5 kHz) gain must rise as Presence rises (fades the hi-cut cap out).
    std::printf ("\n  presence (gain @ %.0f Hz vs PRESENCE, tone=0.5):\n", hf);
    double prevP = -1e9;
    bool presenceMonotonic = true;
    for (double p : { 0.0, 0.25, 0.5, 0.75, 1.0 })
    {
        stage.setTone (0.5);
        stage.setPresence (p);
        const double g = measureGainDB (stage, hf, amp);
        std::printf ("    presence=%.2f : %7.2f dB\n", p, g);
        if (g < prevP - 0.05)
            presenceMonotonic = false;
        prevP = g;
    }

    std::printf ("\n  passband (tone=0.5 @ 200 Hz): %.2f dB (passive: expect ~0 to -3 dB)\n", passbandRef);

    const bool passbandOk = passbandRef < 0.5 && passbandRef > -6.0; // passive, mild divider loss
    const bool pass = toneMonotonic && presenceMonotonic && passbandOk && ! nanSeen;

    std::printf ("\n  TONE↑bright↑: %s | PRESENCE↑bright↑: %s | passband sane: %s | no NaN: %s\n",
                 toneMonotonic ? "ok" : "FAIL", presenceMonotonic ? "ok" : "FAIL",
                 passbandOk ? "ok" : "FAIL", nanSeen ? "FAIL" : "ok");
    std::printf ("%s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
