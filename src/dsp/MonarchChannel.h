#pragma once

#include <algorithm>
#include <cmath>

#include "Stage1.h"
#include "Stage2.h"
#include "SW1SoftClip.h"
#include "SW2HardClip.h"
#include "ToneStage.h"
#include "VolumePot.h"

namespace monarch
{

/**
 * A single King of Tone channel (Yellow or Red), full DSP chain.
 *
 * Signal path (circuit.md Section 13):
 *   in → Stage1 (IC_A, non-inv; incl. input network; fixed Hi-Gain on Red) → V(NodeG)
 *      → Stage2 (IC_B, inv ×−22)   — SW-1 OFF: stock Stage2; SW-1 ON: SW1SoftClip (soft clip)
 *      → op-amp rail saturation (±3.3 V soft knee). Models the JRC4580 output ceiling. It is
 *        load-bearing wherever the op-amp swing would exceed the rails: ALWAYS in Boost (no
 *        diodes) and in Distortion (the linear Stage2 ×−22 path reaches ~13.9 V before the
 *        hard-clip shunt), and at extreme drive in OD/Both. Tone-safe at normal levels: the
 *        feedback soft-clip (OD/Both) holds pin7 well below ±3 V, so the knee passes it
 *        unchanged there; it only ever clamps a swing the real op-amp would also clamp.
 *      → R12/node_HC            — SW-2 OFF: pass-through (R12 loading minor, circuit.md §10);
 *                                 SW-2 ON: SW2HardClip (R12 + 1S1588 shunt, hard clip)
 *      → ToneStage (passive TONE/Presence) → VolumePot (audio taper + C11/R14) → out
 *
 * Clipping mode (architecture.md): 0 Boost(—/—), 1 Overdrive(SW1/—), 2 Distortion(—/SW2)
 * — 3-way per channel. Hi Gain is fixed per channel (ctor flag → Stage1), not a runtime mode.
 */
class MonarchChannel
{
public:
    // ±3.3 V op-amp output ceiling around BIAS (JRC4580 on a 9 V rail) — circuit.md §4, dsp.md.
    // This is the 9 V baseline; setSupplyVoltage() scales it for the 12 V / 18 V mod (below).
    static constexpr double railV9V = 3.3;
    static constexpr double railKneeMargin = 0.3; // knee sits this far below the ceiling

    // ---- Even-harmonic match (capture A/B, 2026-06-20) -------------------------------------
    // The KOT clips symmetrically BY DESIGN, so the ideal WDF model makes only odd harmonics.
    // The real-pedal captures show a consistent 2nd harmonic (H2) the model lacks — a junction-
    // charge / op-amp-asymmetry behaviour. We proved it can't be reproduced by a circuit-accurate
    // DC offset: the feedback soft-clip (OD) and hard shunt (Dist) structurally reject internal
    // biasing (an offset shifts clamp LEVELS → equal duty → DC, blocked downstream), and even
    // harmonics REQUIRE duty-cycle asymmetry. So — like the capture-match tilt shelf — we inject
    // H2 directly at the clip output, keyed to a clipping-depth envelope so it tracks the
    // captures' H2-vs-level across drive / input level / mode:
    //   • OD / Distortion: H2 GROWS with clipping depth → coeff scales with clipEnv.
    //   • Boost: H2 ~level-independent once the rails clip → coeff saturates (tanh of clipEnv).
    // Injected as +k·(x² − ⟨x²⟩): x² is the even (H2) part; subtracting the running mean keeps it
    // DC-free. Sign of k sets the asymmetry direction (negative = OD/Dist, positive = Boost).
    static constexpr double asymOD = -0.43;    // OD even-harmonic mix coeff
    static constexpr double asymDist = -0.14;  // Distortion mix coeff
    static constexpr double asymBoost = 0.35;  // Boost mix coeff (modest; Boost's true trend is the
                                               // opposite — H2 falls with level via the op-amp DC
                                               // offset — which this topology resists, so we settle
                                               // for a moderate, level-independent warmth)
    static constexpr double asymThresh = 0.37; // clipEnv ignores drive below this (clean stays clean)
    static constexpr double asymDriveScale = 1.70; // sets where the H2 source saturates → the drive
                                                   // at which H2 peaks (it washes out above, matching
                                                   // the captures' non-monotonic H2-vs-gain: peak
                                                   // ~noon, lower at max drive)
    static constexpr double asymTauSeconds = 0.005;     // clip-depth envelope (gate) time constant
    static constexpr double asymMeanTauSeconds = 0.050; // DC-removal time constant — must be SLOW so
                                                        // it tracks only DC and preserves low-frequency
                                                        // even harmonics (a fast mean cancels them)

