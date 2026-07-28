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
    static constexpr double asymOD = -0.43;    // OD even-harmonic mix coeff
    static constexpr double asymDist = -0.14;  // Distortion mix coeff
    static constexpr double asymBoost = 0.0;   // RETIRED — superseded by railAsymV (was 0.35)
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
    // The Clean/Boost EQ error vs the captures is a bass TILT that reverses with drive — the plugin
    // runs ~+3 dB too bassy (a bump PEAKING ~180 Hz) at low drive (G2) and ~−1.8 dB too thin at high
    // drive (G10). The two ends need different SHAPES (a bell at low drive, a shelf at high drive), so
    // they are two separate drive-dependent corrections:
    //   • bass BOOST low-shelf, fades IN with drive — counters the high-drive bass-bloom (original).
    static constexpr double bassPivotHz = 105.0;       // bass boost low-shelf centre (Hz)
    static constexpr double bassOnsetDrive = 0.25;     // boost engages above this drive
    static constexpr double bassBoostSlopeDb = 7.5;    // dB of LF boost per unit drive past onset
    static constexpr double bassBoostMaxDb = 4.2;      // cap on the high-drive LF boost
    //   • bass CUT bell, fades OUT with drive — removes the low-drive low-mid EXCESS. A WIDE peaking
    //     bell (not a shelf: a shelf over-cuts sub-100 and under-cuts the peak): the excess is broad
    //     (100-330 Hz, peaking ~200), so a low-Q bell centred 185 flattens it to ±0.2 dB at G2.
    static constexpr double bassCutPivotHz = 185.0;    // cut-bell centre (Hz)
    static constexpr double bassCutQ = 0.45;           // cut-bell width (low Q = wide, covers 100-330)
    static constexpr double bassCutOffDrive = 0.5;     // cut fades to 0 at this drive (== G5)
    static constexpr double bassCutSlopeDb = 13.0;     // dB of cut per unit drive below the cutoff
    static constexpr double bassCutMaxDb = 4.6;        // cap on the low-drive cut
    // Fixed (drive-independent) HF trim high-shelf: eases the plugin's slightly-hot top end toward
    // the captures (matches them within ~0.3 dB across 2-4.5k, where the captures are reliable).
    static constexpr double hfTrimPivotHz = 4500.0;    // HF-trim high-shelf centre (Hz)
    static constexpr double hfTrimDb = -1.3;           // HF cut above the pivot (dB)

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
        // LF extension: fixed, drive- and mode-independent low-shelf (glo=lift, ghi=1). Rate-only,
        // so it belongs here rather than in updateDriveShelf. See the lfExt* constants.
        shelfCoeffs (std::pow (10.0, lfExtDb / 20.0), 1.0, lfExtPivotHz, leB0, leB1, leA1);
        // OD clip-gated low-shelf: fixed coeffs at the OS rate; a low-shelf sets ghi=1, glo=lift.
        shelfCoeffs (std::pow (10.0, odShelfMaxDb / 20.0), 1.0, odShelfPivotHz, olB0, olB1, olA1);
        // Fixed HF-trim high-shelf (drive-independent): eases the slightly-hot top end (glo=1, ghi=cut).
        shelfCoeffs (1.0, std::pow (10.0, hfTrimDb / 20.0), hfTrimPivotHz, htB0, htB1, htA1);
        hsX1 = hsY1 = lsX1 = lsY1 = wsX1 = wsY1 = olX1 = olY1 = htX1 = htY1 = bcX1 = bcX2 = bcY1 = bcY2 = leX1 = leY1 = 0.0;
    }

    void prepareClip (double clipRate)
    {
        stage2.prepare (clipRate);
        sw1.prepare (clipRate);
        sw2.prepare (clipRate);
        asymCoeff = std::exp (-1.0 / (asymTauSeconds * clipRate));      // fast: clip-depth gate
        meanCoeff = std::exp (-1.0 / (asymMeanTauSeconds * clipRate));  // slow: DC removal only
        lpLowCoeff = std::exp (-2.0 * M_PI * asymLowFc / clipRate);     // low-band low-pass corner
        railDcCoeff = std::exp (-1.0 / (railDcTauSeconds * clipRate));  // asymmetric-rail DC removal
        clipEnv = 0.0;
        meanSq = 0.0;
        xLp = 0.0;
        meanLow = 0.0;
        railXprev = 0.0; // F(0)=0 for any rails
        railFprev = 0.0;
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
        railMean = 0.0;
        hsX1 = hsY1 = lsX1 = lsY1 = wsX1 = wsY1 = olX1 = olY1 = htX1 = htY1 = bcX1 = bcX2 = bcY1 = bcY2 = leX1 = leY1 = 0.0;
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

    // Base-rate front: input network + Stage 1 → V(NodeG), then the drive-dependent voicing
    // correction (high-shelf; unity pass-through once drive ≳ 0.47, see shelf* consts).
    inline double processPre (double x) noexcept { return driveShelf (stage1.processSample (x)); }

    // Oversampled nonlinear span: Stage2 (or SW1 soft clip) → op-amp rail-sat → SW2 (or pass)
    // → V(node_HC). This is the ONLY part that should run at the oversampled rate.
    inline double processClip (double nodeG) noexcept
    {
        double pin7 = sw1On ? sw1.processSample (nodeG) : stage2.processSample (nodeG);
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
        railFprev = railAntideriv (railXprev); // keep the ADAA antiderivative consistent with new rails
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
    // fades OUT with drive, a bass BOOST low-shelf that fades IN with drive (high-drive bloom), and a
    // bass CUT bell that fades OUT with drive (low-drive low-mid excess). All on Stage 1's output.
    void updateDriveShelf (double drive01) noexcept
    {
        const double trebleDb = std::max (0.0, shelfMaxDb - shelfSlopeDb * drive01);          // HF lift
        const double bassBoostDb = std::min (bassBoostMaxDb, std::max (0.0, bassBoostSlopeDb * (drive01 - bassOnsetDrive)));
        const double bassCutDb = -std::min (bassCutMaxDb, std::max (0.0, bassCutSlopeDb * (bassCutOffDrive - drive01)));
        shelfCoeffs (1.0, std::pow (10.0, trebleDb / 20.0), shelfPivotHz, hsB0, hsB1, hsA1);  // treble high-shelf
        shelfCoeffs (std::pow (10.0, bassBoostDb / 20.0), 1.0, bassPivotHz, lsB0, lsB1, lsA1); // bass boost low-shelf
        peakCoeffs (bassCutPivotHz, bassCutDb, bassCutQ, bcB0, bcB1, bcB2, bcA1, bcA2);        // bass cut bell
    }

    inline double driveShelf (double x) noexcept
    {
        const double t = hsB0 * x + hsB1 * hsX1 - hsA1 * hsY1; // treble high-shelf
        hsX1 = x;
        hsY1 = t;
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
        const double w = wsB0 * e + wsB1 * wsX1 - wsA1 * wsY1; // bilinear-warp top-octave correction
        wsX1 = e;
        wsY1 = w;
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

    double railV { railV9V };                       // MEAN op-amp ceiling (V); 9 V default = 3.3 V
    // Per-side ceiling / knee — the two differ by ±railAsymV (see the constant). setSupplyVoltage
    // moves the mean and leaves the offset alone.
    double railVPos { railV9V + railAsymV };
    double railVNeg { railV9V - railAsymV };
    double railKneePos { railV9V + railAsymV - railKneeMargin };
    double railKneeNeg { railV9V - railAsymV - railKneeMargin };
    double railXprev { 0.0 };                       // ADAA state: previous rail-sat input
    double railFprev { 0.0 };                       // ADAA state: F(railXprev) (F(0)=0)
    double railMean { 0.0 };                        // running mean removed by railDcBlock
    double railDcCoeff { 0.0 };                     // railDcBlock one-pole coeff (set in prepareClip)

    // Capture-match correction: treble high-shelf (hs*) + bass low-shelf (ls*) + bilinear-warp
    // top-octave high-shelf (ws*). shBaseRate is the effective (oversampled) rate.
    double shBaseRate { 48000.0 };
    double hsB0 { 1.0 }, hsB1 { 0.0 }, hsA1 { 0.0 }, hsX1 { 0.0 }, hsY1 { 0.0 };
    double lsB0 { 1.0 }, lsB1 { 0.0 }, lsA1 { 0.0 }, lsX1 { 0.0 }, lsY1 { 0.0 };
    double wsB0 { 1.0 }, wsB1 { 0.0 }, wsA1 { 0.0 }, wsX1 { 0.0 }, wsY1 { 0.0 };
    double htB0 { 1.0 }, htB1 { 0.0 }, htA1 { 0.0 }, htX1 { 0.0 }, htY1 { 0.0 }; // fixed HF-trim high-shelf
    double leB0 { 1.0 }, leB1 { 0.0 }, leA1 { 0.0 }, leX1 { 0.0 }, leY1 { 0.0 }; // fixed LF-extension low-shelf
    double bcB0 { 1.0 }, bcB1 { 0.0 }, bcB2 { 0.0 }, bcA1 { 0.0 }, bcA2 { 0.0 };  // drive-gated bass-cut bell
    double bcX1 { 0.0 }, bcX2 { 0.0 }, bcY1 { 0.0 }, bcY2 { 0.0 };

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
