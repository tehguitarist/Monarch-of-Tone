// v1.5 step 5 GATE — what does running Stage 1 at the BASE rate actually cost?
//
// CPU_AUDIT.md §6.3 is the last open lever: `processPre` is 40 ns/sample of a 114/166/141 ns channel
// and it is paid ×OS, yet Stage 1 is LINEAR and cannot alias. Per the standing rule ("oversample what
// can ALIAS, not what is merely inaccurate") it should not be in the OS span at all. The blocker on
// record is that Stage 1's gain peak SWEEPS 2.8-5.0 kHz with DRIVE, so no fixed prewarp can correct
// the bilinear warp — and a drive-dependent one has never been derived.
//
// This probe is the gate that must pass BEFORE any of that is built. It answers three questions, and
// all three are about Stage 1 ALONE (no clipper, no shelves, no oversampler — those add confounds):
//
//   1. HOW BIG is the base-rate warp, in dB, per octave band? If it is ~nothing below 8 kHz then the
//      change is cheap and a top-octave shelf covers it. If it reaches into the presence band it is a
//      voicing change and has to be judged on the 44-capture null.
//   2. Does the PEAK move? Both in frequency (the warp compresses the axis) and in gain.
//   3. Does the error DEPEND ON DRIVE? This is the one that decides fixed-vs-drive-dependent prewarp.
//      A warp error that is the same shape at every drive needs one shelf; one that tracks the peak
//      needs `setDrive` to recompute it per block.
//
// ⚠️ Instrument validity (the standing trap — CPU_AUDIT.md §5b, three of OSFidelity's four sections
// have been caught on this). Stage 1 is linear and has NO knee, so there is no operating point to get
// wrong and the measurement is exact at any level: a steady sine's amplitude is read by quadrature
// correlation over an integer number of cycles, which is a clean projection, not an FFT bin estimate.
// The 8x column is the REFERENCE (the current shipped arrangement), not the truth; the analog
// prototype is warp-free, so read the 8x column as "what ships today" and remember it is itself
// ~1/8 of the 1x error away from the continuous-time answer.
//
// Header-only: DSP headers, no JUCE. ~1 s compile, ~15 s run.
//   clang++ -std=c++17 -O2 -I. -isystem libs/chowdsp_wdf/include analysis/v15_stage1_warp_probe.cpp \
//       -o analysis/.cache/v15_stage1_warp_probe && analysis/.cache/v15_stage1_warp_probe
#include "src/dsp/Stage1.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
constexpr double baseRate = 48000.0;
constexpr int osFactors[] = { 1, 2, 4, 8 };
constexpr int nOs = 4;
constexpr int refIdx = 3; // 8x = the shipped reference

// Steady-state magnitude of Stage 1 at one frequency, one rate, one drive.
//
// Settle first: the input high-pass is 22 nF into 1 M (tau = 22 ms), so a short run reads the
// transient, not the response. Then correlate against quadrature references over an INTEGER number
// of cycles so the projection is exact for a pure tone.
double magnitudeAt2 (double rate, bool hiGain, double drive01, double f)
{
    monarch::Stage1 s1 { hiGain }; // Yellow/stock — the channel the captures constrain
    s1.prepare (rate);
    s1.setDrive (drive01);

    const double settleSeconds = std::max (0.30, 20.0 / f); // >=13 tau, and >=20 cycles at low f
    const long settle = (long) (settleSeconds * rate);

    // Measurement window: an integer number of cycles, at least 8 of them and at least 0.20 s.
    const double cycles = std::max (8.0, std::floor (0.20 * f));
    const long window = (long) std::llround (cycles * rate / f);

    const double w = 2.0 * M_PI * f / rate;
    for (long n = 0; n < settle; ++n)
        s1.processSample (std::sin (w * (double) n));

    double sumI = 0.0, sumQ = 0.0;
    for (long n = 0; n < window; ++n)
    {
        const double ph = w * (double) (settle + n);
        const double y = s1.processSample (std::sin (ph));
        sumI += y * std::cos (ph);
        sumQ += y * std::sin (ph);
    }
    return 2.0 * std::hypot (sumI, sumQ) / (double) window;
}

double magnitudeAt (double rate, double drive01, double f) { return magnitudeAt2 (rate, false, drive01, f); }

double db (double a, double ref) { return 20.0 * std::log10 (std::max (a, 1e-300) / std::max (ref, 1e-300)); }

struct Peak { double freq = 0.0, gainDb = 0.0; };