    // ---- Low-frequency even-harmonic path ----------------------------------------------------
    // The nodeG-sourced injection above misses LOW notes: Stage 1's high-shelf makes nodeG tiny at
    // low frequencies, so the gate never fires there even though the note clips (its odd harmonics
    // are correct). The captures show real low-note H2 (~−43 dB at 82 Hz). Fix: a second injection
    // sourced from a LOW-PASS of the clip OUTPUT x (node_HC) — x is large only when clipping (so it
    // self-gates → clean notes stay clean) and at low frequencies carries the clamped low note that
    // nodeG lacks. Its square's 2f component is the low-band H2. Empirically models the coupling-cap
    // "blocking distortion" the schematic can't (decision 2026-06-21).
    // Per-mode low-band coeff (the clip output x has a different amplitude per mode — OD ~1.6 V,
    // Dist ~0.58 V — so the same coeff gives different H2). Tuned to the captures' low-note H2.
    static constexpr double asymLowOD = -0.015;   // OD low-band H2 coeff
    static constexpr double asymLowDist = -0.042; // Distortion low-band H2 coeff
    static constexpr double asymLowBoost = 0.0;   // Boost low-band (none — boost low notes ~clean)
    static constexpr double asymLowFc = 150.0;    // low-band low-pass corner (Hz) — taper to ~440 Hz

    explicit MonarchChannel (bool hiGain = false) : stage1 (hiGain), hiGainStage1 (hiGain) {}

    // The linear stages run at the base rate; the nonlinear clip span (Stage2/SW1 + rail-sat
    // + SW2) runs at the OVERSAMPLED rate, so the OS factor changes only the anti-aliasing,
    // never the linear voicing. Prepare the two groups independently; re-prepare just the clip
    // group when the OS factor changes. For standalone/1x use, baseRate == clipRate.
    void prepareLinear (double baseRate)
    {
        stage1.prepare (baseRate);
        tone.prepare (baseRate);
        volume.prepare (baseRate);
    }

    void prepareClip (double clipRate)
    {
        stage2.prepare (clipRate);
        sw1.prepare (clipRate);
        sw2.prepare (clipRate);
        asymCoeff = std::exp (-1.0 / (asymTauSeconds * clipRate));      // fast: clip-depth gate
        meanCoeff = std::exp (-1.0 / (asymMeanTauSeconds * clipRate));  // slow: DC removal only
        lpLowCoeff = std::exp (-2.0 * M_PI * asymLowFc / clipRate);     // low-band low-pass corner
        clipEnv = 0.0;
        meanSq = 0.0;
        xLp = 0.0;
        meanLow = 0.0;
        railXprev = 0.0; // F(0)=0 for any rails
        railFprev = 0.0;
    }

    void prepare (double sampleRate, int /*samplesPerBlock*/ = 0)
    {
        prepareLinear (sampleRate);
        prepareClip (sampleRate);
    }

    void reset()
    {
        stage1.reset();
        stage2.reset();
        sw1.reset();
        sw2.reset();
        tone.reset();
        volume.reset();
        railXprev = 0.0;
        railFprev = 0.0;
    }

    // ---- Parameter setters (call per block; tapers applied inside each stage) ----
    void setDrive (double d) { stage1.setDrive (d); }
    void setTone (double t) { tone.setTone (t); }
    void setPresence (double p) { tone.setPresence (p); }
    void setVolume (double v) { volume.setVolume (v); }

