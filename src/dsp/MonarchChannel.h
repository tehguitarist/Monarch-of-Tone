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

    // ---- Drive-dependent capture-match voicing correction (two shelves, 2026-06-29) ----------
    // A/B vs the NAM captures, measured as a best-fit-gain-aligned EQ error across 40 Hz–16 kHz
    // at every gain/tone, showed the model-vs-pedal mismatch is a DRIVE-DEPENDENT TILT (a clean
    // line in log-f; tone-independent): the plugin is treble-short at low drive and bass-short /
    // treble-hot at high drive, crossing over near G4. A single fixed shelf can't fix a tilt that
    // reverses sign with drive. Re-deriving the literal 3-terminal DRIVE wiper-tap topology proved
    // the pot's dual action moves Stage 2's flat level, NOT Stage 1's tilt — so this is not in the
    // linear topology; it's the same class of second-order / capture-chain effect as the (retired)
    // TiltShelf and the even-harmonic injection, corrected empirically here. Two physically-keyed,
    // drive-scaled first-order shelves on Stage 1's output (NodeG, pre-clip so the clipper sees the
    // corrected spectrum), each fading to unity by the G4–G5 crossover:
    //   • HIGH-SHELF, treble lift, fades OUT as drive rises — restores the Stage-1 HF shelf that
    //     Av(s)=1+Z_upper/Z_lower lets collapse at low drive (the original "engaging it is dark").
    //   • LOW-SHELF, bass lift, fades IN as drive rises — counters the documented
    //     bass-bloom-under-drive the model under-does (real pedal blooms low end as it compresses).
    // (The top-octave deficit once blamed on capture aliasing was actually bilinear warping of the
    // base-rate linear solve — now fixed by running the linear stages oversampled; see warp* below.)
    static constexpr double shelfPivotHz = 450.0; // treble high-shelf geometric centre (Hz)
    static constexpr double shelfMaxDb = 5.6;     // HF lift at drive 0 (fades to 0 by ~drive 0.47)
    static constexpr double shelfSlopeDb = 11.8;  // dB of HF lift lost per unit drive
    static constexpr double bassPivotHz = 105.0;  // bass low-shelf geometric centre (Hz)
    static constexpr double bassOnsetDrive = 0.25;// bass lift starts engaging above this drive
    static constexpr double bassSlopeDb = 7.5;    // dB of LF lift gained per unit drive past onset
    static constexpr double bassMaxDb = 4.2;      // cap on the LF lift

    // ---- Bilinear-warp top-octave correction (rate-dependent high-shelf, 2026-06-29; recal 06-30) -
    // The linear WDF stages run at the oversampled rate (see PluginProcessor), but the bilinear
    // transform still warps the top octave DOWN at finite rates — the deficit vs the fully-resolved
    // 8x solve, measured at gain-2 Boost (16 kHz): −7.4 dB @1x(48k), −2.7 dB @2x(96k), −0.55 dB
    // @4x(192k), 0 @8x. Earlier this shelf was deliberately self-disabled by 2x (×(48k/rate)^4) on
    // the assumption 2x was "good enough" — but that left a 2–3 dB top-octave gap between the live
    // default (2x) and the render path (4x/8x), i.e. the bounce sounded brighter than playback.
    // Recalibrated (06-30) to track the actual deficit so 2x and 4x match 8x: lift =
    // warpScaleDb·(48k/rate)^warpExp at warpPivotHz, capped at warpMaxDb, then DC-NORMALIZED (see
    // prepareLinear) so the low/mid stay at unity at every rate. (scale,exp) were FIT (exact
    // prewarped-bilinear, per OS rate) to the warp-free-baseline-vs-8x deficit at 6/8/12/16 kHz.
    // A MODERATE pivot (6.5 k) is chosen on purpose over a higher one: a first-order shelf can't be
    // flat at 8 k AND steep at 16 k, and matching the 6–8 kHz PRESENCE band (where the guitar has
    // energy) to 8x matters far more than the 16 kHz edge (which carries no musical content). Result
    // vs 8x: DC–8 kHz within ~0.2 dB, 12 kHz ~0.4 dB, 16 kHz ~1.8 dB short at 2x (≈0.35 dB at 4x).
    // The low warpMaxDb cap holds 1x sane (a first-order shelf can't match 1x's near-Nyquist cliff —
    // 1x stays the low-CPU/approximate-top mode); 2x+ is full fidelity and live(2x)↔render(4x/8x)
    // now share the audible top octave. The DC-normalization is what lets the cap stay clean at 1x.
    static constexpr double warpPivotHz = 6500.0; // warp-correction high-shelf centre (Hz)
    static constexpr double warpScaleDb = 10.6;   // base HF lift at 48k; ×(48k/rate)^warpExp
    static constexpr double warpExp = 2.20;       // rate falloff from the fitted ghi₂ₓ/ghi₄ₓ ratio
    static constexpr double warpMaxDb = 3.0;      // cap (1x; kept low so the prewarped shelf holds unity DC)

    // ---- Overdrive clip-depth-gated low-mid restoration (2026-07-04) --------------------------
    // Farina linear-TF audit vs the captures (analysis/mid_eq_audit.py) shows the Overdrive channel
    // ALONE falls short in the low mids as it is driven HARD: a broad, ~flat shortfall of ~1.8 dB
    // below ~500 Hz that appears only at high clip depth (≈0 at normal levels, growing to −1.8 dB at
    // the hottest −6 dB sweep), CONSISTENT across every gain. Distortion matches (<0.6 dB) and Boost
    // has a separate knob-tilt — so this is OD-specific: the soft feedback clipper compresses the low
    // mids more than the real pedal's does. Restored with a first-order LOW-SHELF on the clip output
    // (post-clip, so clipping can't re-compress it), its lift BLENDED IN by the existing clip-depth
    // envelope `clipEnv` and applied ONLY in OD (sw1On): near-zero at normal playing, reaching the
    // shelf only when digging in hard. It is NOT a fixed pre-clip bump — that (the reverted 335 Hz
    // peak) failed: wrong drive-profile, and pre-clip compression ate it (see CLAUDE.md).
    // Calibrated against the captures by the artifact-immune TIME-DOMAIN null across all gains (a
    // swept-sine linear_tf mis-reads a clip-gated correction — its gate modulates across the sweep,
    // corrupting the deconvolution). Roughly halves the hot-drive deficit at every gain (G5 60–500 Hz
    // −1.6→−0.8, overall null −1.2 dB), inert (≤0.1 dB null) at normal levels, byte-identical in
    // Boost/Distortion, worst case ~+0.3 dB null at the G10+hot extreme (max drive + hottest input).
    static constexpr double odShelfPivotHz = 520.0; // low-shelf corner (Hz)
    static constexpr double odShelfMaxDb = 2.0;     // shelf lift at full clip-depth gate (dB)
    static constexpr double odGateScale = 12.0;     // clipEnv→gate steepness (tanh): engages only under hard clip

    explicit MonarchChannel (bool hiGain = false) : stage1 (hiGain), hiGainStage1 (hiGain) {}

    // The WHOLE channel now runs at the OVERSAMPLED rate (PluginProcessor wraps Stage 1, the clip
    // span, and Tone/Volume in one oversampler), so the linear stages' near-Nyquist bilinear warp
    // shrinks with the OS factor. Both prepareLinear and prepareClip are re-called at the OS rate on
    // factor change. `rate` here is that effective (oversampled) rate; for standalone/1x it == base.
    void prepareLinear (double rate)
    {
        stage1.prepare (rate);
        tone.prepare (rate);
        volume.prepare (rate);
        shBaseRate = rate;
        updateDriveShelf (0.5); // default = unity pass-through until setDrive() runs
        // Bilinear-warp top-octave correction: rate-only, tracks the measured 1x/2x/4x→8x deficit
        // so the live (2x) and render (4x/8x) paths share the same top octave (see warp* consts).
        const double warpDb = std::min (warpMaxDb, warpScaleDb * std::pow (48000.0 / shBaseRate, warpExp));
        shelfCoeffs (1.0, std::pow (10.0, warpDb / 20.0), warpPivotHz, wsB0, wsB1, wsA1);
        // DC-normalize the warp shelf: a prewarped first-order high-shelf with a pivot up near the
        // (oversampled) Nyquist loses unity DC gain — the whole spectrum droops a few tenths of a dB
        // (and several dB at 1x), an audible broadband tone/level shift. Dividing by the measured DC
        // gain restores exact unity at DC at every rate, so we can place the pivot high enough to
        // reach the 16 kHz deficit while the low/mid stay untouched. H(z=1) = (b0+b1)/(1+a1).
        const double wsDc = (wsB0 + wsB1) / (1.0 + wsA1);
        wsB0 /= wsDc;
        wsB1 /= wsDc;
        // OD clip-gated low-shelf: fixed coeffs at the OS rate; a low-shelf sets ghi=1, glo=lift.
        shelfCoeffs (std::pow (10.0, odShelfMaxDb / 20.0), 1.0, odShelfPivotHz, olB0, olB1, olA1);
        hsX1 = hsY1 = lsX1 = lsY1 = wsX1 = wsY1 = olX1 = olY1 = 0.0;
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
        hsX1 = hsY1 = lsX1 = lsY1 = wsX1 = wsY1 = olX1 = olY1 = 0.0;
    }

    // ---- Parameter setters (call per block; tapers applied inside each stage) ----
    void setDrive (double d)
    {
        stage1.setDrive (d);
        updateDriveShelf (d); // drive-dependent Stage-1 voicing correction (see shelf* consts)
    }
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

    /** Diode-solve quality (Best eqn-39 vs Good eqn-18). NOT a user control: the FeatureProfile
        probe (tests/FeatureProfile.cpp) measured this "lever" and found it negligible on BOTH axes
        — Best vs Good null at −76 dB (OD), and identical CPU (both already use the cheap omega4
        kernel; Best just calls it twice). So there is no HQ/Eco button — the oversampling factor is
        the real CPU/quality control (see the README "Performance" note). This setter stays internal,
        defaulting to Best, purely so FeatureProfile can A/B the two solves and guard against the
        production path silently changing. Production always runs Best (byte-for-byte unchanged). */
    void setHighQuality (bool highQ) noexcept
    {
        sw1.setHighQuality (highQ);
        sw2.setHighQuality (highQ);
    }

    // Base-rate front: input network + Stage 1 → V(NodeG), then the drive-dependent voicing
    // correction (high-shelf; unity pass-through once drive ≳ 0.47, see shelf* consts).
    inline double processPre (double x) noexcept { return driveShelf (stage1.processSample (x)); }

    // Oversampled nonlinear span: Stage2 (or SW1 soft clip) → op-amp rail-sat → SW2 (or pass)
    // → V(node_HC). This is the ONLY part that should run at the oversampled rate.
    inline double processClip (double nodeG) noexcept
    {
        double pin7 = sw1On ? sw1.processSample (nodeG) : stage2.processSample (nodeG);
        pin7 = railSaturateADAA (pin7); // op-amp output ceiling, antialiased (Boost always; Dist via Stage2)
        const double hc = sw2On ? sw2.processSample (pin7) : pin7;
        return odLowShelf (injectEvenHarmonic (hc, nodeG));
    }

    // OD-only clip-depth-gated low-mid restoration (see odShelf* consts). The low-shelf runs
    // continuously (state stays coherent across mode changes); its lift is BLENDED IN only in
    // Overdrive and only in proportion to clip depth, so it is inert in Boost/Distortion and at
    // normal OD levels, engaging solely when the soft clipper is being driven hard. `clipEnv` is
    // the same clip-depth envelope injectEvenHarmonic maintains (updated just above, this sample).
    inline double odLowShelf (double x) noexcept
    {
        const double shelfed = olB0 * x + olB1 * olX1 - olA1 * olY1;
        olX1 = x;
        olY1 = shelfed;
        const double gate = sw1On ? std::tanh (odGateScale * clipEnv) : 0.0;
        return x + gate * (shelfed - x);
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

    // First-order shelf coeffs (bilinear, prewarped — mirrors TiltShelf). `glo`/`ghi` are the
    // LF/HF linear-gain asymptotes, `pivot` the geometric centre. ghi=glo → exact unity passthrough.
    // Writes b0/b1/a1. A high-shelf sets glo=1; a low-shelf sets ghi=1.
    void shelfCoeffs (double glo, double ghi, double pivot, double& b0, double& b1, double& a1) const noexcept
    {
        const double rt = std::sqrt (ghi / glo);
        const double fz = pivot / rt; // zero
        const double fp = pivot * rt; // pole
        const double K = 2.0 * shBaseRate;
        const double wz = K * std::tan (M_PI * fz / shBaseRate);
        const double wp = K * std::tan (M_PI * fp / shBaseRate);
        const double a0 = K + wp;
        a1 = (wp - K) / a0;
        b0 = ghi * (K + wz) / a0;
        b1 = ghi * (wz - K) / a0;
    }

    // Drive-dependent capture-match correction (see shelf*/bass* consts): a treble HIGH-SHELF that
    // fades OUT with drive + a bass LOW-SHELF that fades IN with drive, both on Stage 1's output.
    void updateDriveShelf (double drive01) noexcept
    {
        const double trebleDb = std::max (0.0, shelfMaxDb - shelfSlopeDb * drive01);          // HF lift
        const double bassDb = std::min (bassMaxDb, std::max (0.0, bassSlopeDb * (drive01 - bassOnsetDrive)));
        shelfCoeffs (1.0, std::pow (10.0, trebleDb / 20.0), shelfPivotHz, hsB0, hsB1, hsA1);  // high-shelf
        shelfCoeffs (std::pow (10.0, bassDb / 20.0), 1.0, bassPivotHz, lsB0, lsB1, lsA1);     // low-shelf
    }

    inline double driveShelf (double x) noexcept
    {
        const double t = hsB0 * x + hsB1 * hsX1 - hsA1 * hsY1; // treble high-shelf
        hsX1 = x;
        hsY1 = t;
        const double b = lsB0 * t + lsB1 * lsX1 - lsA1 * lsY1; // bass low-shelf
        lsX1 = t;
        lsY1 = b;
        const double y = wsB0 * b + wsB1 * wsX1 - wsA1 * wsY1; // bilinear-warp top-octave correction
        wsX1 = b;
        wsY1 = y;
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

    // Capture-match correction: treble high-shelf (hs*) + bass low-shelf (ls*) + bilinear-warp
    // top-octave high-shelf (ws*). shBaseRate is the effective (oversampled) rate.
    double shBaseRate { 48000.0 };
    double hsB0 { 1.0 }, hsB1 { 0.0 }, hsA1 { 0.0 }, hsX1 { 0.0 }, hsY1 { 0.0 };
    double lsB0 { 1.0 }, lsB1 { 0.0 }, lsA1 { 0.0 }, lsX1 { 0.0 }, lsY1 { 0.0 };
    double wsB0 { 1.0 }, wsB1 { 0.0 }, wsA1 { 0.0 }, wsX1 { 0.0 }, wsY1 { 0.0 };

    // OD clip-depth-gated low-mid restoration (ol* = OD low-shelf; runs post-clip at the OS rate).
    double olB0 { 1.0 }, olB1 { 0.0 }, olA1 { 0.0 }, olX1 { 0.0 }, olY1 { 0.0 };

    double clipEnv { 0.0 };   // clipping-depth envelope (gates the even-harmonic coeff)
    double meanSq { 0.0 };    // slow ⟨soft²⟩ (removes only DC from the H2 injection)
    double asymCoeff { 0.0 }; // fast envelope smoothing (clip-depth gate)
    double meanCoeff { 0.0 }; // slow envelope smoothing (DC removal)
    double xLp { 0.0 };       // low-passed clip output (low-band H2 source)
    double meanLow { 0.0 };   // slow ⟨xLp²⟩ (DC removal for the low band)
    double lpLowCoeff { 0.0 };// low-band low-pass coeff (set in prepareClip)
};

} // namespace monarch