// Peak of the gain curve, 1/48-octave grid over 1.5-8 kHz with a parabolic refinement in log-f.
Peak findPeak (double rate, double drive01)
{
    const double f0 = 1500.0, f1 = 8000.0;
    const int steps = (int) std::lround (std::log2 (f1 / f0) * 48.0);
    std::vector<double> fs, mag;
    fs.reserve ((size_t) steps + 1);
    mag.reserve ((size_t) steps + 1);
    for (int i = 0; i <= steps; ++i)
    {
        const double f = f0 * std::pow (2.0, std::log2 (f1 / f0) * (double) i / (double) steps);
        if (f >= 0.48 * rate) // keep clear of Nyquist, where the warp is a pole not a peak
            break;
        fs.push_back (f);
        mag.push_back (magnitudeAt (rate, drive01, f));
    }

    size_t best = 0;
    for (size_t i = 1; i < mag.size(); ++i)
        if (mag[i] > mag[best])
            best = i;

    Peak p;
    p.gainDb = 20.0 * std::log10 (mag[best]);
    p.freq = fs[best];
    if (best > 0 && best + 1 < mag.size()) // parabolic vertex in (log f, dB)
    {
        const double yL = 20.0 * std::log10 (mag[best - 1]);
        const double yC = p.gainDb;
        const double yR = 20.0 * std::log10 (mag[best + 1]);
        const double denom = yL - 2.0 * yC + yR;
        if (std::abs (denom) > 1e-12)
        {
            const double delta = 0.5 * (yL - yR) / denom; // in grid steps
            const double stepRatio = std::log2 (fs.back() / fs.front()) / (double) (fs.size() - 1);
            p.freq = fs[best] * std::pow (2.0, delta * stepRatio);
            p.gainDb = yC - 0.25 * (yL - yR) * delta;
        }
    }
    return p;
}
// `fit` mode — the CORRECTION TARGET, emitted as CSV for analysis/v15_s1warp_fit.py.
//
// Once Stage 1 runs at the base rate the warp stops being an OS-factor disagreement and becomes an
// absolute, rate-independent deficit vs the analog prototype. That target is NOISE-FREE (a
// higher-rate solve of the same filter), so the fit needs no captures and no null — unlike every
// EQ instrument in FR_THD_AUDIT.md, this one has a right answer.
//
// One row per (channel, session rate, drive, f): the dB the correction must supply, measured as
// base-rate vs an 8x-of-base solve. Read the 8x solve as the analog answer: its own residual warp
// is ~1/64 of the base rate's, i.e. ≤0.04 dB at 16 kHz where the base rate is 2.6 dB down.
void emitFitTarget()
{
    const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
    std::printf ("channel,rate,drive,rleg,f,deficit_db\n");
    for (int hi = 0; hi <= 1; ++hi)
    {
        // R_leg = floor + DRIVE — the physical key. C2's corner is 1/(2 pi R_leg C2), and it is
        // that corner walking into the top octave that IS the deficit, so a law in R_leg covers
        // Yellow and Red with one expression instead of two knob-keyed fits.
        const double floorR = hi ? monarch::Stage1::HiGain_floor : monarch::Stage1::R6_floor;
        for (double rate : rates)
            for (int d = 0; d <= 10; ++d)
            {
                const double drive01 = 0.1 * (double) d;
                const double rleg = floorR + drive01 * monarch::Stage1::DRIVE_max;
                for (double f = 1000.0; f < 0.4999 * rate; f *= std::pow (2.0, 1.0 / 6.0))
                {
                    monarch::Stage1 base { hi != 0 }, ref { hi != 0 };
                    (void) base;
                    (void) ref;
                    const double mb = magnitudeAt2 (rate, hi != 0, drive01, f);
                    const double mr = magnitudeAt2 (8.0 * rate, hi != 0, drive01, f);
                    std::printf ("%d,%.0f,%.2f,%.1f,%.2f,%.5f\n", hi, rate, drive01, rleg, f, db (mr, mb));
                }
            }
    }
}
} // namespace