    /** Supply-voltage mod (9/12/18 V). Simulates running the pedal on a higher supply: the
        op-amp rails move out to ±(Vsupply/2 − margin), so each +1 V of supply adds +0.5 V of
        usable swing around BIAS. Only the op-amp ceiling changes — the diode clip thresholds
        (±1.64 V soft / ±0.584 V hard) are set by junction physics and DO NOT move. So a higher
        supply gives more clean headroom (most audible in Boost, and a touch in Distortion's
        rail-clamped path) while OD/Dist diode voicing is essentially unchanged — exactly the
        real-world "18 V mod" behaviour. 9 V maps to the validated ±3.3 V baseline exactly. */
    void setSupplyVoltage (double vSupply) noexcept
    {
        railV = railV9V + (vSupply - 9.0) * 0.5; // +0.5 V swing per +1 V supply (rail moves ΔV/2)
        railKnee = railV - railKneeMargin;
        railFprev = railAntideriv (railXprev); // keep the ADAA antiderivative consistent with new rails
    }

    /** Clipping mode 0..2 (Boost/Overdrive/Distortion → SW-1/SW-2 on/off). 3-way per channel
        (no "Both" — dropped 2026-06-19 for the 3-position hardware toggle). processClip still
        handles any SW-1/SW-2 combination, so re-adding a stacked mode later is a 1-line change. */
    void setClippingMode (int mode)
    {
        sw1On = (mode == 1);
        sw2On = (mode == 2);
    }

    // Base-rate front: input network + Stage 1 → V(NodeG).
    inline double processPre (double x) noexcept { return stage1.processSample (x); }

    // Oversampled nonlinear span: Stage2 (or SW1 soft clip) → op-amp rail-sat → SW2 (or pass)
    // → V(node_HC). This is the ONLY part that should run at the oversampled rate.
    inline double processClip (double nodeG) noexcept
    {
        double pin7 = sw1On ? sw1.processSample (nodeG) : stage2.processSample (nodeG);
        pin7 = railSaturateADAA (pin7); // op-amp output ceiling, antialiased (Boost always; Dist via Stage2)
        const double hc = sw2On ? sw2.processSample (pin7) : pin7;
        return injectEvenHarmonic (hc, nodeG);
    }

    // Base-rate back: Tone → Volume → output.
    inline double processPost (double nodeHC) noexcept
    {
        return volume.processSample (tone.processSample (nodeHC));
    }

    /** Full chain at a single rate (standalone / tests / 1x). */
    inline double processSample (double x) noexcept { return processPost (processClip (processPre (x))); }

    bool isHiGain() const { return hiGainStage1; }

private:
    // Soft op-amp rail saturation: linear below ±railKnee, gentle tanh knee approaching ±railV.
    // Below the knee the signal passes UNCHANGED, so it never colours the feedback soft-clip's
    // sub-3 V output at normal drive. It clamps only swings the real op-amp would also clamp:
    // Boost (no diodes) and Distortion's linear-Stage2 ×−22 path always, OD/Both at extreme drive.
    inline double railSaturate (double v) const noexcept
    {
        const double a = std::abs (v);
        if (a <= railKnee)
            return v;
        const double over = (a - railKnee) / (railV - railKnee);
        const double clamped = railKnee + (railV - railKnee) * std::tanh (over);
        return std::copysign (clamped, v);
    }

    // Numerically-stable log(cosh(z)) for the rail-sat antiderivative (avoids cosh overflow).
    static inline double logCosh (double z) noexcept
    {
        const double az = std::abs (z);
        return az + std::log1p (std::exp (-2.0 * az)) - 0.6931471805599453; // − ln 2
    }

    // Antiderivative F of railSaturate (F' = railSaturate, F(0)=0). railSaturate is odd, so F is
    // even → F(v) = F(|v|). Below the knee f(v)=v → F=v²/2; above, f(v)=knee + w·tanh(u/w) with
    // u=|v|−knee, w=railV−knee → F = knee²/2 + knee·u + w²·logCosh(u/w). Used for first-order ADAA.
    inline double railAntideriv (double v) const noexcept
    {
        const double a = std::abs (v);
        if (a <= railKnee)
            return 0.5 * a * a;
        const double w = railV - railKnee;
        const double u = a - railKnee;
        return 0.5 * railKnee * railKnee + railKnee * u + w * w * logCosh (u / w);
    }

