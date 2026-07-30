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
 *      → op-amp rail saturation (ASYMMETRIC soft knee, +3.9 / −2.7 V; see railAsymV) → DC block.
 *        Models the JRC4580 output ceiling. It is load-bearing wherever the op-amp swing would
 *        exceed the rails: ALWAYS in Boost (no diodes) and in Distortion (the linear Stage2 ×−22
 *        path reaches ~13.9 V before the hard-clip shunt), and at extreme drive in OD/Both.
 *        Tone-safe at normal levels: the feedback soft-clip (OD/Both) holds pin7 well below the
 *        knee, so it passes unchanged there; it only ever clamps a swing the real op-amp would
 *        also clamp. The ± ceilings differ, which is what generates Boost's even harmonics; the
 *        asymmetry is scaled off in Distortion, where SW-2's shunt loads pin7 ~25x harder.
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

    // ---- IC_A's ceiling (v1.4 P9, 2026-07-29) -------------------------------------------------
    // The rail saturation above was only ever applied to IC_B (pin7). IC_A is the same op-amp in
    // the same package on the same supply, and NodeG is its output pin — it has the same ceiling,
    // and the model let NodeG swing straight past it. Measured at the captures' hot-sweep input
    // level (0.436 V peak), peak |NodeG| is 2.36 V at G6, 3.12 at G7, 4.06 at G8 and 5.93 at G10,
    // against a +3.9 / −2.7 V ceiling: unbounded from G7 up. No free parameter — it reuses the
    // ceilings already fitted for IC_B, so this adds physics, not a fit.
    static constexpr bool stage1RailsEnabled = true;

    // ---- Tone/Volume at BASE rate (v1.5, 2026-07-29) ------------------------------------------
    // `processPost` (the 3-port tone R-type + the volume pot) is LINEAR, so it cannot alias — the
    // only thing it ever gained from the oversampled span was a smaller bilinear frequency warp, and
    // warp is correctable with a filter where aliasing is not. `analysis/perf_split_probe.cpp`
    // measured it at **27 ns/sample of a 111 ns Boost channel (24 %)**, paid ×OS for that one
    // benefit, so running it once per output sample instead of ×OS is the cheapest real CPU win
    // available.
    //
    // ⚠ It is a VOICING change, not a free optimisation: at 48 kHz the tone stack's own warp
    // reappears at every OS factor, and the `warp*` shelf was fitted with the tone stack warp-free
    // at 4x/8x. Judge on the 44-capture null, never on FR alone. Compile-time so the A/B is exact.
    static constexpr bool postAtBaseRate = true;

    // ---- Stage 1 at BASE rate (v1.5 step 5, 2026-07-30) ---------------------------------------
    // The same rule, applied to the other linear span. Stage 1 is a linear WDF — it cannot alias, so
    // the OS span only ever bought it a smaller bilinear warp, at **18.0 ns/sample paid ×OS** (§1),
    // the largest single remaining oversampled-linear cost. Per output frame at 8x that is 126 ns of
    // 729/1137/938 (Boost/OD/Dist) = **17.3 / 11.1 / 13.4 %** of the channel.
    //
    // Only Stage 1 moves. `processPre`'s other two blocks stay oversampled and MUST: IC_A's rail-sat
    // is a genuine nonlinearity (NodeG reaches 2.36-5.93 V against a +3.9/−2.7 V ceiling from G6 up),
    // and `driveShelf` sits downstream of it, so it cannot be hoisted upstream without changing what
    // the ceiling sees. The split is therefore Stage 1 | [rail-sat → driveMakeup → driveShelf].
    //
    // ⚠ It is a VOICING change confined to the top octave, and it is DRIVE-dependent — which is the
    // one thing the on-record objection got right, for the wrong reason.
    // `analysis/v15_stage1_warp_probe.cpp` (validated against the shipped `Stage1_FreqResponse` gate
    // to 0.01 dB / 18 Hz) measures the cost as ≤0.02 dB at and below 6 kHz at EVERY drive and rate,
    // then at drive 1.0: −0.16 dB at 8 kHz, −0.84 at 12 kHz, −2.58 at 16 kHz (8x; 2x −0.13/−0.68/
    // −2.17). At drive 0.5 the 16 kHz figure is only −1.05, so the deficit's DEPTH tracks the knob.
    // The peak is NOT the problem: it moves ≤0.062 octaves and its GAIN is identical to 0.01 dB at
    // every rate and drive, so there is nothing there for a prewarp to chase.
    // Judge on the 44-capture null, never on FR alone. Compile-time so the A/B is exact.
    static constexpr bool preAtBaseRate = true;

    // ---- ADAA identity-region early-out (v1.5 step 3, 2026-07-30) -----------------------------
    // First-order ADAA of the IDENTITY map is not the identity — it is a half-sample delay plus a
    // one-zero rolloff, and that is arithmetic, not a hypothesis. Below the knee `railAntideriv`
    // returns ½x², so the difference quotient is ½(x² − x₋₁²)/(x − x₋₁) = **(x + x₋₁)/2**, whose
    // magnitude response is |cos(π f / fs_os)|: per stage at 16 kHz, 6.02 dB at 1x, 1.25 at 2x,
    // 0.30 at 4x, 0.07 at 8x. Measured on the real processor (`OSFidelity` (c2)) it comes out at
    // exactly 4.00× that at every rate — 4 = 2 op-amp ceilings × 2 series pedal channels — so the
    // mechanism is confirmed. The identity cannot alias, so there is nothing there for ADAA to
    // suppress; every dB of the 33–46 dB of alias suppression ADAA is worth (CPU_AUDIT.md §4)
    // happens ABOVE the knee and is untouched by this.
    //
    // So: when the WHOLE interval [x₋₁, x] lies inside the linear region, return x. The state pair
    // is still maintained (F = ½x² there, no transcendental), so the first sample that crosses the
    // knee gets an exact difference quotient — the early-out changes nothing above the knee.
    //
    // ⚠ It is a VOICING change: `warp*` was fitted to a combined "1x/2x vs 8x" deficit of which
    // this droop was the larger part, so it has been compensating ADAA more than bilinear warp.
    // `warp*` AND `hfTrim` must be re-fitted in ONE pass with this in place — see CPU_AUDIT.md §5.
    static constexpr bool adaaIdentityEarlyOut = true;

    // ---- Rail ASYMMETRY (v1.4 P2, 2026-07-28) ------------------------------------------------
    // The op-amp's two output ceilings are NOT equal, for two independent circuit reasons:
    //   • BIAS is not mid-supply. Theseus measured 4.5 V against VCC/2 = 4.575 V (V+ = 9.15 V), so
    //     the positive headroom is ~0.075 V larger than the negative before the op-amp is even
    //     considered.
    //   • A bipolar class-AB output stage does not saturate symmetrically — the pull-up loses more
    //     to V+ (Vbe of the driver + Vce_sat) than the pull-down loses to V−, so Voh headroom is
    //     the smaller one. Net direction and size are FITTED below to the captures' Boost even
    //     harmonics, because no datasheet number covers this part under this load.
    // Modelled as a fixed VOLTAGE offset, not a fraction: both contributions are fixed drops from
    // the supply rails, so the asymmetry does not scale when setSupplyVoltage() moves the ceiling
    // (only the mean does). railVpos = railV + railAsymV, railVneg = railV − railAsymV.
    //
    // WHY THIS AND NOT MORE INJECTION (FR_THD_AUDIT.md P2): the pedal's Boost is strongly
    // asymmetric where the plugin's was effectively symmetric — H2/H4/H6 were 25/34/45 dB short
    // while H3/H5/H7 already matched within ~1 dB. Two unequal ceilings generate the WHOLE even
    // series at once, with the internal H2:H4:H6 ratios set by the clipping duty cycle, which is
    // exactly what one short empirical H2 term can never do.
    // FITTED 2026-07-28 to the Boost even harmonics over railAsymV ∈ [0, 0.75] (11 renders of the
    // capture subset, analysis/p2_rail_asym_fit.py). The SIGN was decided by the null, not by the
    // magnitudes: ±0.30 give identical harmonic magnitudes and identical clean-sweep nulls, but
    // +0.30 improves the driven Boost nulls while −0.30 degrades them by as much (+1.20 dB at the
    // −12 sweep) — so the positive ceiling is the higher one. The MAGNITUDE sits on the null's
    // plateau (0.45–0.60 all within 0.05 dB of the best driven-Boost null), at the top of it where
    // the harmonic match is best: H2/H4 land within 1.6 dB of the pedal, from 25/34 dB short.
    // Pushing on to 0.75 finishes H2/H4 exactly but gives back 0.1 dB of null, doubles the small
    // OD/Dist cost, and implies the op-amp swings to 0.6 V of V+ — not credible for a JRC4580.
    // At 0.60 the implied ceilings are 8.40 V and 1.80 V against V+ = 9.15 V, bias 4.5 V: 0.75 V
    // of headroom lost at the top, 1.80 V at the bottom, which is ordinary bipolar behaviour.
    static constexpr double railAsymV = 0.60; // ceiling offset (V); + = positive rail is the higher one

    // ---- ...but only while the output stage is LIGHTLY LOADED -------------------------------
    // A fixed asymmetry cannot satisfy both modes, and the captures say so unambiguously: the
    // pedal's Boost is strongly asymmetric (H2 −21 dBc) while its Distortion is almost perfectly
    // symmetric (H2 −51.5 dBc). Distortion's path IS rail-clamped (linear Stage 2 ×−22 reaches
    // ~13.9 V), so it inherits whatever asymmetry the rails have — measured, railAsymV = 0.60
    // lands Distortion's whole even series ~26 dB hot, and zeroing the empirical asymDist barely
    // dents it (+25.6 → +24.6), i.e. the rails are the source.
    //
    // The physical discriminator is the LOAD on the op-amp output, which the clip switch sets:
    //   • Boost / OD: node_HC sees only the 25k tone stack → ~0.13 mA out of pin 7.
    //   • Distortion: SW-2's 1S1588 pair clamps node_HC to ±0.584 V, so pin 7 drives
    //     (3.9 − 0.584)/R12 ≈ 3.3 mA — about 25x the current.
    // An op-amp's output saturation voltages are specified per load for exactly this reason: both
    // ceilings collapse toward the supply rails under load, and the asymmetry between them goes
    // with it. So the asymmetry is scaled down when SW-2 is loading the output. This is a MODE key
    // standing in for a load-dependent ceiling — the rail-sat is applied feed-forward at pin 7 and
    // has no access to the actual output current — but the mode is what sets the load, so the key
    // is exact even though the mechanism is modelled coarsely.
    // Fitted to 0 — the loaded output stage keeps NO measurable asymmetry. At 1.0 (ungated) the
    // Distortion even series runs ~26 dB hot; at 0 it returns to its pre-P2 state exactly, and
    // Distortion's remaining even-harmonic error (+14.7 dB at H2, from the empirical asymDist) is
    // untouched pre-existing work, i.e. P3's, not something P2 introduced. Note the injections
    // must STAY: zeroing asymDist/asymLowDist as well overshoots the other way, leaving Distortion
    // ~60 dB short of the pedal's H2, so they are still carrying that mode's evens.
    static constexpr double railAsymLoadedScale = 0.0;

    // An asymmetric clipper RECTIFIES: sustained clipping leaves a DC step at pin7. The circuit's
    // only DC block is C11/R14 at the very output, and its corner is 0.16 Hz — a ~1 s tail. That
    // is faithful to the pedal in isolation but wrong against every measurement of it: the NAM
    // capture chain is AC-coupled far above 0.16 Hz, so the captures carry no such tail (measured
    // on the loud 1 kHz calibration tone: plugin 8.7% DC vs pedal 0.08%, still 2.5% a second later
    // in the clean sweep that follows it). It is also simply bad behaviour for a plugin output.
    // So the rectified DC is removed at source with a slow one-pole mean, exactly as
    // injectEvenHarmonic already does for its own even-order term. 50 ms = a 3.2 Hz corner: far
    // below the 20 Hz sweep floor, so it removes the step WITHOUT touching any real harmonic (the
    // lowest H2 in play is 40 Hz).
    static constexpr double railDcTauSeconds = 0.050;

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
    //
    // NOTE (v1.4 P2): the reasoning above was right about OD/Dist and WRONG about Boost. In Boost
    // there are no diodes to reject an internal offset — the rails ARE the nonlinearity — so the
    // asymmetry can be, and now is, modelled at its physical source (railAsymV). asymBoost is
    // therefore RETIRED to 0: with the asymmetric rails in, restoring it to its old 0.35 moves the
    // Boost harmonics by ≤0.3 dB and the null by 0.01 dB, i.e. it no longer does anything. Kept as
    // a named zero rather than deleted so the A/B stays one edit away.
    static constexpr double asymOD = -0.267;    // OD even-harmonic mix coeff
    static constexpr double asymDist = -0.0706;  // Distortion mix coeff
    static constexpr double asymBoost = 0.0;   // RETIRED — superseded by railAsymV (was 0.35)
    static constexpr double asymThresh = 0.37; // clipEnv ignores drive below this (clean stays clean)
    static constexpr double asymDriveScale = 3.50; // sets where the H2 source saturates → the drive
                                                   // at which H2 peaks (it washes out above, matching
                                                   // the captures' non-monotonic H2-vs-gain: peak
                                                   // ~noon, lower at max drive). It ALSO sets the
                                                   // H2:H4:H6 ratio in this path's band — re-fitted
                                                   // 1.70 → 3.50 in P3.1 (see asymLowDriveScale)
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
    static constexpr double asymLowOD = -0.0178;   // OD low-band H2 coeff
    static constexpr double asymLowDist = -0.0153; // Distortion low-band H2 coeff
    static constexpr double asymLowBoost = -0.017; // Boost low-band — see P3.2 below
    static constexpr double asymLowFc = 150.0;    // low-band low-pass corner (Hz) — taper to ~440 Hz

    // ---- P3.2: Boost's evens below the rail knee (v1.4, 2026-07-28) --------------------------
    // asymLowBoost was 0 on the reasoning that "Boost low notes are ~clean", and after P2 gave the
    // rails their asymmetry that looked settled: Boost's evens came entirely from the rails and
    // matched the captures within ~1-3 dB. But P2 only ever measured the −6 dB sweep. The
    // whole-set `evens` view (added in P3.1) showed the other half: on the two QUIET driven sweeps
    // the plugin was not merely short but SILENT — 30 of 143 H2 cells at ≈−160 dBc, no even
    // mechanism engaged at all, against a pedal reading −18 to −59 dBc. Below the knee an
    // asymmetric clipper is still an EXACTLY symmetric map, so the plugin was mathematically
    // perfect where the real pedal is not.
    //
    // The pedal's readings there are real signal, not capture noise: analysis/p31_harm_floor.py
    // over the Boost captures puts every even order +58.7 dB clear of the chain's harmonic floor
    // at worst (median +100), and they track drive the way a harmonic does and a floor does not.
    //
    // No new mechanism was needed — P3.1's low path already does exactly this job, and in Boost it
    // was already wired and running with railV as its clamp reference (see clampRef); only its
    // coefficient was zero. Sourced from a low-pass of the clip output, it is always-on rather
    // than knee-triggered, which is the property the rails lack, and its lowEnv wash-out hands
    // over to the rails as drive rises — so it fills in the quiet regime and gets out of the way
    // in the loud one, with no threshold to tune.
    // FITTED in two passes. A coarse sweep of |asymLowBoost| ∈ [0.002, 0.12] on the Boost subset
    // (the scratch loop drives analysis/p2_rail_asym_fit.py) found a very broad rms minimum near
    // 0.030; the whole 44-capture set then discriminated inside it, where the aggregate rms is
    // flat to ~0.1 dB across 0.017–0.030 and **H2's bias is the only thing that really moves**
    // (+1.9 at 0.030 → +1.3 at 0.024 → +0.5 at 0.017). Zeroing H2 is P3.1's rule — the coefficient
    // sets level, and what is left in H4/H6 belongs to the knee, not to it — so 0.017 it is.
    // Whole set, driven sweeps, before → after:
    //     silent  H2 30 → 0,   H4 24 → 0,    H6 11 → 0
    //     H2  rms 47.7 →  6.9  bias −22.5 → +0.5
    //     H4  rms 43.2 →  8.3  bias −18.3 → −1.7
    //     H6  rms 31.5 → 11.1  bias −12.1 → −4.5
    // That puts Boost's even series on a par with OD/Distortion's (H2 rms 7.4 / 8.5), and its H4
    // and H6 bias are the best of the three modes. The quiet CLEAN sweep improves too and stays
    // SHORT rather than hot — the safe direction. H6's residual is the shared knee's limit, not
    // Boost's: asymLowDriveScale (4.90) turned out to give Boost the right H2:H4:H6 ratio as it
    // stands, so no Boost-specific knee was added and P3.1's fit is untouched.
    //
    // Guards. OD and Distortion are BYTE-IDENTICAL (this coefficient is selected only when neither
    // switch is on), so P3/P3.1's fits cannot move. The time-domain null is unchanged to 0.01 dB
    // on every capture — the injection is ~−50 dBc, far below what the null resolves. SMPTE/CCIF
    // IMD unchanged.
    //
    // SIGN: unlike railAsymV, whose two signs give identical magnitude spectra, the signs are NOT
    // equivalent here — this injection rides on the rails' own fixed-polarity asymmetry and can
    // reinforce or partly cancel it. The difference is small (measured at the coarse optimum:
    // subset rms 8.18 negative vs 8.46 positive, null within 0.01 dB either way) but negative
    // wins, and it agrees with asymLowOD/asymLowDist — one mechanism, one polarity, all 3 modes.
    //
    // NOT fixed, and worth stating plainly: this closes the "silent" pathology, not the whole gap.
    // The residual is concentrated at 100 Hz on the quietest sweep at HIGH drive, where the pedal
    // reads ≈−18 dBc — fully saturated — and the plugin, with the same 159 Hz Stage-2 HPF, simply
    // does not swing far enough to reach its rails. That is the documented LF shortfall (P1) seen
    // through the clipper rather than a second missing even mechanism, and P1 established it is
    // not correctable with any minimum-phase EQ. Do not chase it by raising this coefficient —
    // that trades H2's now-zero bias for a few dB of H6 and re-creates the LF overshoot P3 removed.

    // ---- Band split + low-band wash-out (v1.4 P3, 2026-07-28) ---------------------------------
    // Both injection paths above were running FULL-RANGE, and at low frequency that was the
    // dominant even-harmonic error in OD/Distortion: on the −6 dB sweep H2 ran +18.8 dB (OD) /
    // +16.5 dB (Dist) hot at 100 Hz, and its level TREND was backwards — the pedal's H2 FALLS as
    // it is driven harder, the plugin's rose. One cause per path, both rooted in the same fact
    // (Stage 1's high-shelf makes nodeG small below a few hundred Hz):
    //
    //  1. The mid/high path spilled into the low band and did not wash out there. Its wash-out
    //     depends on tanh(asymDriveScale·nodeG) squaring up, but with nodeG small the tanh stays
    //     in its LINEAR region, so the injection grows as nodeG² instead of collapsing. Measured
    //     by ablation (low path off): the mid path alone was still +15.5 dB hot at 100 Hz. Fixed
    //     by high-passing its source — the counterpart the low band's low-pass always implied but
    //     never had, so the two paths now split the spectrum instead of overlapping it. 400 Hz
    //     beat 200/300/800 on H2 at every anchor and level.
    //
    //  2. The low path cannot wash out on its own, because its source x is CLAMPED: the
    //     low-passed square stops growing with drive, so the injection sits flat while the pedal's
    //     H2 falls ~10 dB from the −18 to the −6 sweep. Nor can it borrow clipEnv, whose 0.37 V
    //     threshold is never reached at low frequency — that IS the shelving this path exists to
    //     cover, and a wash keyed to clipEnv moved H2 by 0.7 dB at the extreme, i.e. nothing. So
    //     the low band gets its OWN depth envelope, lowEnv: a low-passed nodeG with a threshold
    //     scaled to the drive that actually arrives down there, washing out as 1/(1+(wash·env)²).
    //
    // The THRESHOLD is what makes the shape right rather than merely smaller — below it the wash
    // is inert, so the clean and −18 sweeps (already matching within ~3 dB) are untouched and only
    // the two hot sweeps are pulled down. asymLowOD/asymLowDist are then raised 1.4x, which the
    // wash pays for at the hot end and which recovers the quiet end.
    // FITTED over asymMidFc ∈ {200,300,400,800} × asymLowThresh ∈ {0.03..0.25} × asymLowWash ∈
    // {8..60} × coeff ∈ {1.0,1.4,1.8,2.5}x (analysis/p2_rail_asym_fit.py, OD/Dist G2–G10). Over
    // all 44 captures, driven sweeps: H2 rms error OD 10.3 → 6.9 dB, Dist 10.2 → 7.9, and the
    // systematic bias is GONE (OD +2.8 → 0.0, Dist +3.0 → +0.2). Odd orders are bit-identical, as
    // was Boost at the time (asymBoost = asymLowBoost = 0 — its evens then came entirely from the
    // asymmetric rails; P3.2 above later gave the low path a Boost coefficient, which is why this
    // paragraph's "Boost unchanged" claim is historical), and the time-domain null is unchanged on
    // every one of the 44 captures.
    // H4/H6 were left 11–16 dB short here and written off as out of this mechanism's reach ("a
    // squared source only makes H2"). That was WRONG and P3.1 (below) fixed them inside the same
    // mechanism; the coefficients and thresholds on this line were re-fitted there.
    static constexpr double asymMidFc = 400.0;     // mid/high-band high-pass corner (Hz)
    static constexpr double asymLowThresh = 0.225; // low-band depth threshold (V of LP(nodeG))
    static constexpr double asymLowWash = 7.4;     // low-band wash-out strength vs lowEnv

    // ---- Even-series SHAPE: the H2:H4:H6 ratio (v1.4 P3.1, 2026-07-28) -----------------------
    // P3 fixed H2's level and trend but left H4/H6 11–16 dB short, and wrote that off as outside
    // this mechanism's reach ("a squared source only makes H2"). That was wrong. tanh(s·u)² is not
    // squaring a sine: tanh already carries 3f/5f, so its square carries f×3f → H4 and 3f×3f,
    // f×5f → H6. The whole even series is available; what sets the RATIO between the orders is the
    // tanh KNEE, i.e. how far into saturation the source is driven:
    //
    //     a = s·(source amplitude)   0.5 → H4−H2 −28 dB   1.7 → −13 dB   2.5 → −6 dB   5 → −2 dB
    //
    // The pedal wants H4 ≈ H2 − 9 dB and H6 ≈ H2 − 12 dB, i.e. a ≈ 2–5. Each path needed a
    // different fix, because they sat at very different points on that curve:
    //
    //  • MID/HIGH path: asymDriveScale had never been re-swept since before P2 (1.70 → 3.50). It
    //    owns the band above asymMidFc and it responds strongly there (at the 400/800 Hz anchors
    //    H6 went from 25/14 dB short to within ~6 dB on its own).
    //  • LOW path: its source is a low-pass of the CLAMPED clip output, so at low frequency xLp is
    //    very nearly a SINE (the low-pass strips the clipped waveform's own harmonics), and a
    //    squared sine is pure H2 with no H4 at all — this path had NO higher even orders by
    //    construction, which is why the LF anchors did not move when asymDriveScale was swept
    //    alone. It now runs through the same tanh knee. The knee is normalised by the mode's clamp
    //    voltage (OD ≈ ±1.64 V at the feedback diodes, Distortion ≈ ±0.584 V at the shunt) so ONE
    //    scale constant sets the same operating point in both modes; the per-mode asymLow*
    //    coefficients then only carry level, which is what they were always for.
    //
    // THE KNEE SETS THE RATIO, THE COEFFICIENT SETS THE LEVEL — and they must be fitted in that
    // order. A coordinate descent that scored all three orders together (H2 weighted x2) drifted
    // H2 +3.7/+5.1 dB hot while chasing H4/H6, i.e. it spent P3's headline result. Cutting all
    // four gains afterwards put H2's bias back to zero and cost only ~0.6 dB of the higher orders,
    // because a gain moves the whole even series together while the knee does not.
    // Result over ALL 44 captures, driven sweeps, cells with pedal > −70 dBc (OD / Distortion,
    // `fr_thd_audit.py evens`) — rms, and the bias that matters:
    //     H2  bias  −0.0/+0.2 → −0.0/−0.0   (rms  6.9/ 7.9 →  7.4/ 8.5)
    //     H4  bias −11.1/−11.2 → −1.5/−2.1  (rms 13.0/13.4 →  6.4/ 6.3)
    //     H6  bias −22.0/−18.2 → −7.4/−5.8  (rms 23.4/20.2 → 11.4/10.2)
    // Odd orders are untouched (the injection is even-order only), Boost is byte-identical, and
    // both the time-domain null (worst cell +0.04 dB) and the SMPTE twin-tone IMD (0.00 dB, and it
    // straddles this split) are unchanged — see FR_THD_AUDIT.md P3.1. H6 is still ~6 dB short:
    // pushing the knees further closes it but costs H2 rms faster than it gains H6.
    static constexpr double asymLowDriveScale = 4.90; // low-band tanh knee, in clamp units
    static constexpr double asymClampOD = 1.64;       // SW-1 soft-clip clamp (V) — MA856 ×2 series
    static constexpr double asymClampDist = 0.584;    // SW-2 hard-clip clamp (V) — 1S1588 pair

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
    //   • HIGH-SHELF, treble lift, fades OUT as drive rises. RETIRED by v1.4 P7 (2026-07-29) — its
    //     rationale ("restores the Stage-1 HF shelf that Av(s)=1+Z_upper/Z_lower lets collapse at low
    //     drive", the original "engaging it is dark") was never measured, and when it finally was,
    //     the captures said the opposite: the plugin was too BRIGHT at low drive. See the P7 note
    //     below the constants.
    //   • LOW-SHELF, bass lift, fades IN as drive rises — counters the documented
    //     bass-bloom-under-drive the model under-does (real pedal blooms low end as it compresses).
    // (The top-octave deficit once blamed on capture aliasing was actually bilinear warping of the
    // base-rate linear solve — now fixed by running the linear stages oversampled; see warp* below.)
    static constexpr double shelfPivotHz = 450.0; // treble high-shelf geometric centre (Hz)
    static constexpr double shelfMaxDb = 0.0;     // RETIRED by P7 (was 5.6) — the bell does this job
    static constexpr double shelfSlopeDb = 0.0;   // RETIRED by P7 (was 11.8)
    // Single source of truth: the analysis harnesses parse these constants out of this header, so
    // the flag is DERIVED from them and cannot drift from what the audio path actually does.
    // Covers both signs so it stays airtight if the instrument is ever revived as a CUT: the gain
    // law is max(0, shelfMaxDb − shelfSlopeDb·drive01), which is non-zero somewhere in
    // drive01 ∈ [0,1] iff shelfMaxDb > 0 OR shelfSlopeDb < 0.
    static constexpr bool trebleShelfEnabled = (shelfMaxDb > 0.0 || shelfSlopeDb < 0.0);
    // The Clean/Boost EQ error vs the captures is a bass TILT that reverses with drive — the plugin
    // runs ~+3 dB too bassy (a bump PEAKING ~180 Hz) at low drive (G2) and ~−1.8 dB too thin at high
    // drive (G10). The two ends need different SHAPES (a bell at low drive, a shelf at high drive), so
    // they are two separate drive-dependent corrections:
    //   • bass BOOST low-shelf — a HUMP in drive, not the original monotone ramp. Refit by v1.4 P8
    //     (2026-07-29), which merged P1's separate sub-64 Hz LF-extension shelf INTO this one rather
    //     than adding a second instrument to the same band on the same key — P7's rule. The ramp
    //     (105 Hz, onset G2.5, 7.5 dB/unit, cap 4.2) was fit to ONE end of the drive axis, the
    //     high-drive bass-bloom, and read as monotone because nothing had measured the low end.
    //     Measured across the whole axis, the pedal wants LF gain at EVERY drive — +1.2 dB already
    //     at G2 — peaking near G5 and then FALLING BACK, so the old law was ~1.2 dB short below G5
    //     and ~2.4 dB over at G10. The fall is what the ramp could not express at all.
    //     Law: bassBoostMaxDb at bassPeakDrive, falling by bassBoostSlopeDb per unit drive below it
    //     and bassBoostFallDb per unit above, floored at 0 (reached exactly at drive 0, so the floor
    //     never binds inside the knob's range). See FR_THD_AUDIT.md P8.
    static constexpr double bassPivotHz = 85.0;        // bass boost low-shelf centre (Hz)
    static constexpr double bassPeakDrive = 0.50;      // drive at which the LF boost peaks (≈ G5)
    static constexpr double bassBoostMaxDb = 3.0;      // dB of LF boost at the peak
    static constexpr double bassBoostSlopeDb = 6.0;    // dB lost per unit drive BELOW the peak
    static constexpr double bassBoostFallDb = 2.5;     // dB lost per unit drive ABOVE the peak
    //   • bass CUT bell, fades OUT with drive — removes the low-drive low-mid EXCESS. A WIDE peaking
    //     bell (not a shelf: a shelf over-cuts sub-100 and under-cuts the peak): the excess is broad
    //     (100-330 Hz, peaking ~200), so a low-Q bell centred 185 flattens it to ±0.2 dB at G2.
    // Refit with the treble shelf as ONE set by v1.4 P7 (2026-07-29): the pivot was already right,
    // but the bell now carries the WHOLE low-drive correction, so it is deeper (4.6 → 6.0) and
    // reaches one knob position further up (G5 → ~G5.5). maxDb is reached exactly at drive 0
    // (= slope × offDrive), so the clamp never binds anywhere inside the knob's range.
    static constexpr double bassCutPivotHz = 185.0;    // cut-bell centre (Hz)
    static constexpr double bassCutQ = 0.50;           // cut-bell width (low Q = wide, covers 100-330)
    static constexpr double bassCutOffDrive = 0.55;    // cut fades to 0 at this drive (≈ G5.5)
    static constexpr double bassCutSlopeDb = 10.909;   // dB of cut per unit drive below the cutoff
    static constexpr double bassCutMaxDb = 6.0;        // cap on the low-drive cut (hit at drive 0)
    // Fixed (drive-independent) HF trim high-shelf: eases the plugin's slightly-hot top end toward
    // the captures (matches them within ~0.3 dB across 2-4.5k, where the captures are reliable).
    static constexpr double hfTrimPivotHz = 4500.0;    // HF-trim high-shelf centre (Hz)
    static constexpr double hfTrimDb = -1.3;           // HF cut above the pivot (dB)
    // ---- v1.4 P7 (2026-07-29): why the treble half above is retired ---------------------------
    // The treble lift and the bass-cut bell were fit INDEPENDENTLY (2026-06-29 / 07-04) and were
    // each supplying about half of ONE correction, so together they delivered ~6.6 dB of tilt at G2
    // where the captures need ~3.95. Measured as a set on Boost + clean sweep, the whole drive-keyed
    // defect is a single see-saw about 508 Hz — that band reads −0.16…−0.31 dB at EVERY drive, i.e.
    // it is the pivot — whose tilt runs +3.95/+2.73/+1.25/+0.20/+0.02 dB at G2…G6 and then reverses.
    // The bell alone reproduces all of it (a 185 Hz cut lifts everything above it relatively), so the
    // shelf had nothing left to do: deleting it and re-fitting the bell beat keeping a reduced shelf
    // on the null in every mode. FR shape rms over G2–G6 0.941 → 0.259 dB; nulls up to 10.2 dB
    // deeper at G2. Byte-identical at and above G5.5, where both instruments are already zero.
    // NOTE the earlier claim above — that the lift "restores the Stage-1 HF shelf Av(s) lets
    // collapse at low drive" — was never measured; the captures say the plugin was too BRIGHT at low
    // drive, not too dark. See FR_THD_AUDIT.md P7.

    // ---- DRIVE make-up: the 3-terminal pot's missing second action (v1.4 P6, 2026-07-28) --------
    // FLAT (no EQ) gain into Stage 2, keyed to the DRIVE knob, zero at and below `driveMakeupOnset`
    // and rising above it. This is NOT another correction shelf — it is the half of the real pot's
    // dual action the 2-terminal rheostat approximation drops. circuit.md §7 keeps the 2-terminal
    // model because the literal 3-terminal wiring over-swings Stage-2 gain (~28 dB vs the measured
    // ~10.6 dB), and the earlier re-derivation established what the dual action actually does: it
    // moves Stage 2's FLAT LEVEL, not Stage 1's tilt. The literal topology therefore had the right
    // SHAPE and the wrong MAGNITUDE; this is that shape, fitted.
    //
    // Measured two independent ways, in agreement (FR_THD_AUDIT.md P6):
    //   • FR peak location is a clip-depth meter — the plugin's overall FR peak moves -0.35 oct per
    //     +3 dB of pre-clip level. Reading the peak error back through that calibration, the drive
    //     the plugin is SHORT of is ~0 dB up to G5, then +3.2 (G6) / +3.7 (G7) / +5.5 (G8) / +6.8
    //     (G10) in OD, with the same shape in Distortion.
    //   • The time-domain null (the standing arbiter) splits at the same knob position: adding
    //     pre-clip level HURTS at G5 and HELPS from G6 up (OD G6 -16.9 -> -22.1 dB at +3 dB).
    // So the deficit is a gain-vs-knob CURVE error above G5, not a broadband tilt — which is why
    // every EQ-shaped attempt at this reversed sign with drive.
    static constexpr double driveMakeupOnset = 0.5;    // knob position where the deficit starts (G5)
    static constexpr double driveMakeupSlopeDb = 14.0; // dB of make-up per unit drive past the onset
    static constexpr double driveMakeupMaxDb = 6.0;    // cap (reached ~G9; the G10 need is +6.8)

    // ---- LF extension (fixed low-shelf, 2026-07-28 — FR_THD_AUDIT.md Finding 1 / P1) -----------
    // The plugin is short of the real pedal below ~64 Hz in EVERY mode at EVERY drive, and the gap
    // SURVIVES stripping every correction shelf above (fr_thd_audit.py `raw`): it is in the raw WDF
    // circuit, not a mis-tuned shelf. Mode-independent (Boost −3.11 / Dist −3.13 / OD −3.64 dB at
    // 20 Hz), so it is not a clipper artifact either.
    //
    // NOT a topology bug — the schematic-checker traced every pole/zero-capable RC in both
    // schematics (signal path AND the bias/supply network, whose exclusion was re-verified by
    // computing Z_VB ≈ 32 Ω @ 50 Hz) and NOTHING lands near 45-55 Hz: the only audio-path corners
    // are the input HPF (7.2 Hz), Stage 2's C7/R9 (159 Hz), Stage 1's feedback ladder (~589 Hz) and
    // the output HPF (0.16 Hz). The named suspect — the literal 3-terminal DRIVE wiper-tap dropped
    // by the 2-terminal rheostat approximation — was traced to R6=10k/C5=100n, i.e. the SAME 159 Hz
    // corner already modelled as R9/C7, so it contributes no sub-60 Hz content at all (it only
    // redistributes gain, which is why it was rejected on separate grounds; circuit.md §7).
    //
    // So this is empirical, like the drive shelves. A SHELF, not a lower high-pass corner, is the
    // right instrument and that is a shape argument, not a convenience: the deficit returns to 0 dB
    // by 160 Hz, and 100-800 Hz already matches to ±0.3 dB at G5. Re-cornering Stage 2's HPF from
    // 159 Hz to ~72 Hz (C7 100n→220n) would buy +6.7 dB at 20 Hz but drag +2.2 dB along at 160 Hz
    // and +0.7 dB at 320 Hz — a pole alone never returns to unity. A pole/ZERO pair does.
    //
    // >>> RETIRED (lfExtEnabled = false). The deficit is REAL but is NOT correctable by any filter
    // a real-time plugin can use. Kept, disabled, for A/B — do not re-enable without reading this.
    //
    // Two fits were built and measured against all 44 captures:
    //   +3.5 dB @ 60 Hz (min FR rms):  FR error improved on 33/42 captures (median rms 2.31→1.98)
    //                                  and the NULL got worse on 27/42, mean +0.95 dB.
    //   +5.0 dB @ 25 Hz (confined to   Same story: null worse on 28/42, mean +1.08 dB. Confining
    //   the drive-agreed 25-64 Hz):    the shelf did NOT help, which killed the first hypothesis
    //                                  (spill into the energy-carrying 80-160 Hz band).
    // Worst regressions were the mid-gain sweet spot, i.e. exactly the captures that matched best:
    // G6 T5 Clean −22.0 → −17.7, G7 T5 Dist −17.9 → −13.5.
    //
    // WHY, measured directly (complex transfer function, pedal vs plugin, clean sweep, 1 kHz-norm):
    //   Hz          20     25     32     40     50     64     80    101
    //   |ped|-|plug|  +2.7   +2.8   +2.5   +1.9   +1.3   +0.5   -0.0   -0.5   dB   (G5 T5 Clean)
    //   phase ped-plug +33°   +21°   +10°    +2°    -3°    -6°    -7°    -6°
    // The pedal is louder at 20-40 Hz AND its phase LEADS. A minimum-phase low-shelf that adds
    // +3 dB at 20 Hz necessarily contributes about −15° of LAG. So the magnitude error goes to zero
    // while the phase error grows 33° → 48°, and the COMPLEX residual gets bigger: |1.36∠33°−1| =
    // 0.76 before, |0.96∠48°−1| = 0.81 after. The null is measuring that, and it is right to.
    //
    // Proof it is the phase and not the magnitude — the identical magnitude correction applied to
    // the same renders offline, minimum-phase vs zero-phase (null depth, dB, clean sweep):
    //                  baseline   min-phase   zero-phase
    //   G6 T5 Dist        −19.6      −15.6       −20.6
    //   G7 T5 Dist        −17.6      −13.6       −18.1
    //   G5 T5 OD          −21.1      −19.1       −22.4
    // Zero-phase helps on every case (mean ~0.6 dB); minimum-phase hurts on every case. But a
    // zero-phase shelf reaching 25 Hz is a multi-thousand-tap FIR — tens of ms of latency — which
    // is an absurd price for 0.6 dB, and unusable live. Hence: retired, not fixed.
    //
    // (Had it shipped it would have gone PRE-clip, here in driveShelf, because the LF THD shortfall
    // is a CONSEQUENCE of this FR shortfall — the plugin's low end never reaches the rails: 40 Hz,
    // G10 Clean, −6 dB sweep, pedal 35.6% THD vs plugin 4.7%. That THD gap is likewise not
    // separately fixable, since its cause is this un-correctable LF gap.)
    static constexpr bool   lfExtEnabled = false;      // RETIRED — min-phase LF boost worsens the null
    static constexpr double lfExtPivotHz = 25.0;       // LF-extension low-shelf centre (Hz)
    static constexpr double lfExtDb = 5.0;             // LF lift below the pivot (dB)

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
    //
    // ⚠⚠ REFITTED 2026-07-30 (v1.5 step 3), and the base lift fell 10.6 → 1.0 dB — a 10× cut,
    // because MOST OF WHAT THIS SHELF WAS CORRECTING WAS NEVER BILINEAR WARP. Two errors, both in
    // the instrument, both now fixed in `tests/OSFidelity` (a):
    //   1. The ADAA identity-region droop (see adaaIdentityEarlyOut / CPU_AUDIT.md §5) supplied
    //      12.04 of the 13.12 dB at 12 kHz and 24.08 of the 32.52 at 16 kHz that the 06-30 fit
    //      read as "warp". The early-out removes it at source.
    //   2. (a) itself was measured at 0.01 FS in Overdrive, where Stage 1's ~4 kHz gain peak puts
    //      pin7 past the 0.5 V sw1Ceil knee — so the "small-signal FR" ran through the soft
    //      clipper in exactly the presence band this pivot serves. Measured at a genuinely linear
    //      5e-4, the analytic shelf model reproduces a candidate's contribution to 0.01 dB at
    //      every rate; against the contaminated baseline it was off 1.64 dB at 1x/8 kHz.
    // With both fixed, the residual warp is −0.14 / −0.86 / −3.08 dB at 8 / 12 / 16 kHz at 1x and
    // ≤0.44 dB at 2x — i.e. essentially nothing below 12 kHz at any rate. Refit
    // (analysis/v15_warp_refit.py, weighted to the presence band, constrained to VANISH at 8x so
    // the accuracy reference is not moved): 1x lands within +0.27 / −0.74 dB, 2x within 0.14, 4x
    // within 0.02. Weighted rms 0.415 (no shelf) → 0.155.
    //
    // The pivot moved 6.5 k → 17 k with it. That is not a taste change: the old moderate pivot
    // existed because the deficit it was fitted to started in the presence band, and the real
    // residual does not — it is confined to the top octave, which is what a high pivot fits. But
    // a pivot that high needs the guard in prepareLinear: shelfCoeffs prewarps the POLE to
    // pivot·√ghi, so at 1x on a low session rate (32 kHz) tan() would cross Nyquist and the
    // filter would be unstable. warpMaxDb = 1.0 is load-bearing for the same reason — at 44.1 kHz
    // 1x the uncapped law asks for 1.17 dB, and the cap is what keeps the pole where it was fit.
    //
    // ⚠⚠⚠ RETIRED by v1.5 step 5 (2026-07-30) — `warpScaleDb = 0`, compiled out via the DERIVED
    // `warpShelfEnabled` (the trebleShelfEnabled pattern, so the audio path and the header-parsing
    // harnesses cannot disagree). Not a retune: this shelf is keyed to the OS RATE, and step 5 moved
    // Stage 1 to the BASE rate, which removes the rate-dependence AT SOURCE. Stage 1 was ~97 % of all
    // the remaining bilinear warp in the plugin: with it at the base rate and this shelf disabled,
    // `OSFidelity` (a) puts 1x within 0.26 dB of 8x at 16 kHz (2x within 0.06) — three times better
    // than the +0.28/−0.75 that shipped WITH the shelf in step 3. So there is no rate disagreement
    // left for it to correct, and leaving it in is a pure over-correction: measured, it puts 1x
    // +2.23 dB ABOVE 8x at 16 kHz.
    //
    // The physical defect did not disappear — it became ABSOLUTE (present at every OS factor,
    // including render) and is keyed to DRIVE, not to rate. See s1Warp* below, which replaces it.
    static constexpr double warpPivotHz = 17000.0; // warp-correction high-shelf centre (Hz)
    static constexpr double warpScaleDb = 0.0;    // RETIRED step 5 (was 1.0) — see above
    static constexpr double warpExp = 1.80;       // rate falloff, refit 2026-07-30 (was 2.20)
    static constexpr double warpMaxDb = 1.0;      // cap — also the Nyquist-safety bound, see above
    static constexpr double warpPoleMaxFrac = 0.42; // prewarped pole ≤ this × the design rate
    static constexpr bool warpShelfEnabled = (warpScaleDb > 0.0);

    // ---- Stage-1 base-rate warp correction (drive-keyed high-shelf, v1.5 step 5, 2026-07-30) ----
    // Replaces the rate-keyed `warp*` above. With Stage 1 at the base rate (`preAtBaseRate`) its
    // bilinear top-octave droop no longer shrinks with the OS factor, so the correction has to be
    // keyed to what actually sets the droop — and that is the DRIVE knob, through the circuit:
    // Z_upper is `R_leg ∥ C2(100 pF)`, so C2's corner is `1/(2π·R_leg·C2)` = 75.8 kHz at drive 0.2
    // but 15.8 kHz at drive 1.0. It walks INTO the top octave as DRIVE rises, and bilinear-warping a
    // corner that close to Nyquist is the whole mechanism.
    //
    // KEYED ON R_leg, NOT ON THE KNOB — which is what makes it correct on RED for free. Every other
    // drive-keyed instrument here (`bassCut*`, `bassBoost*`, `driveMakeup`) is keyed to the raw knob
    // and fitted to the Yellow captures, which is why dsp.md carries a standing "deferred refinement"
    // note about Red being mis-keyed by 1/6 of a knob turn. This law reads Red's 17.7 k floor
    // directly, so ONE expression covers both channels; the fit was scored over both at once.
    //
    // THE LIFT HAS ZERO FITTED SHAPE — only `s1WarpLift0` scales it. The bilinear warp of a one-pole
    // at `fc`, read at the shelf's own pivot, is `10·log10((1+(f̃/fc)²)/(1+(pivot/fc)²))` with
    // `f̃ = (rate/π)·tan(π·pivot/rate)`: closed form, no free parameters. `s1WarpLift0` converts that
    // one-pole prediction to the composite `Av = 1 + Z_upper/Z_lower` (the deficit is C2's warp seen
    // THROUGH the gain stage, not C2's warp alone).
    //
    // ⚠ The TARGET is exact and the null CANNOT arbitrate it — the one EQ fit in this project with a
    // right answer. `analysis/v15_stage1_warp_probe.cpp fit` emits base-rate vs an 8x-of-base solve of
    // the SAME filter (residual warp ~1/64 of what is measured): no captures, no NAM model, no noise.
    // The 44-capture null renders at 4x and the whole effect is above 8 kHz, where the captures carry
    // ±18 dB of spread — so the null's only job here is to confirm nothing ELSE moved. Harness:
    // `analysis/v15_s1warp_fit.py`.
    //
    // ⚠ `s1WarpLift0 = 0.40` is DELIBERATELY BELOW the harness's weighted optimum (0.545), and the
    // reason is a metric-weighting trap. That weighting scores 4–11 kHz at 3–4×, where the raw deficit
    // is already ~0 — so it credits the 14–16 kHz repair almost nothing while charging full price for
    // presence-band over-correction, and its "best" answer costs +0.3…+0.40 dB at 6–10 kHz (series
    // pair) to buy the last 20 %. At 0.40 the 6–10 kHz residual stays ≤0.23 dB — below what any
    // instrument in this project resolves, so it cannot be double-corrected later by `hfTrim` (4.5 kHz)
    // or a P7 refit, which are the two things in this band. Series pair @48 kHz, 16 kHz, G2→G10:
    // deficit −1.0…−5.4 dB → residual −0.4…−2.7. Read the aggregate as a screen, the cells as the
    // verdict (FR_THD_AUDIT.md P10 step 3's rule).
    //
    // Accepted undershoot: above ~0.36·rate the warp DIVERGES toward Nyquist (+9.1 dB at 22.6 kHz on
    // a 48 kHz session) while a first-order shelf flattens out at its own lift. Out of the audio band
    // — not fitted, deliberately.
    static constexpr double s1WarpPivotHz = 16000.0; // high-shelf centre (Hz) — the pole guard's ceiling
    static constexpr double s1WarpLift0 = 0.40;      // scales the analytic one-pole warp prediction
    static constexpr double s1WarpC2 = 100.0e-12;    // Stage-1 Z_upper HF cap — the corner that warps
    static constexpr bool s1WarpEnabled = (s1WarpLift0 > 0.0);

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

    // ---- SW-1 output ceiling (v1.4 P9 step 3, 2026-07-29) — OD's missing saturation -------------
    // THE DEFECT (FR_THD_AUDIT.md P9): on 1 kHz level steps the real pedal's OVERDRIVE output
    // saturates and this model's never did — the pedal rises ~4.0–6.1 dB from −30 to −3 dBFS where
    // the plugin rose 7.3–8.5, i.e. 2.4–3.9 dB of missing compression at the hot end, at EVERY
    // drive. Confirmed on two independent signal families (synthetic level steps `comp` and
    // plucked-note decay `decay`) and absent in Boost/Distortion, whose ceilings (the op-amp rails
    // and the ±0.584 V shunt) are already modelled. OD's soft feedback clipper is the only path
    // whose output still tracks its input: above the diode clamp, pin7 ≈ Vf + i_in·R11, and that
    // R11/R9 = 0.68 residual slope keeps the output growing without bound.
    //
    // EMPIRICAL, and only after tracing was exhausted (schematic-checker re-traced the whole clip
    // branch; P9 step 2 found and fixed the one real topology bug — the parallel strings' Is —
    // which shifted the clamp 54 mV but left the SHAPE untouched). This is the standing "depart
    // from the schematic once tracing is exhausted" authorization being used.
    //
    // WHY A STATIC CEILING IS THE RIGHT INSTRUMENT — measured, not assumed
    // (`analysis/p9_ceiling_fit.py static`). A memoryless map on pin7 imposes ONE level→level
    // relation shared by every drive, pinned only up to a per-drive offset. Tested pairwise on the
    // absolute pin7 level, every drive's curve IS that one curve to within **0.31 dB rms over
    // G2–G7** — so a ceiling is admissible. It degrades (to 1.4 dB) only when G8/G10 are included,
    // and the residual grows monotonically with the DRIVE GAP (0.20 dB at Δ0.1 → 1.08 at Δ0.7),
    // which is the signature of a second, drive-keyed error — P6's `driveMakeup`, whose 6.0 dB cap
    // is already known to fall ~0.8 dB short of the measured G10 need. So the fit window is
    // **G2–G7** and the G8–G10 remainder is left to that separate defect, exactly as P7 scoped its
    // own fit to where its instrument was still valid.
    //
    // R11 WAS RULED OUT FIRST, on the primary sources. The ceiling landing at ~1.6 V — essentially
    // the diode clamp itself — is exactly what a much SMALLER series resistance would produce on
    // its own (a bare diode's voltage grows only logarithmically with current), and R11 = 1.5 k
    // scores nearly as well as the fitted ceiling with no re-levelling at all
    // (`p9_ceiling_fit.py r11`: comp rms 1.378 → 0.383, dG −0.04 dB). That would have been a
    // component-value fix rather than a new mechanism, so it was checked against the schematics
    // rather than adopted: BOTH show one shared 6k8 feeding the whole network (Theseus R8,
    // matsumin R11 — the matsumin BMP had never been opened before because it has no file
    // extension; it converts fine with `sips`). Two independent sources agree, so 6.8 k stands and
    // the empirical ceiling is what is left.
    //
    // Shape: identity below the knee, then a tanh approach to the ceiling — the same map as
    // `railSaturate`, so the ADAA antiderivative is the same form (its own state pair, since this
    // is a different signal). SYMMETRIC: the diode network is symmetric and OD's even series is
    // `injectEvenHarmonic`'s job, so putting asymmetry here would double-count it.
    //
    // The two constants were swept on the comp curves and the top candidates taken to the null.
    // The objective is BROAD along the ceiling ridge (0.305–0.315 dB rms for knee 0.2–0.5 at
    // ceiling 1.6), so the knee was NOT ground finer than that: 0.5 is the largest knee that keeps
    // the full G2–G7 benefit (a bigger knee gives up benefit for no reduction in the G10 cost —
    // see below — and a smaller one only re-levels OD harder for 0.03 dB). Ceiling 1.8 was tried
    // too: it halves the G10 damage but loses on both the fit window and the headline null.
    static constexpr double sw1CeilV = 1.6;     // ceiling on pin7 in OD (V); 0 disables
    static constexpr double sw1CeilKneeV = 0.5; // level (V) below which the map is exactly identity
    // v1.4 P10 step 3 — the ceiling gets a RESIDUAL SLOPE, so it never stops rising.
    //
    // WHY (`p9_ceiling_fit.py need`, the table that raised it): the extra compression the pedal
    // requires is nearly drive-INDEPENDENT — mean 3.11 dB, spread 1.71 across G2–G10 — while a
    // ceiling *voltage* must supply more at higher drive, because pin7 is higher there, and does:
    // mean 3.20 dB, spread 3.06. The MEAN was right all along; the distribution across the knob was
    // not. That is why the G10 penalty was identical at every knee (P9 saw the symptom and read it
    // as untunable) and why raising the ceiling could only trade one end of the axis for the other.
    //
    // A pure fixed-ratio power law was built and scored first and is NOT what this is: it beats the
    // tanh over G2–G10 (0.946 → 0.714 dB rms) but gives up the validated window (0.315 → 0.486) and
    // re-levels Overdrive ~1 dB, because a ratio compresses the quiet end too. This form CONTAINS
    // the shipped map at `sw1CeilSlope = 0`, so the fit window can be held rather than traded.
    // ⚠️ It is SHIPPED AT 0, i.e. the P9 ceiling unchanged, and the reason is the target and not the
    // instrument: `p9_ceiling_fit.py floor` shows the pedal's own G10 Overdrive comp curve varies
    // 1.19 dB across the TONE knob, a quantity the circuit makes tone-INDEPENDENT (the tone stack is
    // post-clip and linear; the plugin's spread is 0.00). That is about half the G10 residual a slope
    // would be fitted to. Every slope that helps G10 also costs the G2–G7 window and re-levels the
    // mode, so the trade would be bought inside the target's own noise. Kept because it is three
    // lines, costs nothing at 0 (the `if constexpr` below compiles to exactly the old expression),
    // and is the shape to reach for FIRST if the captures are ever re-taken at more tone settings.
    static constexpr double sw1CeilSlope = 0.0; // asymptotic slope above the knee; 0 = P9's ceiling
    static constexpr bool sw1CeilSloped = (sw1CeilSlope > 0.0);
    static constexpr bool sw1CeilEnabled = (sw1CeilV > 0.0 && sw1CeilKneeV < sw1CeilV);
    // RESULT (all 44 captures; Boost + Distortion byte-identical by construction, verified):
    // comp-curve rms error over the fit window **1.33 → 0.40 dB**, bias +0.87 → −0.25, worst cell
    // 3.94 → 1.00; every drive G2–G8 improves. Confirmed on the instrument it was NOT fitted to —
    // `decay_1k`'s attack-window error goes G2 +1.38 → −0.20, G6 +3.12 → +0.49. Headline null
    // median −22.9 → −23.1 dB, G5 T5 OD −23.2 → −24.4, worst G2–G7 capture −19.6 → −21.9;
    // 13 captures deeper (to −1.50 dB), 30 byte-identical, 1 shallower. All nine gates PASS.
    //
    // ⚠️ THE COST, stated plainly: **G10 over-corrects.** Its comp error flips sign (+2.25 →
    // −2.83 dB at −3 dBFS) and its driven-sweep nulls lose 1.0–2.0 dB, because the pedal's OWN
    // Overdrive compresses LESS at G10 than at G7 while a level-keyed ceiling necessarily bites
    // MORE there. That is not fixable with this instrument and was predicted before fitting, by
    // the admissibility test — it is the same G8–G10 drive-keyed residual, now visible from the
    // other side. The headline still improves because G10's deepest segment is its clean sweep.
    // IMD also moves OD-only and both ways (SMPTE worst +1.64 dB at G10 T8, CCIF −1.07 to +2.17).

    explicit MonarchChannel (bool hiGain = false) : stage1 (hiGain), hiGainStage1 (hiGain) {}

    // Stage 1 and the clip span run at the OVERSAMPLED rate, so their near-Nyquist bilinear warp
    // shrinks with the OS factor. Both prepareLinear and prepareClip are re-called at the OS rate on
    // factor change. `rate` here is that effective (oversampled) rate; for standalone/1x it == base.
    //
    // `postRate` is the rate the Tone/Volume span runs at, which is NOT necessarily `rate` — see
    // `postAtBaseRate`. `preRate` is the same for Stage 1 — see `preAtBaseRate`. BOTH default to
    // `rate` so a caller that wants the whole channel at one rate (the standalone probes in
    // analysis/, the per-stage tests, 1x) needs no change and gets the pre-v1.5 behaviour exactly.
    //
    // NOTE `rate` remains the SHELF design rate (`shBaseRate`): `driveShelf` — bass cut/boost, warp,
    // HF trim — lives downstream of IC_A's rail-sat and so stays in the oversampled span even when
    // Stage 1 does not. Only `stage1` itself moves with `preRate`.
    void prepareLinear (double rate, double postRate = 0.0, double preRate = 0.0)
    {
        if (postRate <= 0.0)
            postRate = rate;
        if (preRate <= 0.0)
            preRate = rate;
        stage1.prepare (preRate);
        tone.prepare (postRate);
        volume.prepare (postRate);
        shBaseRate = rate;
        s1Rate = preRate;
        // Stage-1 warp shelf: rate AND drive keyed, so it is designed here and re-designed per block
        // in setDrive. 0.5 matches Stage1's own ctor drive, so a caller that never calls setDrive
        // (the per-stage tests, the analysis/ probes) still gets a coherent filter.
        updateS1Warp (0.5);
        // Unity pass-through until setDrive() runs. Keyed off bassCutOffDrive rather than a literal
        // 0.5: P7 moved the bell's zero to 0.55, which silently made the old `0.5` a −0.55 dB bell.
        // Harmless either way (setDrive() runs every block before processing, and the filter state
        // is zeroed just below), but a hardcoded number here goes stale the moment a law is retuned.
        updateDriveShelf (bassCutOffDrive);
        // Bilinear-warp top-octave correction: rate-only, tracked the measured 1x/2x/4x→8x deficit so
        // the live (2x) and render (4x/8x) paths shared the same top octave. RETIRED by step 5, which
        // removed that rate-dependence at source; the replacement is drive-keyed and lives in
        // updateS1Warp. See the warp*/s1Warp* consts.
        if constexpr (warpShelfEnabled)
        {
            const double warpDb = std::min (warpMaxDb, warpScaleDb * std::pow (48000.0 / shBaseRate, warpExp));
            // Nyquist guard on the PIVOT (v1.5 step 3). shelfCoeffs prewarps the pole to pivot·√ghi;
            // at 17 kHz that clears Nyquist comfortably at 44.1/48 kHz but NOT at a 32 kHz session
            // running 1x, where tan() would cross π/2, flip sign and hand back an unstable filter.
            // Slide the pivot down with the rate instead.
            const double warpGhi = std::pow (10.0, warpDb / 20.0);
            const double warpPivot = std::min (warpPivotHz, warpPoleMaxFrac * shBaseRate / std::sqrt (warpGhi));
            shelfCoeffs (1.0, warpGhi, warpPivot, wsB0, wsB1, wsA1);
            // DC-normalize: a prewarped first-order high-shelf with a pivot up near Nyquist loses unity
            // DC gain and droops the whole spectrum. H(z=1) = (b0+b1)/(1+a1).
            const double wsDc = (wsB0 + wsB1) / (1.0 + wsA1);
            wsB0 /= wsDc;
            wsB1 /= wsDc;
        }
        // LF extension: fixed, drive- and mode-independent low-shelf (glo=lift, ghi=1). Rate-only,
        // so it belongs here rather than in updateDriveShelf. See the lfExt* constants.
        shelfCoeffs (std::pow (10.0, lfExtDb / 20.0), 1.0, lfExtPivotHz, leB0, leB1, leA1);
        // OD clip-gated low-shelf: fixed coeffs at the OS rate; a low-shelf sets ghi=1, glo=lift.
        shelfCoeffs (std::pow (10.0, odShelfMaxDb / 20.0), 1.0, odShelfPivotHz, olB0, olB1, olA1);
        // Fixed HF-trim high-shelf (drive-independent): eases the slightly-hot top end (glo=1, ghi=cut).
        shelfCoeffs (1.0, std::pow (10.0, hfTrimDb / 20.0), hfTrimPivotHz, htB0, htB1, htA1);
        hsX1 = hsY1 = lsX1 = lsY1 = wsX1 = wsY1 = swX1 = swY1 = olX1 = olY1 = htX1 = htY1 = bcX1 = bcX2 = bcY1 = bcY2 =
            leX1 = leY1 = 0.0;
    }

    void prepareClip (double clipRate)
    {
        stage2.prepare (clipRate);
        sw1.prepare (clipRate);
        sw2.prepare (clipRate);
        asymCoeff = std::exp (-1.0 / (asymTauSeconds * clipRate));      // fast: clip-depth gate
        meanCoeff = std::exp (-1.0 / (asymMeanTauSeconds * clipRate));  // slow: DC removal only
        lpLowCoeff = std::exp (-2.0 * M_PI * asymLowFc / clipRate);     // low-band low-pass corner
        hpMidCoeff = std::exp (-2.0 * M_PI * asymMidFc / clipRate);     // mid-band high-pass corner
        railDcCoeff = std::exp (-1.0 / (railDcTauSeconds * clipRate));  // asymmetric-rail DC removal
        clipEnv = 0.0;
        meanSq = 0.0;
        xLp = 0.0;
        meanLow = 0.0;
        gLp = 0.0;
        lowEnv = 0.0;
        gHpX1 = 0.0;
        gHpY1 = 0.0;
        railXprev = 0.0; // F(0)=0 for any rails
        railFprev = 0.0;
        s1RailXprev = 0.0;
        s1RailFprev = 0.0;
        sw1CeilXprev = 0.0;
        sw1CeilFprev = 0.0;
        railMean = 0.0;
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
        s1RailXprev = 0.0;
        s1RailFprev = 0.0;
        sw1CeilXprev = 0.0;
        sw1CeilFprev = 0.0;
        railMean = 0.0;
        hsX1 = hsY1 = lsX1 = lsY1 = wsX1 = wsY1 = swX1 = swY1 = olX1 = olY1 = htX1 = htY1 = bcX1 = bcX2 = bcY1 = bcY2 =
            leX1 = leY1 = 0.0;
    }

    // ---- Parameter setters (call per block; tapers applied inside each stage) ----
    void setDrive (double d)
    {
        stage1.setDrive (d);
        updateDriveShelf (d); // drive-dependent Stage-1 voicing correction (see shelf* consts)
        updateS1Warp (d);     // Stage-1 base-rate warp correction, keyed on R_leg (see s1Warp* consts)
        const double makeupDb = std::min (driveMakeupMaxDb,
                                          std::max (0.0, driveMakeupSlopeDb * (d - driveMakeupOnset)));
        driveMakeup = std::pow (10.0, makeupDb / 20.0); // see driveMakeup* consts
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
        updateRails();
    }

    /** Clipping mode 0..2 (Boost/Overdrive/Distortion → SW-1/SW-2 on/off). 3-way per channel
        (no "Both" — dropped 2026-06-19 for the 3-position hardware toggle). processClip still
        handles any SW-1/SW-2 combination, so re-adding a stacked mode later is a 1-line change. */
    void setClippingMode (int mode)
    {
        sw1On = (mode == 1);
        sw2On = (mode == 2);
        updateRails(); // SW-2 loads the op-amp output → scales the rail asymmetry (see railAsymLoadedScale)
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

    /** ADAA on the two soft-ceiling maps (`railSaturate` ×2 op-amps, `sw1Ceil`). Production is ON at
        every rate; this exists because ADAA is a SUBSTITUTE for oversampling and the plugin currently
        pays for both — measured at **25 ns/sample in Boost, 37 in OD, 25 in Dist, i.e. 18–22 % of the
        channel** (analysis/perf_split_probe.cpp), which makes it the single largest lever in the DSP.
        Whether the 4x/8x decimation filter already removes what ADAA removes is a fidelity question,
        so OSFidelity section (c) A/Bs it per OS factor rather than a hunch deciding.

        Toggling back ON re-bases each antiderivative from its stored x₋₁ — same reasoning as
        updateRails(), since a stale F(x₋₁) would corrupt exactly one difference quotient. */
    void setAdaaEnabled (bool on) noexcept
    {
        if (on && ! adaaEnabled)
        {
            railFprev = railAntideriv (railXprev);
            s1RailFprev = railAntideriv (s1RailXprev);
            sw1CeilFprev = sw1CeilAntideriv (sw1CeilXprev);
        }
        adaaEnabled = on;
    }

    // Base-rate front: input network + Stage 1 → V(NodeG), then the drive-dependent voicing
    // correction (high-shelf; unity pass-through once drive ≳ 0.47, see shelf* consts).
    // `driveMakeup` is flat, so it is equivalent anywhere between Stage 1 and the clipper; it sits
    // here (before driveShelf, i.e. at NodeG) because that is where the real pot's second action
    // feeds Stage 2. Stage 1 is linear, so this cannot change Stage 1's own voicing — only the
    // level presented to the clip stages, which is exactly what the measurement says is short.
    // Split at the Stage-1 boundary so Stage 1 can run at the base rate (`preAtBaseRate`) while the
    // rail-sat and the shelves stay oversampled. `processPre` composes the two, so the single-rate
    // path (1x, the per-stage tests, the analysis/ probes, `processSample`) is bit-identical to
    // before this split — the only caller that separates them is PluginProcessor's OS path.
    inline double processStage1 (double x) noexcept
    {
        const double g = stage1.processSample (x);
        if constexpr (! s1WarpEnabled)
            return g;
        // Corrects Stage 1's OWN bilinear top-octave droop, so it belongs here, at Stage 1's rate and
        // before the nonlinearity — the clipper should see the corrected spectrum. See s1Warp* consts.
        const double y = swB0 * g + swB1 * swX1 - swA1 * swY1;
        swX1 = g;
        swY1 = y;
        return y;
    }

    inline double processPreOs (double nodeG) noexcept
    {
        // IC_A has the SAME output ceiling as IC_B and the model never applied it (v1.4 P9).
        // It sits here, BEFORE driveMakeup, because that is the physical order: NodeG is IC_A's
        // output pin, and the DRIVE pot's second action (which driveMakeup stands in for) is a
        // divider hung off that pin — it can only attenuate what IC_A already produced.
        // It is also why Stage 1 is the ONLY thing that can leave the OS span here: this is a
        // nonlinearity, and everything after it is downstream of the nonlinearity.
        if (stage1RailsEnabled)
            nodeG = railSaturateADAA (nodeG, s1RailXprev, s1RailFprev);
        return driveShelf (driveMakeup * nodeG);
    }

    inline double processPre (double x) noexcept { return processPreOs (processStage1 (x)); }

    // Oversampled nonlinear span: Stage2 (or SW1 soft clip) → op-amp rail-sat → SW2 (or pass)
    // → V(node_HC). This is the ONLY part that should run at the oversampled rate.
    inline double processClip (double nodeG) noexcept
    {
        // The SW-1 ceiling lives INSIDE the sw1On branch, so Boost and Distortion are byte-identical
        // by construction (same discipline as odLowShelf). It sits before the rail-sat because in OD
        // it is the dominant ceiling — the feedback clipper holds pin7 well below the rails, which is
        // exactly why the rails could not supply this (see the sw1Ceil* constants).
        double pin7;
        if (sw1On)
        {
            pin7 = sw1.processSample (nodeG);
            if constexpr (sw1CeilEnabled)
                pin7 = sw1CeilADAA (pin7);
        }
        else
            pin7 = stage2.processSample (nodeG);
        pin7 = railSaturateADAA (pin7); // op-amp output ceiling, antialiased (Boost always; Dist via Stage2)
        pin7 = railDcBlock (pin7);      // strip rectified DC before it smears (see railDcTauSeconds)
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
        const double gate = sw1On ? fastTanh (odGateScale * clipEnv) : 0.0;
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
    // Padé [7/6] rational tanh, for the empirical even-harmonic terms only (injectEvenHarmonic /
    // odLowShelf) — NOT railSaturate/sw1Ceil, whose ADAA antiderivative is fitted to std::tanh's
    // exact log(cosh) form. Matches std::tanh to <1.2e-5 abs error over [0,4] and <1e-4 near the
    // clamp (v1.5 CPU pass; those terms are fitted to ~1% / −40 dBc precision, so this is far
    // tighter than needed). The raw polynomial diverges unbounded past ~5 (denominator degree <
    // numerator degree), so clamp at 4.97 where it still agrees with std::tanh to 9e-5 — several of
    // these calls saturate hard (e.g. odGateScale·clipEnv can exceed 40).
    static inline double fastTanh (double x) noexcept
    {
        if (x >= 4.97) return 1.0;
        if (x <= -4.97) return -1.0;
        const double x2 = x * x;
        const double num = x * (135135.0 + x2 * (17325.0 + x2 * (378.0 + x2)));
        const double den = 135135.0 + x2 * (62370.0 + x2 * (3150.0 + x2 * 28.0));
        return num / den;
    }

    // Soft op-amp rail saturation: linear below the knee, gentle tanh knee approaching the ceiling.
    // Below the knee the signal passes UNCHANGED, so it never colours the feedback soft-clip's
    // sub-3 V output at normal drive. It clamps only swings the real op-amp would also clamp:
    // Boost (no diodes) and Distortion's linear-Stage2 ×−22 path always, OD/Both at extreme drive.
    // The two sides use DIFFERENT ceilings (railAsymV) — that asymmetry is what makes the even
    // harmonic series. Below min(kneePos, kneeNeg) the map is still exactly the identity, so the
    // tone-safety argument for OD is unchanged.
    inline double railSaturate (double v) const noexcept
    {
        const double a = std::abs (v);
        const double knee = (v >= 0.0) ? railKneePos : railKneeNeg;
        const double rail = (v >= 0.0) ? railVPos : railVNeg;
        if (a <= knee)
            return v;
        const double clamped = knee + (rail - knee) * std::tanh ((a - knee) / (rail - knee));
        return std::copysign (clamped, v);
    }

    // Numerically-stable log(cosh(z)) for the rail-sat antiderivative (avoids cosh overflow).
    static inline double logCosh (double z) noexcept
    {
        const double az = std::abs (z);
        return az + std::log1p (std::exp (-2.0 * az)) - 0.6931471805599453; // − ln 2
    }

    // Antiderivative F of railSaturate (F' = railSaturate, F(0)=0). Below the knee f(v)=v →
    // F=v²/2; above, f = knee + w·tanh(u/w) with u=|v|−knee, w=rail−knee → F = knee²/2 + knee·u +
    // w²·logCosh(u/w). Used for first-order ADAA.
    //
    // With unequal ceilings railSaturate is no longer odd, so F is no longer even — but it is
    // still the true antiderivative on each side, because each side's ∫ from 0 uses that side's
    // parameters: for v < 0, F(v) = ∫₀ᵛ f = +G_neg(|v|) (two sign flips — the negated integrand
    // and the reversed limits). So the SAME |v| expression holds, just with the per-side knee and
    // ceiling, and F stays continuous with F(0)=0. That keeps the ADAA difference quotient exact
    // even for a sample pair that straddles zero.
    inline double railAntideriv (double v) const noexcept
    {
        const double a = std::abs (v);
        const double knee = (v >= 0.0) ? railKneePos : railKneeNeg;
        const double rail = (v >= 0.0) ? railVPos : railVNeg;
        if (a <= knee)
            return 0.5 * a * a;
        const double w = rail - knee;
        const double u = a - knee;
        return 0.5 * knee * knee + knee * u + w * w * logCosh (u / w);
    }

    // First-order antiderivative antialiasing of the rail saturation (DAFx-2020). Replaces the
    // pointwise f(x) with the averaged (F(x)−F(x₋₁))/(x−x₋₁), which suppresses the aliasing the
    // hard-ish knee would otherwise fold back — most audible in Boost (the rails are the ONLY
    // nonlinearity there). Falls back to the midpoint value when x≈x₋₁ (ill-conditioned divide).
    // This is in ADDITION to oversampling: the clip span (incl. this) already runs oversampled.
    inline double railSaturateADAA (double x) noexcept
    {
        return railSaturateADAA (x, railXprev, railFprev);
    }

    // Same map, caller-supplied ADAA state — IC_A and IC_B are two op-amps in the same package
    // with the same ceilings but their own signals, so they need their own (x₋₁, F(x₋₁)) pair.
    inline double railSaturateADAA (double x, double& xPrev, double& fPrev) const noexcept
    {
        if (! adaaEnabled) // see setAdaaEnabled — fPrev is re-based on re-enable, not maintained here
        {
            xPrev = x;
            return railSaturate (x);
        }
        // Identity region: the map is exactly y=x on [−railKneeNeg, +railKneePos], so if BOTH ends
        // of the interval are inside it there is no nonlinearity to antialias and the difference
        // quotient would be the midpoint average (x+x₋₁)/2 — see adaaIdentityEarlyOut.
        if constexpr (adaaIdentityEarlyOut)
        {
            if (x <= railKneePos && x >= -railKneeNeg && xPrev <= railKneePos && xPrev >= -railKneeNeg)
            {
                xPrev = x;
                fPrev = 0.5 * x * x; // == railAntideriv(x) below the knee; keeps the next sample exact
                return x;
            }
        }
        const double Fx = railAntideriv (x);
        const double dx = x - xPrev;
        double y;
        if (std::abs (dx) < 1.0e-6)
            y = railSaturate (0.5 * (x + xPrev));
        else
            y = (Fx - fPrev) / dx;
        xPrev = x;
        fPrev = Fx;
        return y;
    }

    // ---- SW-1 output ceiling: same map/antiderivative form as railSaturate, own parameters -----
    // See the sw1Ceil* constants. Symmetric, so this map IS odd and its antiderivative even —
    // simpler than the rails' asymmetric pair.
    // Above the knee: y = K + ω·tanh(u/ω) + m·u, with u = |x| − K, m = sw1CeilSlope and
    // ω = (1 − m)·(ceiling − K). The (1 − m) factor is what keeps the slope at the knee exactly 1
    // (tanh contributes 1 − m there, the linear term m), so the map stays C1 for every m — and at
    // m = 0 both expressions reduce to P9's ceiling identically, which is the point of this form.
    static constexpr double sw1CeilOmega()
    {
        return (1.0 - sw1CeilSlope) * (sw1CeilV - sw1CeilKneeV);
    }

    inline double sw1Ceil (double v) const noexcept
    {
        const double a = std::abs (v);
        if (a <= sw1CeilKneeV)
            return v;
        const double u = a - sw1CeilKneeV;
        constexpr double om = sw1CeilOmega(); // == sw1CeilV - sw1CeilKneeV when the slope is 0
        if constexpr (sw1CeilSloped)
            return std::copysign (sw1CeilKneeV + om * std::tanh (u / om) + sw1CeilSlope * u, v);
        return std::copysign (sw1CeilKneeV + om * std::tanh (u / om), v);
    }

    inline double sw1CeilAntideriv (double v) const noexcept
    {
        const double a = std::abs (v);
        if (a <= sw1CeilKneeV)
            return 0.5 * a * a;
        const double u = a - sw1CeilKneeV;
        constexpr double om = sw1CeilOmega();
        const double base = 0.5 * sw1CeilKneeV * sw1CeilKneeV + sw1CeilKneeV * u
                            + om * om * logCosh (u / om);
        if constexpr (sw1CeilSloped)
            return base + 0.5 * sw1CeilSlope * u * u;
        return base;
    }

    // First-order ADAA on the ceiling. It is a NEW nonlinearity in the OD path, so it aliases like
    // any other; the clip span already runs oversampled and this is in addition, matching how the
    // rail knee is treated.
    inline double sw1CeilADAA (double x) noexcept
    {
        if (! adaaEnabled) // see setAdaaEnabled
        {
            sw1CeilXprev = x;
            return sw1Ceil (x);
        }
        // Identity region — see adaaIdentityEarlyOut. In OD this is very nearly the whole signal:
        // the feedback clipper holds |pin7| ≤ 1.64 V, so above the 0.5 V knee is the exception.
        if constexpr (adaaIdentityEarlyOut)
        {
            if (std::abs (x) <= sw1CeilKneeV && std::abs (sw1CeilXprev) <= sw1CeilKneeV)
            {
                sw1CeilXprev = x;
                sw1CeilFprev = 0.5 * x * x;
                return x;
            }
        }
        const double Fx = sw1CeilAntideriv (x);
        const double dx = x - sw1CeilXprev;
        const double y = (std::abs (dx) < 1.0e-6) ? sw1Ceil (0.5 * (x + sw1CeilXprev))
                                                  : (Fx - sw1CeilFprev) / dx;
        sw1CeilXprev = x;
        sw1CeilFprev = Fx;
        return y;
    }

    // Recompute the two ceilings and their knees from the current mean rail and the current load.
    // Called whenever either input changes: setSupplyVoltage (moves the mean) or setClippingMode
    // (SW-2 loads the output → railAsymLoadedScale). Only the MEAN moves with the supply — the
    // asymmetry comes from fixed drops (the bias offset and the output stage's Voh/Vol
    // difference), which do not scale with it.
    void updateRails() noexcept
    {
        const double asym = railAsymV * (sw2On ? railAsymLoadedScale : 1.0);
        railVPos = railV + asym;
        railVNeg = railV - asym;
        railKneePos = railVPos - railKneeMargin;
        railKneeNeg = railVNeg - railKneeMargin;
        railFprev = railAntideriv (railXprev);     // keep the ADAA antiderivative consistent with new rails
        s1RailFprev = railAntideriv (s1RailXprev); // ditto for IC_A's own ADAA state
    }

    // Remove the DC an asymmetric rail-sat rectifies out of the signal (see railDcTauSeconds).
    // A one-pole running mean subtracted from the sample — i.e. a 3.2 Hz high-pass, well below
    // anything the circuit passes, so it takes the step and leaves the harmonics.
    inline double railDcBlock (double v) noexcept
    {
        railMean = railDcCoeff * railMean + (1.0 - railDcCoeff) * v;
        return v - railMean;
    }

    // First-order shelf coeffs (bilinear, prewarped — mirrors TiltShelf). `glo`/`ghi` are the
    // LF/HF linear-gain asymptotes, `pivot` the geometric centre. ghi=glo → exact unity passthrough.
    // Writes b0/b1/a1. A high-shelf sets glo=1; a low-shelf sets ghi=1.
    // `rate` defaults to shBaseRate (the oversampled span's rate, where all but one of these live).
    // The Stage-1 warp shelf must pass the BASE rate — it runs inside Stage 1's own span.
    void shelfCoeffs (double glo, double ghi, double pivot, double& b0, double& b1, double& a1,
                      double rate = 0.0) const noexcept
    {
        if (rate <= 0.0)
            rate = shBaseRate;
        const double rt = std::sqrt (ghi / glo);
        const double fz = pivot / rt; // zero
        const double fp = pivot * rt; // pole
        const double K = 2.0 * rate;
        const double wz = K * std::tan (M_PI * fz / rate);
        const double wp = K * std::tan (M_PI * fp / rate);
        const double a0 = K + wp;
        a1 = (wp - K) / a0;
        b0 = ghi * (K + wz) / a0;
        b1 = ghi * (wz - K) / a0;
    }

    // RBJ peaking (bell) biquad — standard digital-domain design (Audio EQ Cookbook). Direct-Form-I.
    void peakCoeffs (double centreHz, double gainDb, double Q,
                     double& b0, double& b1, double& b2, double& a1, double& a2) const noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * M_PI * centreHz / shBaseRate;
        const double alpha = std::sin (w0) / (2.0 * Q);
        const double a0 = 1.0 + alpha / A;
        b0 = (1.0 + alpha * A) / a0;
        b1 = (-2.0 * std::cos (w0)) / a0;
        b2 = (1.0 - alpha * A) / a0;
        a1 = (-2.0 * std::cos (w0)) / a0;
        a2 = (1.0 - alpha / A) / a0;
    }

    // Drive-dependent capture-match correction (see shelf*/bass* consts): a treble HIGH-SHELF that
    // fades OUT with drive (retired by P7), a bass BOOST low-shelf that HUMPS with drive (peaking at
    // bassPeakDrive — P8), and a bass CUT bell that fades OUT with drive (low-drive low-mid excess).
    // All on Stage 1's output.
    // Stage-1 base-rate warp correction (see s1Warp* consts). Designed at `s1Rate`, NOT shBaseRate —
    // it corrects Stage 1's own bilinear warp and so has to live at Stage 1's rate. Drive-keyed
    // through the physical R_leg, so it is called from setDrive, and rate-keyed too, so prepareLinear
    // must call it after setting s1Rate.
    void updateS1Warp (double drive01) noexcept
    {
        if constexpr (! s1WarpEnabled)
            return;
        const double rleg = stage1.floorResistance() + std::min (1.0, std::max (0.0, drive01)) * Stage1::DRIVE_max;
        const double fc = 1.0 / (2.0 * M_PI * rleg * s1WarpC2);          // C2's corner — the drive axis
        // ⚠ TWO clamps, and the FIRST one is not optional. The lift law itself contains
        // tan(π·pivot/rate), so the pivot has to be inside Nyquist BEFORE the lift is computed — at a
        // 32 kHz session 16 kHz IS Nyquist and that tan diverges, taking the lift, ghi and the whole
        // filter with it (measured: Stage 1's output reaches 7e6). The retired warp* clamped only
        // after, which was safe there ONLY because its lift came from a rate power law that never
        // touches tan. Copying the clamp's placement instead of its reasoning is what reproduced this.
        const double pivot0 = std::min (s1WarpPivotHz, warpPoleMaxFrac * s1Rate);
        const double ftil = (s1Rate / M_PI) * std::tan (M_PI * pivot0 / s1Rate); // prewarped pivot
        const double liftDb = std::max (0.0, s1WarpLift0
                                                 * 10.0 * std::log10 ((1.0 + (ftil / fc) * (ftil / fc))
                                                                      / (1.0 + (pivot0 / fc) * (pivot0 / fc))));
        const double ghi = std::pow (10.0, liftDb / 20.0);
        // Second clamp — the pole guard the retired warp* also had, and it BINDS HARDER here: this
        // shelf is live at every OS factor and its lift rises with drive, so the prewarped pole
        // pivot·√ghi moves with the knob. Both clamps are inert at 44.1/48/88.2/96 kHz (16 kHz needs
        // ≤17.6 kHz of headroom against 18.5 available at 44.1 k), so nothing that was fitted moves.
        const double pivot = std::min (pivot0, warpPoleMaxFrac * s1Rate / std::sqrt (ghi));
        shelfCoeffs (1.0, ghi, pivot, swB0, swB1, swA1, s1Rate);
        // DC-normalize (H(z=1) = (b0+b1)/(1+a1)), exactly as the retired warp* did: a prewarped
        // first-order high-shelf with a pivot up near Nyquist loses unity DC gain and droops the whole
        // spectrum, which is a broadband level shift rather than the top-octave fix intended.
        const double dc = (swB0 + swB1) / (1.0 + swA1);
        swB0 /= dc;
        swB1 /= dc;
    }

    void updateDriveShelf (double drive01) noexcept
    {
        const double trebleDb = std::max (0.0, shelfMaxDb - shelfSlopeDb * drive01);          // HF lift
        const double bassFall = bassBoostSlopeDb * std::max (0.0, bassPeakDrive - drive01)    // below the peak
                              + bassBoostFallDb * std::max (0.0, drive01 - bassPeakDrive);    // above it
        const double bassBoostDb = std::max (0.0, bassBoostMaxDb - bassFall);
        const double bassCutDb = -std::min (bassCutMaxDb, std::max (0.0, bassCutSlopeDb * (bassCutOffDrive - drive01)));
        if constexpr (trebleShelfEnabled)                                                     // retired by P7
            shelfCoeffs (1.0, std::pow (10.0, trebleDb / 20.0), shelfPivotHz, hsB0, hsB1, hsA1);
        shelfCoeffs (std::pow (10.0, bassBoostDb / 20.0), 1.0, bassPivotHz, lsB0, lsB1, lsA1); // bass boost low-shelf
        peakCoeffs (bassCutPivotHz, bassCutDb, bassCutQ, bcB0, bcB1, bcB2, bcA1, bcA2);        // bass cut bell
    }

    inline double driveShelf (double x) noexcept
    {
        double t = x;
        if constexpr (trebleShelfEnabled) // retired by P7 — the bass-cut bell carries this correction
        {
            t = hsB0 * x + hsB1 * hsX1 - hsA1 * hsY1; // treble high-shelf
            hsX1 = x;
            hsY1 = t;
        }
        const double b = lsB0 * t + lsB1 * lsX1 - lsA1 * lsY1; // bass boost low-shelf
        lsX1 = t;
        lsY1 = b;
        const double c = bcB0 * b + bcB1 * bcX1 + bcB2 * bcX2 - bcA1 * bcY1 - bcA2 * bcY2; // bass cut bell
        bcX2 = bcX1; bcX1 = b;
        bcY2 = bcY1; bcY1 = c;
        double e = c;
        if constexpr (lfExtEnabled) // retired — a min-phase LF boost worsens the null (see lfExt*)
        {
            e = leB0 * c + leB1 * leX1 - leA1 * leY1;
            leX1 = c;
            leY1 = e;
        }
        double w = e;
        if constexpr (warpShelfEnabled) // RETIRED step 5 — Stage 1 at base rate killed it at source
        {
            w = wsB0 * e + wsB1 * wsX1 - wsA1 * wsY1;
            wsX1 = e;
            wsY1 = w;
        }
        const double y = htB0 * w + htB1 * htX1 - htA1 * htY1; // fixed HF trim (ease the top toward captures)
        htX1 = w;
        htY1 = y;
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
        //
        // That wash-out only works where nodeG is BIG. Stage 1's high-shelf keeps it small below a
        // few hundred Hz, so down there the tanh never squares up and this path grew as nodeG²
        // instead — the low band's H2 overshoot (see asymMidFc). High-pass the source so the two
        // paths split the spectrum; the low band's low-pass below is the other half of the split.
        const double gHp = hpMidCoeff * (gHpY1 + nodeG - gHpX1);
        gHpX1 = nodeG;
        gHpY1 = gHp;

        // In BOOST this band's coefficient is exactly zero — `asymBoost` was retired to 0 by P2 (the
        // rails supply Boost's mid/high evens) while `asymLowBoost` = −0.017 keeps the LOW path live.
        // So `k` is 0.0 and the injection term is dead weight. `gate` feeds nothing but `k`, and has
        // no state, so hoisting the coefficient out and skipping the gate tanh + the multiply-add is
        // **byte-identical by construction** — verified over every mode × drive × tone × channel AND
        // across mid-stream mode changes. Worth −6 ns/sample of a 114 ns Boost channel at 8x
        // (analysis/perf_split_probe.cpp); OD/Dist take the branch and are unchanged.
        //
        // ⚠️ `soft` and `meanSq` deliberately STAY OUTSIDE the branch, and that is the whole reason
        // this saves 6 ns and not 12. `meanSq` is a 50 ms running mean of soft², read only when the
        // coefficient is live — so in Boost its only job is to be WARM if the mode later switches.
        // Skipping it is not free: the first Boost→OD sample diverges (measured — the dump probe's
        // only differing byte), and the term it lands in is O(0.03 V), i.e. ~−31 dB, swelling over
        // 50 ms rather than clicking. The remaining 6 ns costs one tanh of mode-switch state
        // coherence and is a judged change, not a free one. Same reasoning keeps the gHp high-pass
        // above unconditional (it is a signal-history filter — cf. odLowShelf's always-running shelf).
        const double kMid = sw1On ? asymOD : (sw2On ? asymDist : asymBoost);
        const double soft = fastTanh (asymDriveScale * gHp);
        meanSq = meanCoeff * meanSq + (1.0 - meanCoeff) * soft * soft;

        double out = x;
        if (kMid != 0.0)
        {
            const double k = kMid * fastTanh (4.0 * clipEnv);
            out = x + k * (soft * soft - meanSq); // mid/high band — DC-free 2f injection
        }

        // Low-frequency band: source the H2 from a low-pass of the clip output x (clamped only when
        // clipping → self-gating, clean stays clean). Catches low notes that clip but whose nodeG is
        // shelved down. At mid/high, xLp → small (x is above the corner) → no double injection.
        // The low-pass strips the clipped waveform's own harmonics, so xLp is nearly a SINE and
        // its square is pure H2 — no H4/H6 at all. Run it through the same tanh knee the mid path
        // uses (normalised by the mode's clamp so one scale serves both modes) to give this band
        // the pedal's even-series SHAPE as well as its level (see asymLowDriveScale).
        xLp = lpLowCoeff * xLp + (1.0 - lpLowCoeff) * x;
        const double clampRef = sw1On ? asymClampOD : (sw2On ? asymClampDist : railV);
        const double softLow = fastTanh (asymLowDriveScale * xLp / clampRef);
        meanLow = meanCoeff * meanLow + (1.0 - meanCoeff) * softLow * softLow;

        // ...and wash it out with drive. This path cannot do that on its own (x is CLAMPED, so its
        // low-passed square stops growing) and cannot use clipEnv (whose threshold is never met
        // down here), so it carries its OWN depth envelope — see asymLowWash / asymLowThresh.
        gLp = lpLowCoeff * gLp + (1.0 - lpLowCoeff) * nodeG;
        lowEnv = meanCoeff * lowEnv + (1.0 - meanCoeff) * std::max (0.0, std::abs (gLp) - asymLowThresh);
        const double w = asymLowWash * lowEnv;
        const double wash = 1.0 / (1.0 + w * w);
        const double kLow = (sw1On ? asymLowOD : (sw2On ? asymLowDist : asymLowBoost)) * wash;
        out += kLow * (softLow * softLow - meanLow);
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
    bool adaaEnabled { true }; // production default; see setAdaaEnabled

    double railV { railV9V };                       // MEAN op-amp ceiling (V); 9 V default = 3.3 V
    // Per-side ceiling / knee — the two differ by ±railAsymV (see the constant). setSupplyVoltage
    // moves the mean and leaves the offset alone.
    double railVPos { railV9V + railAsymV };
    double railVNeg { railV9V - railAsymV };
    double railKneePos { railV9V + railAsymV - railKneeMargin };
    double railKneeNeg { railV9V - railAsymV - railKneeMargin };
    double railXprev { 0.0 };                       // ADAA state: previous rail-sat input
    double railFprev { 0.0 };                       // ADAA state: F(railXprev) (F(0)=0)
    double s1RailXprev { 0.0 };                     // ADAA state for IC_A's ceiling (see processPre)
    double s1RailFprev { 0.0 };
    double sw1CeilXprev { 0.0 };                    // ADAA state for the SW-1 output ceiling (OD only)
    double sw1CeilFprev { 0.0 };
    double railMean { 0.0 };                        // running mean removed by railDcBlock
    double railDcCoeff { 0.0 };                     // railDcBlock one-pole coeff (set in prepareClip)

    // Capture-match correction: treble high-shelf (hs*) + bass low-shelf (ls*) + bilinear-warp
    // top-octave high-shelf (ws*). shBaseRate is the effective (oversampled) rate.
    double shBaseRate { 48000.0 };
    double hsB0 { 1.0 }, hsB1 { 0.0 }, hsA1 { 0.0 }, hsX1 { 0.0 }, hsY1 { 0.0 };
    double lsB0 { 1.0 }, lsB1 { 0.0 }, lsA1 { 0.0 }, lsX1 { 0.0 }, lsY1 { 0.0 };
    double wsB0 { 1.0 }, wsB1 { 0.0 }, wsA1 { 0.0 }, wsX1 { 0.0 }, wsY1 { 0.0 };
    // Stage-1 base-rate warp shelf (sw*) — designed at s1Rate, applied inside processStage1.
    double s1Rate { 48000.0 };
    double swB0 { 1.0 }, swB1 { 0.0 }, swA1 { 0.0 }, swX1 { 0.0 }, swY1 { 0.0 };
    double htB0 { 1.0 }, htB1 { 0.0 }, htA1 { 0.0 }, htX1 { 0.0 }, htY1 { 0.0 }; // fixed HF-trim high-shelf
    double leB0 { 1.0 }, leB1 { 0.0 }, leA1 { 0.0 }, leX1 { 0.0 }, leY1 { 0.0 }; // fixed LF-extension low-shelf
    double bcB0 { 1.0 }, bcB1 { 0.0 }, bcB2 { 0.0 }, bcA1 { 0.0 }, bcA2 { 0.0 };  // drive-gated bass-cut bell
    double bcX1 { 0.0 }, bcX2 { 0.0 }, bcY1 { 0.0 }, bcY2 { 0.0 };

    // OD clip-depth-gated low-mid restoration (ol* = OD low-shelf; runs post-clip at the OS rate).
    double olB0 { 1.0 }, olB1 { 0.0 }, olA1 { 0.0 }, olX1 { 0.0 }, olY1 { 0.0 };

    // Flat DRIVE make-up into Stage 2 (stateless — a gain, not a filter). Set in setDrive.
    double driveMakeup { 1.0 };

    double clipEnv { 0.0 };   // clipping-depth envelope (gates the even-harmonic coeff)
    double meanSq { 0.0 };    // slow ⟨soft²⟩ (removes only DC from the H2 injection)
    double asymCoeff { 0.0 }; // fast envelope smoothing (clip-depth gate)
    double meanCoeff { 0.0 }; // slow envelope smoothing (DC removal)
    double xLp { 0.0 };       // low-passed clip output (low-band H2 source)
    double meanLow { 0.0 };   // slow ⟨xLp²⟩ (DC removal for the low band)
    double lpLowCoeff { 0.0 };// low-band low-pass coeff (set in prepareClip)
    double gLp { 0.0 };       // low-passed nodeG (source for the low-band depth envelope)
    double lowEnv { 0.0 };    // low-band clipping-depth envelope (drives the low-band wash-out)
    double gHpX1 { 0.0 }, gHpY1 { 0.0 }; // mid-band high-pass state (on nodeG)
    double hpMidCoeff { 1.0 };// mid-band high-pass coeff (set in prepareClip)
};

} // namespace monarch