int main (int argc, char** argv)
{
    if (argc > 1 && std::string (argv[1]) == "fit")
    {
        emitFitTarget();
        return 0;
    }

    const double drives[] = { 0.2, 0.5, 0.7, 1.0 };
    const int nDrives = 4;
    const double freqs[] = { 100.0, 1000.0, 2000.0, 3000.0, 4000.0, 6000.0, 8000.0, 12000.0, 16000.0 };
    const int nFreqs = 9;

    std::printf ("== v1.5 step 5 gate: Stage 1's BASE-RATE bilinear warp (Yellow, %.0f kHz base) ==\n\n",
                 baseRate);
    std::printf ("Q1/Q3 - how much darker does Stage 1 get at the base rate, and does it depend on DRIVE?\n");
    std::printf ("        Read DOWN a column for drive-dependence.\n\n");

    // magnitudes[os][drive][freq]
    static double mag[nOs][4][9];
    for (int o = 0; o < nOs; ++o)
        for (int d = 0; d < nDrives; ++d)
            for (int k = 0; k < nFreqs; ++k)
                mag[o][d][k] = magnitudeAt (baseRate * osFactors[o], drives[d], freqs[k]);

    // THE DECISION TABLE. The proposal runs Stage 1 at the BASE rate while the clip span keeps its
    // OS factor, so the cost at factor N is (Stage 1 @ 48 kHz) vs (Stage 1 @ N x 48 kHz) — NOT the
    // "deficit vs 8x" blocks below. At 1x the change is a no-op by construction (there is no OS span),
    // which is the built-in control: that row MUST read 0.00 everywhere or the probe is wrong.
    std::printf ("*** COST OF THE CHANGE — Stage 1 at 48 kHz vs Stage 1 at the span's rate, dB ***\n");
    std::printf ("    (negative = the change makes that OS factor darker there)\n\n");
    std::printf ("    %-4s %-7s", "OS", "drive");
    for (int k = 0; k < nFreqs; ++k)
        std::printf ("%9.0f", freqs[k]);
    std::printf ("\n");
    for (int o = 0; o < nOs; ++o)
    {
        for (int d = 0; d < nDrives; ++d)
        {
            std::printf ("    %-4d %-7.2f", osFactors[o], drives[d]);
            for (int k = 0; k < nFreqs; ++k)
                std::printf ("%9.2f", db (mag[0][d][k], mag[o][d][k]));
            std::printf ("\n");
        }
        std::printf ("\n");
    }

    std::printf ("Supporting view — each rate's own deficit vs the shipped 8x arrangement:\n\n");
    for (int o = 0; o < nOs; ++o)
    {
        if (o == refIdx)
            continue;
        std::printf ("  %dx (%.0f kHz)\n", osFactors[o], baseRate * osFactors[o] / 1000.0);
        std::printf ("    %-7s", "drive");
        for (int k = 0; k < nFreqs; ++k)
            std::printf ("%9.0f", freqs[k]);
        std::printf ("\n");
        for (int d = 0; d < nDrives; ++d)
        {
            std::printf ("    %-7.2f", drives[d]);
            for (int k = 0; k < nFreqs; ++k)
                std::printf ("%9.2f", db (mag[o][d][k], mag[refIdx][d][k]));
            std::printf ("\n");
        }
        std::printf ("\n");
    }

    std::printf ("Q2 - the gain peak: does it move, and does the move depend on DRIVE?\n");
    std::printf ("     (the on-record blocker: the peak sweeps 2.8-5.0 kHz with DRIVE, so a FIXED\n");
    std::printf ("      prewarp cannot correct it. A drive-INDEPENDENT shift here retires that.)\n\n");
    std::printf ("    %-7s", "drive");
    for (int o = 0; o < nOs; ++o)
        std::printf ("   %dx f_pk    %dx dB", osFactors[o], osFactors[o]);
    std::printf ("      d_f 1x-8x   d_oct 1x-8x   d_gain\n");

    for (int d = 0; d < nDrives; ++d)
    {
        Peak p[nOs];
        for (int o = 0; o < nOs; ++o)
            p[o] = findPeak (baseRate * osFactors[o], drives[d]);

        std::printf ("    %-7.2f", drives[d]);
        for (int o = 0; o < nOs; ++o)
            std::printf ("%10.0f%9.2f", p[o].freq, p[o].gainDb);
        std::printf ("%12.0f%14.3f%9.2f\n", p[0].freq - p[refIdx].freq,
                     std::log2 (p[0].freq / p[refIdx].freq), p[0].gainDb - p[refIdx].gainDb);
    }

    std::printf ("\nReading it:\n");
    std::printf ("  - If the 1x block is ~0 below 8 kHz, the cost is a top-octave shelf and `warp*`\n");
    std::printf ("    already has that shape (it was just refit in step 3 to 1.0 dB at 17 kHz).\n");
    std::printf ("  - If d_oct is the SAME at every drive, one fixed prewarp/shelf covers all drives\n");
    std::printf ("    and §6.3's on-record blocker is retired by measurement.\n");
    std::printf ("  - 2x is the LIVE default and 4x the RENDER default; 1x is the low-CPU corner.\n");
    return 0;
}