    // First-order antiderivative antialiasing of the rail saturation (DAFx-2020). Replaces the
    // pointwise f(x) with the averaged (F(x)−F(x₋₁))/(x−x₋₁), which suppresses the aliasing the
    // hard-ish knee would otherwise fold back — most audible in Boost (the rails are the ONLY
    // nonlinearity there). Falls back to the midpoint value when x≈x₋₁ (ill-conditioned divide).
    // This is in ADDITION to oversampling: the clip span (incl. this) already runs oversampled.
    inline double railSaturateADAA (double x) noexcept
    {
        const double Fx = railAntideriv (x);
        const double dx = x - railXprev;
        double y;
        if (std::abs (dx) < 1.0e-6)
            y = railSaturate (0.5 * (x + railXprev));
        else
            y = (Fx - railFprev) / dx;
        railXprev = x;
        railFprev = Fx;
        return y;
    }

    // Even-harmonic injection at the clip output (see the asym* constants). `x` = clip output
    // (node_HC), `nodeG` = clip-span input (drive level). A clipping-depth envelope gates/scales
    // the H2 so clean playing stays symmetric and the level-trend matches the captures.
    inline double injectEvenHarmonic (double x, double nodeG) noexcept
    {
        const double over = std::max (0.0, std::abs (nodeG) - asymThresh);
        clipEnv = asymCoeff * clipEnv + (1.0 - asymCoeff) * over;

        // The clip outputs are ~50%-duty squares (hard shunt / rails) or a soft-squaring knee
        // (OD) — neither has even harmonics a memoryless shaper can pull out at high drive. So
        // source the H2 from a BOUNDED soft-saturation of the pre-clip drive: tanh(nodeG·s) has a
        // clean 2f component at moderate drive but SQUARES UP at high drive (losing its own even
        // harmonics) — reproducing the captures' wash-out (H2 peaks ~noon, falls at max drive).
        // A clip-depth gate keeps clean playing symmetric; ⟨soft²⟩ is subtracted to stay DC-free.
        const double gate = std::tanh (4.0 * clipEnv);
        const double soft = std::tanh (asymDriveScale * nodeG);
        const double k = (sw1On ? asymOD : (sw2On ? asymDist : asymBoost)) * gate;

        meanSq = meanCoeff * meanSq + (1.0 - meanCoeff) * soft * soft;
        double out = x + k * (soft * soft - meanSq); // mid/high band — DC-free 2f injection

        // Low-frequency band: source the H2 from a low-pass of the clip output x (clamped only when
        // clipping → self-gating, clean stays clean). Catches low notes that clip but whose nodeG is
        // shelved down. At mid/high, xLp → small (x is above the corner) → no double injection.
        xLp = lpLowCoeff * xLp + (1.0 - lpLowCoeff) * x;
        meanLow = meanCoeff * meanLow + (1.0 - meanCoeff) * xLp * xLp;
        const double kLow = sw1On ? asymLowOD : (sw2On ? asymLowDist : asymLowBoost);
        out += kLow * (xLp * xLp - meanLow);
        return out;
    }

    Stage1 stage1;     // includes the fixed Hi-Gain selection for Red
    Stage2 stage2;     // stock inverting Stage 2 (SW-1 OFF path)
    SW1SoftClip sw1;   // Stage 2 with soft-clip diodes (SW-1 ON path)
    SW2HardClip sw2;   // R12 + 1S1588 hard-clip shunt (SW-2 ON path)
    ToneStage tone;
    VolumePot volume;

    bool sw1On { true };  // default Overdrive (SW-1 ON, SW-2 OFF)
    bool sw2On { false };
    bool hiGainStage1 { false };

    double railV { railV9V };                       // op-amp ceiling (V); 9 V default = ±3.3 V
    double railKnee { railV9V - railKneeMargin };   // soft-knee onset (V); set by setSupplyVoltage
    double railXprev { 0.0 };                       // ADAA state: previous rail-sat input
    double railFprev { 0.0 };                       // ADAA state: F(railXprev) (F(0)=0)

    double clipEnv { 0.0 };   // clipping-depth envelope (gates the even-harmonic coeff)
    double meanSq { 0.0 };    // slow ⟨soft²⟩ (removes only DC from the H2 injection)
    double asymCoeff { 0.0 }; // fast envelope smoothing (clip-depth gate)
    double meanCoeff { 0.0 }; // slow envelope smoothing (DC removal)
    double xLp { 0.0 };       // low-passed clip output (low-band H2 source)
    double meanLow { 0.0 };   // slow ⟨xLp²⟩ (DC removal for the low band)
    double lpLowCoeff { 0.0 };// low-band low-pass coeff (set in prepareClip)
};

} // namespace monarch
