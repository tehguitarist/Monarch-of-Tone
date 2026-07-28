# Monarch of Tone — Project Memory

Monarch of Tone is a circuit-level emulation of the Analog Man King of Tone overdrive pedal,
built as an AU/VST3 plugin using JUCE 8+ and chowdsp_wdf Wave Digital Filter modelling.
**Author/Company:** Leigh Pierce

The King of Tone is a **dual-channel** Bluesbreaker-derived overdrive — a 1-to-1 digital clone.
Both channels run in series and are independently bypassable. They're named by LED colour:
**Yellow** (left, stock) and **Red** (right). **Signal flow is Red → Yellow** — the real pedal
processes through Red (the fixed Hi-Gain channel) *first*, then Yellow. Yellow/Red on-screen
position is unchanged from the hardware; small **A** (Red) / **B** (Yellow) badges outside each
LED show the processing order. The Theseus Hi-Gain mod is a **fixed** part of the **Red** channel
only (not a runtime toggle); Yellow is always stock.

---

## Rule Files (read before touching any code)

@.claude/rules/circuit.md      ← circuit topology, all component values, signal flow
@.claude/rules/dsp.md          ← WDF implementation rules, API reference, diode parameters
@.claude/rules/architecture.md ← plugin structure, APVTS parameters, threading model
@.claude/rules/ui.md           ← layout, controls, colour scheme
@.claude/rules/build.md        ← CMake setup, project structure, validation gates

**Agents:**
- `dsp-validator` — invoke after implementing each DSP stage. Don't proceed on a non-PASS verdict.
- `schematic-checker` — invoke for any circuit topology question. Never guess a component value or
  connection from memory.

---

## Quick Reference

```bash
# First-time setup
git submodule update --init --recursive   # JUCE 8+, chowdsp_wdf, xsimd (optional SIMD)

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
cmake --build build --target Monarch_AU     # AU (primary) / Monarch_VST3 / Monarch_Standalone

# Code quality
clang-format -i src/**/*.{cpp,h}
```

**CMake minimum:** 3.15 | **C++ standard:** 17 | **macOS target:** 10.13+ | **Version:** 1.3.0

---

## Roadmap

- **v1.0 — Project cleanup.** ✅ Done — repo/docs audited and condensed; `.claude/rules/*` + this
  file collapsed to load-bearing facts (values, topology, decisions+why), "DONE" narratives and
  resolved discrepancy back-and-forth removed, experiments folded to one-line "tried X → Y"
  notes. Stale facts reconciled to the actual code (Stage-1 floors, input cap, OS defaults).
- **v1.1 — CPU / latency / memory optimization pass (TODO).** Profile `MonarchChannel` /
  oversampling / WDF-solve cost; add CPU-usage and latency measurement to the test suite (a
  benchmark reporting ms/block and reported plugin latency per oversampling factor) and publish
  the numbers in the README. While profiling, flag anything that *costs* CPU but also *improves*
  fidelity (diode-stage ADAA, finer NR iteration, etc.) as a candidate for an optional "HQ" mode
  rather than the default path — **discuss with the user before implementing an HQ toggle**; this
  is a flag for a future decision, not a green light to add it now.
- **v1.4 — FR / harmonic accuracy pass (IN PROGRESS). → `analysis/FR_THD_AUDIT.md`** — full
  findings, evidence and the ordered P0–P5 work plan live there; don't re-derive them here.
  - **P0 harness hygiene — ✅ done (2026-07-28).** THD above the H3 limit (~6.3 kHz) now reports
    `na` (both estimators fail up there: Farina is H2-only, the discrete fallback aliases onto the
    fundamental at 6/8 kHz), the FR trust band (40 Hz–8 kHz) is owned by `comprehensive_report.py`
    and read by the dashboard from `meta`, the per-mode tiles use the same shape metric as the
    heatmap, and **H2-vs-frequency** is a first-class chart. The old dashboard's dramatic 6–8.5 kHz
    "THD cliff" was a measurement artifact and is gone.
  - **P1 sub-64 Hz LF extension — ❌ CLOSED, no DSP change (2026-07-28).** Real but *not*
    correctable; see the rejected-experiments entry below. Consequence: the LF THD gap is now an
    accepted residual too, and **P2/P3 no longer need to wait for P1** — the harmonic baseline
    won't move, so fit them against the current `comprehensive_data.json` directly.
  - **P2 asymmetric op-amp rail saturation — ✅ done (2026-07-28).** `railSaturate` now has
    **separate ± ceilings** (`railAsymV = 0.60 V` around the unchanged 3.3 V mean), which generates
    Boost's whole missing even series with the right internal ratios: H2/H4/H6 went from **−21/−29/
    −39 dB short to within ~2 dB**, odd orders unchanged. The empirical `asymBoost` is retired to 0
    (redundant now). Three things the plan got wrong, all recorded in the audit: the **sign** is
    invisible to harmonic magnitudes and was decided by the null; an asymmetric clipper **rectifies**,
    and the 0.16 Hz output cap smeared a 1 s DC tail into the next segment (now stripped at source —
    worth **0.5–0.7 dB of OD/Dist null on its own**); and **Distortion is rail-clamped**, so a fixed
    asymmetry made it 26 dB too even — it is scaled off under SW-2's ~25× heavier load. Whole set:
    null mean **0.12 dB deeper**, Boost **0.29 dB**, OD/Dist neutral, FR unchanged.
  - **P3 even-harmonic H2 shape in OD/Dist — ✅ done (2026-07-28).** Both injection paths ran
    full-range and **the same fact broke both**: Stage 1's high-shelf makes `nodeG` small below a
    few hundred Hz. Ablation reversed the diagnosis — the plan blamed the low path, but the
    **mid/high** path was the larger share (+15.5 dB hot on its own at 100 Hz), because its
    `tanh(asymDriveScale·nodeG)` wash-out never leaves the linear region down there and so grows as
    `nodeG²` instead of collapsing. Two fixes: **`asymMidFc` = 400 Hz high-pass** on the mid source
    (the counterpart the low band's 150 Hz low-pass always implied — the paths now split the
    spectrum), and the low band gets **its own depth envelope** (`lowEnv`, `asymLowWash` = 25,
    `asymLowThresh` = 0.15 V) because its source is clamped and `clipEnv`'s 0.37 V threshold is
    never met at LF (a wash keyed to it moved H2 by 0.7 dB — nothing). The envelope's **threshold**
    is what makes the shape right rather than merely smaller. `asymLowOD/Dist` then raised 1.4×.
    All 44 captures, driven sweeps: **H2 rms error OD 10.3 → 6.9 dB, Dist 10.2 → 7.9, systematic
    bias eliminated** (+2.8 → 0.0, +3.0 → +0.2). Odd orders bit-identical, Boost byte-identical,
    **null unchanged on every capture**.
  - **P3.1 H4/H6 in OD/Dist — ✅ done (2026-07-28), and it corrected a wrong P3 claim.** P3 called
    this structurally unfixable ("a squared source only makes H2"); its own ablation data disproved
    that, and re-fitting the **tanh knee** fixed it inside the same mechanism. Two knees were
    needed, for different reasons: `asymDriveScale` **1.70 → 3.50** (never re-swept since before
    P2; the knee alone sets the H2:H4:H6 ratio), and a **new** `asymLowDriveScale` = 4.90 on the
    low path — whose source is a low-pass of the clip output, i.e. nearly a **sine**, so its square
    had **no H4/H6 by construction** and never responded to `asymDriveScale` at all. Order matters:
    **knee first, then re-zero the bias with the coefficients** — a joint search drifted H2
    +3.7/+5.1 dB hot buying H4/H6, and a gain moves the whole series while the knee does not. All
    44 captures, driven sweeps: H4 bias **−11.1/−11.2 → −1.5/−2.1**, H6 **−22.0/−18.2 → −7.4/−5.8**,
    H2 bias still **0.0**. Odd orders 0.00 dB changed, Boost byte-identical, null unchanged (worst
    +0.04 dB), and **IMD is now a measured guard** (SMPTE 60 Hz+7 kHz — the pair straddling the
    injection split — 0.00 dB) rather than the by-ear check the plan proposed. H6 stays ~6 dB
    short: accepted, since closing it costs H2 rms faster than it gains H6. Prerequisite settled
    first — `analysis/p31_harm_floor.py` shows the pedal's H4/H6 targets clear the capture chain's
    harmonic floor by 39 dB median / 6.9 dB worst, so none of the gap was measurement noise.
  - **P3.2 Boost's evens vanish on the quiet driven sweeps — ✅ done (2026-07-28), in ONE constant.**
    Found by P3.1's whole-set metric, which P2 could not see (it measured the −6 dB sweep only):
    **30 of 143** Boost H2 cells had the plugin at ≈−160 dBc — no even-harmonic mechanism engaged at
    all — while the pedal reads −18…−59 dBc, and `p31_harm_floor.py` puts those pedal readings
    **+58.7 dB clear of the capture chain's floor at worst** (median +100), so the target was real.
    The fix needed **no new mechanism**, which is the finding: **P3.1's low path was already wired
    and running in Boost** (with `railV` as its `clampRef`) and only its coefficient was zero — and
    it has exactly the property the rails lack, being sourced from a low-pass of the clip output and
    so *always-on* rather than knee-triggered, with `lowEnv` handing over to the rails as drive
    rises. `asymLowBoost` 0 → **−0.017**, chosen on the whole set where aggregate rms is flat across
    0.017–0.030 and only H2's bias moves (P3.1's rule: zero H2). Driven sweeps, whole set: **silent
    30/24/11 → 0**, H2 rms 47.7 → 6.9 (bias −22.5 → **+0.5**), H4 43.2 → 8.3 (−18.3 → −1.7), H6 31.5
    → 11.1 (−12.1 → −4.5) — Boost's even series is now on a par with OD/Dist's and its H4/H6 bias is
    the best of the three. Two invented mechanisms were probed numerically and rejected first (a
    `tanh²` term on `railSaturate` — H4/H6 30–50 dB too weak, P3.1's "squared near-sine" trap again;
    and widening `railKneeMargin` — right ratio, but no margin both spares OD's ±1.64 V and reaches
    0.5 V). Guards: **OD/Dist byte-identical** (verified by rebuilding with the coefficient zeroed),
    Boost null within **0.005 dB**, IMD ≤0.002 dB, 44-capture headline unchanged. What's left is at
    100 Hz / quiet / high drive, where the pedal is fully saturated and the plugin can't swing to its
    rails — that is **P1's LF shortfall seen through the clipper**, not a second mechanism. See
    `FR_THD_AUDIT.md` P3.2.
  - **P6 mid-gain FR peak displacement — ✅ done (2026-07-28), and the premise was wrong.** It was
    framed as a **mode sign-split** (OD peaks too HIGH — G6 T5 488 vs 376 Hz, +0.38 oct — while
    Distortion peaks too LOW, G4/G5 ≈−0.25), and three sessions hunting a mode-differentiated
    mechanism found nothing, correctly: **there isn't one.** Reading the same table as a
    **trajectory in drive** instead of a list of per-capture errors shows one mode-*independent*
    effect — the pedal's FR peak migrates **1.4–1.7 octaves down** from G2 to G10, the plugin's only
    0.2–1.0. The "split" is that single monotone drift crossing zero at a different knob position in
    each mode, because each mode has a different low-drive intercept (G2: Clean +0.24, OD +0.06,
    Dist −0.14). Finding 3's original "±0.2 oct" hid it by averaging; P6's sign reading hid it by
    dropping the drive axis. **Two aggregation errors on the same data.**
    - **Cause:** a **gain-vs-DRIVE-knob curve error above ~G5**. The FR peak is a clip-depth meter
      (**−0.35 oct per +3 dB** of pre-clip level, measured with `p6_peak_fit.py --in-gain`), which
      converts the error into dB of missing drive: ~0 to G5, then **+3.2/+3.7/+5.5/+6.8** at
      G6/G7/G8/G10. Confirmed twice independently — Boost's best-fit gain vs the captures rises
      **−0.69 → +4.83 dB** across G2→G10 (a metric that never touches the peak), and the
      **time-domain null splits at the same knob position**: extra pre-clip level *hurts* at G5 and
      *helps* from G6 up (G6 T5 OD −16.9 → −22.1 at +3 dB).
    - **Fix:** `MonarchChannel::driveMakeup` — one flat drive-keyed gain at NodeG (onset 0.5,
      14 dB/unit, cap 6 dB), **not a shelf**. It restores the half of the real 3-terminal DRIVE
      pot's dual action the 2-terminal model drops: the literal wiring was rejected for over-swinging
      Stage-2 gain (28 vs 10.6 dB), and the 2026-06-29 re-derivation had already shown the discarded
      action moves **Stage 2's flat LEVEL, not Stage 1's tilt** — right shape, wrong magnitude. So
      the schematic-departure authorization ended up not being needed.
    - **Result (all 44 captures):** null **−22.7…−6.8 median −16.4 → −23.2…−6.6 median −16.6**, mean
      **0.52 dB deeper**; 13 deeper (up to **−4.9 dB**, G7 T5 OD), 5 shallower (worst +0.9, the
      anomalous G10 T2 OD), **26 byte-identical** — the gain is exactly 1.0 below G5, so no earlier
      G2–G5 fit moved. Peak error: all-44 mean +0.24 → **+0.08** oct, sd 0.44 → **0.28**; rms OD
      0.44 → **0.14**, Dist 0.37 → 0.21, Clean 0.66 → 0.35. All nine per-stage gates still PASS.
    - **Left open:** Distortion's −0.25 oct at G4–G5 (below the onset — the genuine *intercept*
      part, no mechanism found) and the G10 residual (documented bass bloom; the measured need is
      +6.8 dB vs the 6.0 cap, and raising it trades against the G10 Distortion null).
    - **Rule it establishes:** a **gain-vs-knob error is not a tilt**, even though clipping makes it
      look like one in a single capture's FR. When an error is indexed by a knob, plot it against
      the knob before hypothesising a mechanism. See `FR_THD_AUDIT.md` P6.
  - **P4 the 1.6–5 kHz tilt — ❌ PREMISE WITHDRAWN (2026-07-28). There is no fixed tilt.**
    Re-measured on fresh post-P6 data as the plan required. Aggregated over all 176 capture×sweep
    rows the error looks like a textbook fixed tilt (**+0.23 dB/oct, 99% sign-consistent, ~1.8 dB
    total**) and a retuned `hfTrim` collapses it 0.379 → **0.073 dB rms**. It is an **artifact**:
    the error is indexed by **DRIVE and SWEEP LEVEL jointly**, so a median over either axis alone
    averages the other away and neither marginal shows the extreme cell. **Third instance of the
    same trap** (Finding 3's ±0.2 oct, P6's sign reading) — first one caught *while* building the
    aggregate. On the cleanest linear instrument (Boost, clean sweep) the plugin is **flat to
    ±0.5 dB wherever the drive shelf's treble lift is zero**, and where the lift is non-zero the
    error tracks it **≈1:1** (G2 +3.92 dB vs 3.24 lift; G3 +2.45 vs 2.06; G4 +1.20 vs 0.88).
    Distortion doesn't show it — the ±0.584 V clamp destroys the pre-clip tilt, so it's pre-clip
    and mode-independent. Confirmed on the arbiter, not just FR: removing the lift from G2 T5 Clean
    moves FR rms **1.51 → 0.44 dB** *and* the null **−15.10 → −25.23 dB**, and over the 17 affected
    captures the null deepens **2.3–6.0 dB on every one**. The FR-optimal fixed shelf is rejected by
    the null (splits by drive: −1.9 dB at G2 against +2.5 dB at G5–G6); the honest ceiling on a
    *fixed* HF trim is ~0.1 dB. → **replaced by P7.**
  - **P7 refit the drive-keyed EQ instruments as one set — ✅ done (2026-07-29). Biggest null gain
    yet: median −16.6 → −21.5 dB.** The 450 Hz treble lift is **retired** (`shelfMaxDb` 5.6 → 0,
    `shelfSlopeDb` 11.8 → 0, behind a *derived* `trebleShelfEnabled` so the audio path and the
    header-parsing harnesses cannot disagree) and the bass-cut bell absorbs its job: `bassCutQ`
    0.45 → **0.50**, `bassCutOffDrive` 0.50 → **0.55**, `bassCutSlopeDb` 13.0 → **10.909**,
    `bassCutMaxDb` 4.6 → **6.0**. Pivot 185 was already right; `bassBoost*` deliberately untouched
    (freeing it wanted to move its pivot to 75 Hz — straight into P8's band — for 0.02 dB).
    - **The rule it adds: P4's "least-nonlinear instrument" is necessary but NOT sufficient.** The
      clean sweep *stops being linear part-way up the DRIVE knob* — THD 250 Hz–2 kHz (plugin/pedal)
      runs 0.08/0.87 at G2 through 0.43/1.11 at G6, then **4.64/4.36 at G7, 7.63/7.71 at G8,
      10.45/14.76 at G10**. Fit window is G2–G6. **Check that the instrument is still an instrument
      at the far end of the axis you are sweeping** — a cell can be the least-nonlinear one
      available and still be useless. This is what **withdrew P10's premise** (below).
    - **The defect is ONE see-saw about 508 Hz** — that band reads −0.16…−0.31 dB at *every* drive
      including G10, which is what identifies the pivot rather than assuming it. Tilt (HF−LF) runs
      +3.95/+2.73/+1.25/+0.20/+0.02 dB at G2–G6, then reverses. The shipped law shape
      (`max(0, max − slope·drive)`) already traced that trajectory; only the magnitude was wrong.
    - **Why no single instrument could be read alone:** the lift and the bell each supplied ~half of
      the same correction, so together they delivered ~6.6 dB of tilt at G2 where 3.95 is needed.
      P4 measured the lift alone, saw that removing it fixed G2, and called it 100 % spurious —
      right outcome, wrong reason (removing it left the bell's 3.9 dB standing, ≈ the 3.95 actually
      required). **Corrections that overlap in band AND in keying must be measured in one pass.**
    - **Result (all 44 captures):** median **−16.6 → −21.5**, range −6.6…−23.2 → −6.6…**−25.1**,
      mean 2.46 dB deeper; 24 deeper (up to **−9.1 dB**, G2 T6.5 Clean), **18 byte-identical** (both
      instruments are exactly 0 at and above drive 0.55, so nothing P6 fitted above G5 moved), 2
      shallower (worst +0.9, G5 T8 OD). FR shape rms G2–G6 **0.941 → 0.259 dB**, and Boost/clean is
      now flat to **±0.5 dB at every measurable drive**. All nine per-stage gates PASS. Two
      candidates were fit on FR and judged on the null; **deleting** the shelf beat shrinking it in
      every mode, so the simpler answer won on the arbiter rather than on taste. Harness
      `analysis/p7_eq_refit.py`. See `FR_THD_AUDIT.md` P7.
  - **P8 the LF band read as ONE drive-keyed instrument — ✅ done (2026-07-29). Median null
    −21.5 → −22.6 dB, 38 of 44 captures deeper, and the worst capture in the set moved for the
    first time.** P1's measurements were right; its *conclusion* over-generalised from two shelves,
    both fit to zero the FR magnitude and both ~2× the complex-optimal depth. The phase lead
    blocking a minimum-phase fix exists **only below ~32 Hz**; at 40–80 Hz the pedal **lags** 3–9°,
    exactly what a min-phase low-shelf supplies.
    - **But the fix is NOT the new shelf both P1 and P8's own plan assumed.** A *fixed* 100 Hz
      +1.0 dB shelf is knob-indexed on the arbiter — helps 1.0–1.6 dB at G2–G5, **hurts 0.6–1.5 dB
      at G6–G10**, in every mode — which is `offline_null_probe`'s own stated tell that a fixed
      filter is the wrong instrument. And the band already had a drive-keyed instrument in it:
      **`bassBoost*`**, which P7 had deliberately left alone as "it belongs to P8". So the two were
      fit as one set, P7's rule applied prospectively for once.
    - **The old law was fit to one end of the drive axis.** `bassBoost` (105 Hz, onset G2.5,
      7.5 dB/unit, cap 4.2) exists to counter the high-drive bass bloom, was measured at high drive,
      and read as monotone because nothing had measured the low end. Per-drive fitting shows the
      pedal wants LF gain at **every** drive — **+1.2 dB at G2 where the ramp gives exactly zero** —
      peaking near G5 and then **falling back**: the ramp was ~1.2 dB short below G5 and ~2.4 dB
      **over** at G10, and a ramp cannot express the fall at all.
    - **Fix:** `bassBoost*` becomes a **hump in drive** — 85 Hz, 3.0 dB peak at drive 0.50, falling
      6.0 dB/unit below and 2.5 above, floored at 0 (reached exactly at drive 0). **No new
      instrument, no new mechanism, one law reshaped.** The offline optimum is broad (±10 Hz,
      2.8–3.5 dB, peak 0.48–0.55 all within 0.07 dB), so it was not ground finer than the
      pre-clip/post-clip placement caveat can carry.
    - **Result (all 44 captures):** null **−25.1…−6.6 median −21.5 → −25.6…−8.7 median −22.6**, mean
      and median both **1.05 dB deeper**; **38 deeper** (best −3.4 dB, G8 T5 Clean), **6 shallower by
      at most +0.2 dB**, none worse. The **worst capture in the set improved 2.1 dB** (G10 T2 Dist
      −6.6 → −8.7) — nothing had moved the G10 floor before. FR: the *drive-dependence* of the 20 Hz
      error more than halves (−3.23…+2.95 → −2.12…+0.59 dB). All nine gates PASS; SMPTE IMD median
      +0.05 dB, CCIF −0.00; even-harmonic series neutral except Boost H6 rms 7.9 → 10.9 (bias only
      −3.2 → −3.6 — a quiet harmonic already ~6 dB short, accepted).
    - **Left open:** the sub-32 Hz remainder — a near-constant ~2 dB shortfall at 20 Hz that an
      85 Hz first-order shelf cannot reach without overshooting 80 Hz, and whose phase lead is
      non-minimum-phase anyway. This is now the *whole* of the accepted LF residual.
    - **Harness trap caught and fixed:** `comprehensive_report.py --keep-renders` and
      `run_validation.py --render-dir` write **two different filename conventions into the same
      directory**, and `offline_null_probe.load_pairs` matched only one — so a fresh render run
      silently left the older set in place. The fit was re-run against a known-good post-P7 set
      (identical to 4 decimals, so nothing rested on it). `load_pairs` now accepts both, takes the
      newest per label, and warns when a directory's renders span >2 minutes.
  - **Remaining, in order — see `FR_THD_AUDIT.md` P9–P10:**
    - **P9: Overdrive's mode-specific tilt** (+1.2…+2.5 dB at *every* drive, only mode that does)
      and its matching THD roll-off (−0.8 dB at 320 Hz → **−4.1 dB at 5 kHz** on the hot sweep).
      The documented "OD compresses 3–4 dB lighter" residual, but it has a **shape** — never worked.
      Not a linear-EQ fix; likely the same thing as the un-audited dynamics axis. **Now the largest
      remaining shape error**: post-P7 Boost is flat to ±0.5 dB at every measurable drive, OD is not.
    - **P10: the G8→G10 Boost discontinuity — ⚠️ PREMISE WITHDRAWN by P7 (2026-07-29).** The
      "+4.9 dB tilt, clean sweep, 3/3 tones" is measured where the pedal reads **14.76 %** THD and
      the plugin 10.45 % — not a linear-EQ measurement at all. Something at G10 is still real (its
      nulls are the worst in the set at −6.6…−10.3), but the clean sweep cannot decide it. **Needs a
      different instrument before it needs a fix** — fold it into the dynamics/discrete-tone axes.
    - **Never audited at all** (in the captures, absent from `comprehensive_data.json`): **IMD**,
      **dynamics/compression** (`lvl_-30…-3`), **discrete-tone THD**, **decay**. P2/P3/P6 all changed
      the clip path underneath them.
    Audit
    tools: `analysis/fr_thd_audit.py` (`evens` view = the even-series rms/bias table, `--base` for
    before/after), `analysis/p2_rail_asym_fit.py` (fast clip-nonlinearity fit loop, now with an IMD
    guard — also the P3/P3.1 harness), `analysis/p31_harm_floor.py` (harmonic noise floor),
    `analysis/p6_peak_fit.py` (FR-peak + compression-tilt subset harness, ~15 s; its `--in-gain`
    probe is the clip-depth calibration that identified P6), **`analysis/shape_audit.py`** (FR error
    *shape*: the `cross` drive×level cross-tab that broke P4, and `clean` = Boost/clean-sweep, the
    only near-linear instrument in the set — **and only up to G6**, per P7),
    **`analysis/p7_eq_refit.py`** (the drive-keyed EQ set read as ONE set: `raw` strips all three to
    expose the underlying defect, `seesaw` collapses it to the 508 Hz pivot, `fit` refits them
    jointly, `--base pre-p7` re-reads a pre-P7 JSON), **`analysis/offline_null_probe.py`** (score an
    EQ hypothesis on the arbiter without rebuilding — `transfer` gives complex pedal/plugin
    magnitude **and phase**, `shelf` fits on the complex residual, `null` re-nulls the kept renders).

---

## Repository State

**Engine + UI complete and validated; matched to NAM captures of a real King of Tone.** All DSP
stages, `MonarchChannel`, `processBlock`, and oversampling are done (build.md Validation Gates,
all PASS, auval PASS). The UI is complete: peripheral shared-look shell (side panels, OS strip,
resizable window) + the purple/gold `PedalFace`. Factory presets (5) ship via the host's native
preset browser. Supply-voltage mod (9/12/18V) and rail-saturation ADAA are in. Latest release
engineering: CI/CD (`.github/workflows/`), cross-platform VST3, and per-platform installers
(`installer/`) — see README.

**Calibration result (Step 11, real-pedal A/B; refreshed v1.4 P8 2026-07-29):** the plugin nulls against
44 NAM captures (drive G2–G10, tone T2–T8, Clean/OD/Dist) at **−8.7 to −25.6 dB, median −22.6**, and
is now at **−19.6 dB or better on every capture from G2 to G7** (worst G6 T5 OD). (Was −6.6 to −23.2,
median −16.4→−16.6 after P2/P6. **P7** deepened the mean 2.46 dB and the median 4.9 dB — 24 captures
deeper by up to 9.1 dB, concentrated at G2–G4 where the double-counted EQ correction lived, 2 shallower
by ≤0.9 dB, and 18 byte-identical because both refitted instruments are exactly zero at and above drive
0.55. **P8** then deepened mean and median a further 1.05 dB — 38 of 44 deeper by up to 3.4 dB, 6
shallower by ≤0.2, and it is the first change to move the **G10 floor**, taking the set's worst capture
from −6.6 to −8.7 dB.) Best
per-mode null at the labelled mid-gain settings (G5 T5): Clean/Boost −23.3, OD −22.2, Dist −22.7 dB.
Excellent to mid gain; shallower only at very high drive (G8–G10) — an
accepted device-physics / capture-aliasing residual, not a topology error (every Stage-1 value +
topology re-traced exact against the Theseus schematic). The 44 captures (`analysis/pedal_export2/`,
842 MB) are **local-only, gitignored** — re-capture against `analysis/test_signal_48k.wav` to
reproduce.

### Experiments tried and rejected (don't re-attempt without new evidence)
- **Literal 3-terminal DRIVE pot** (wiper=output, pin3→R6→Stage2): over-swings Stage-2 gain (~28 dB
  total vs measured ~10.6 dB). The 2-terminal rheostat approximation matches the captures *better*.
- **Sharper op-amp rail-sat knee** (smaller `railKneeMargin`) to bloom the bass at high drive:
  negligible effect (≤0.007 sample, 0.0 dB on every null/bass metric) — once a swing is over the
  rail it's clamped regardless of knee shape. The bloom needs more *gain into the rails*, which
  can't be added circuit-accurately. Reverted.
- **Rail-sat knee softening for high-drive null** (railV 3.3↔3.6, knee 3.0↔3.18): helps G8/G10
  ~0.7 dB but costs G6 ~0.9 dB — a wash. Left at the circuit-motivated ±3.3 V / knee 3.0.
- **Active null optimization at G5** (drive×tone×input-level search per mode): the labelled/nominal
  settings are already optimal — tuning the heavily-driven segment deeper only trades against the
  lighter segments. Knob calibration confirmed correct.
- **Sub-64 Hz LF-extension low-shelf** (`MonarchChannel::lfExt*`, 2026-07-28, v1.4 P1 — code kept,
  `lfExtEnabled = false`): the deficit is **real** (pedal is +2.7 dB at 20 Hz, present in the raw
  circuit at every drive/mode) and there is **no topology fix** — `schematic-checker` traced every
  RC in both schematics including the bias/supply network and nothing lands near 45–55 Hz; the
  named suspect, the literal 3-terminal DRIVE wiper-tap, uses R6=10k/C5=100n, i.e. the *same*
  159 Hz corner already modelled as R9/C7. But the empirical shelf **fails**: two fits (+3.5 dB @
  60 Hz minimising FR rms, and +5.0 dB @ 25 Hz confined to the drive-agreed band) both improved FR
  error on 33/42 captures while making the **null worse on 27–28/42**, gutting the best matches
  (G6 T5 Clean −22.0 → −17.7, G7 T5 Dist −17.9 → −13.5).
  - **Why:** the pedal is louder at 20–40 Hz **and its phase LEADS** (+33° at 20 Hz). A
    minimum-phase low-shelf adding +3 dB necessarily brings ≈−15° of *lag*, so the magnitude error
    goes to zero while the phase error grows 33°→48° and the complex residual *grows*
    (|1.36∠33°−1| = 0.76 → |0.96∠48°−1| = 0.81).
  - **Proved by direct A/B:** the identical magnitude correction applied offline zero-phase *helps*
    every case (G6 Dist −19.6→−20.6, G5 OD −21.1→−22.4) while minimum-phase hurts every case. So
    the reading and the direction were right and only the **instrument** was wrong — but a
    zero-phase shelf reaching 25 Hz is a multi-thousand-tap FIR (tens of ms latency) for ~0.6 dB.
    Ruled out on cost.
  - **⚠️ CORRECTED by P8 (closed 2026-07-29) — "don't re-attempt with any IIR EQ" is WITHDRAWN, and
    a min-phase IIR fix has now SHIPPED.** The prohibition held for *these two shelves* only: both
    were fit to zero the **FR magnitude** and both are ~2× the complex-optimal depth. Measured
    per-band, the phase lead exists **only below ~32 Hz** — from 40–80 Hz the deficit is still
    +0.5…+2.1 dB and the pedal **lags 3–9°**, exactly what a min-phase low-shelf supplies.
    Also: not a corner error — reaching +2.7 dB at 20 Hz needs C7 = 137 nF against a 100 nF ±10%
    part, and a lower corner gives *less* lead, the wrong direction.
    **The depth axis was never searched with a phase-aware metric — and neither was the DRIVE axis,
    which is where the answer was.** A *fixed* shelf of any depth is the wrong instrument (it splits
    by knob: helps G2–G5, hurts G6–G10). P8 shipped the correction by refitting the **existing**
    `bassBoost*` low-shelf into a hump in drive, adding no new filter at all — median null −21.5 →
    −22.6 dB. `lfExt*` stays retired and is now redundant rather than merely rejected.
    See `FR_THD_AUDIT.md` P8.
  - **Metric lesson (new, and the mirror image of the presence-bump one):** FR rms weights every
    third-octave band equally and is **blind to phase**; the time-domain null is complex. Note the
    "no guitar energy below 80 Hz" intuition does **not** apply to this test signal — an
    exponential sweep carries equal energy per octave, so 20–40 Hz is fully weighted in the null.
    For anything that can carry phase: **FR generates the hypothesis, the null decides.**
- **Capture-match tilt shelf** (`TiltShelf`, an artificial fixed high-shelf): retired
  (`kEnabled=false`) once the corrected Stage-1 Z_lower topology reproduced the EQ tilt
  circuit-accurately. Code kept for A/B only. (Superseded by the drive-dependent two-shelf
  correction below — a *fixed* shelf cannot fix a tilt that reverses sign with drive.)
- **Literal 3-terminal DRIVE wiper-tap as the cause of the low-drive EQ collapse** (2026-06-29):
  re-derived the full wiper-tap transfer function (pin1→R2→NodeF, wiper=NodeG, pin3→R6→C5→Stage2)
  numerically — it has the *same* drive-dependence of the Stage-1 tilt as the 2-terminal model
  (the pot's dual action moves Stage 2's flat *level*, not Stage 1's *tilt*). So the real pedal's
  drive-INDEPENDENT clean EQ is not explainable by the linear topology; corrected empirically (below).
- **Low-mid "presence bump" (335 Hz peaking biquad, fixed +4→2.6 dB pre-clip) to add 200–500 Hz
  body** (2026-07-03, reverted 2026-07-04): the *fixed* bump was a **regression and was reverted** —
  but the underlying instinct was partly right (see the nuance below). It was fit by a flawed
  *tilt-corrected-excess* method (straight line through the 100 Hz & 1 kHz shoulders, curvature
  between = "deficit") which massively overstated the size and got the drive-profile backwards. As a
  fixed pre-clip bump it was largest at low drive (no deficit there) and compressed away at hard drive
  (where the deficit is), so the best-fit-gain null got worse everywhere (overall worse ~2–4 dB mean,
  up to +7.5 dB at low-mid gain; e.g. OD G5 overall −21→−15).
  - **The nuance (measured 2026-07-04, the authoritative view):** a **small, real, drive-dependent**
    low-mid deficit DOES exist, but ONLY in hard-driven Overdrive. Farina linear-TF (`analyze.py
    linear_tf`, which removes the distortion and isolates the linear EQ) vs the captures, normalized at
    1 kHz, 200–500 Hz: OD G5 = **−0.3 dB at light drive → −1.6 dB at hot drive (−6 dB sweep)**;
    Clean/Boost +0.5 dB and Distortion −0.3 dB both already match. So the perceived "real pedal has
    more mids in OD" is CORRECT but it's a ~1.5 dB, drive-gated, OD-only effect — not the 3–4 dB
    fixed bump that was tried.
  - **Metric lesson (important):** for a small linear-EQ question, the **Farina `linear_tf`** is the
    right tool. Both other methods fail here in opposite directions: the tilt-subtraction plot
    *invented* a large deficit; the best-fit-gain **null test** *masked* the real one (a global gain +
    the loud fundamental/harmonics swamp a 1–2 dB linear feature — OD G5 nulled deep yet had a real
    −1.6 dB mid dip). Use `null_test.py` for overall waveform match, `linear_tf` for EQ shape.
  - **The correct fix that WAS built (2026-07-04, `MonarchChannel::odLowShelf`):** a clip-depth-gated
    low-shelf on the OD clip output — `gate = sw1On ? tanh(odGateScale·clipEnv) : 0`, 2.0 dB @ 520 Hz,
    gate 12. OD-only (Boost/Dist byte-identical), inert at normal levels, engages only under hard clip;
    roughly halves the hot-drive deficit (OD G5 60–500 Hz −1.6→−0.8, overall null −1.2 dB) with the
    time-domain null neutral at normal levels and worst-case ~+0.3 dB at the G10+hot extreme. See
    dsp.md "OD clip-depth-gated low-mid restoration".
  - **Validation-metric lesson (the crux both failures share):** match the metric to the question.
    `null_test.py` (best-fit-gain, time-domain) = overall waveform match, and it MASKS a small linear
    feature. Farina `linear_tf` = linear EQ shape, but it MIS-READS a *clip-gated* correction (the
    gate modulates across the sweep → deconvolution artifact showing a false deficit at moderate
    drive). So: use `linear_tf` to FIND the linear deficit, but the **time-domain null is the
    arbiter for validating a clip-gated correction**. The tilt-subtraction excess plot *invented* a
    deficit and must not be used. The reusable audit encoding all four guardrails is
    `analysis/mid_eq_audit.py` (→ `analysis/MID_EQ_AUDIT.md`).

### Drive-dependent two-shelf capture-match correction (`MonarchChannel`, 2026-06-29)
Best-fit-gain-aligned EQ error (plugin vs captures, 40 Hz–16 kHz, every gain/tone) is a clean,
tone-independent **tilt that reverses with drive**: treble-short at low drive, bass-short/treble-hot
at high drive, crossing near G4. Corrected with two drive-scaled first-order shelves on Stage 1's
output (pre-clip): a **treble high-shelf** fading OUT with drive (restores the Stage-1 HF shelf
`Av=1+Z_upper/Z_lower` lets collapse at low drive — the "engaging it is dark" complaint) and a
**bass low-shelf** fading IN with drive (counters the bass-bloom-under-drive). Also *improves*
OD/Dist nulls at mid/high drive (G5 OD −18.4→−23.7, G5 Dist −14.9→−19.1).

> **⚠️ The treble half of this is RETIRED (v1.4 P7, 2026-07-29).** Its parenthesised rationale above
> was never measured, and when it was, the captures said the opposite — the plugin was too *bright*
> at low drive, not too dark. It and the bass-cut bell below were each supplying about half of one
> correction and were fit independently, so together they over-corrected G2 by ~1.65×. `shelfMaxDb`/
> `shelfSlopeDb` are now 0; the bell carries the whole job. See the v1.4 P7 roadmap entry and
> `FR_THD_AUDIT.md` P7.
>
> **⚠️ The bass half was then REFIT by v1.4 P8 (2026-07-29) and is no longer "fading IN with
> drive".** It is a **hump**: 85 Hz, 1.2 dB at G2, peaking 3.0 dB at G5, back to 1.75 dB at G10.
> The monotone ramp was fit to one end of the axis — it counters the high-drive bloom, was measured
> at high drive, and read as monotone because nothing had measured the low end, where the pedal
> actually wants +1.2 dB and the ramp gave exactly zero. P8 also folded P1's separate sub-64 Hz
> LF-extension shelf into this one rather than adding a second instrument to the same band on the
> same key. See the v1.4 P8 roadmap entry and `FR_THD_AUDIT.md` P8.

**Low-drive bass-cut bell + fixed HF trim (`MonarchChannel`, 2026-07-04, v1.3):** a later A/B (by ear
+ harmonic-immune tone bursts) found Boost/Clean ran **~+3 dB too bassy below ~250 Hz at low drive**
(G2), a bump PEAKING ~180 Hz that reverses to ~−1.8 dB thin by G10. Audible only in Boost (OD/Dist
clipping masks it). Fixed with a drive-gated **bass cut bell** (`bassCut*`, 185 Hz, Q 0.45 — a WIDE
bell, refined 07-05 from 160/Q0.7 to flatten the broad 100–330 Hz excess to ±0.2 dB) that fades
OUT by G5 — a bell not a shelf (a shelf over-cuts sub-100, under-cuts the 150–220 peak). Validated:
driven-sweep nulls **improve 1–2.8 dB at G2–G4 in ALL three modes**; only cost is a small clean-sweep
(below-playing-level) regression at G2/G3 leaving them at −15…−18 dB (the excess is level-dependent, a
knob-keyed cut can't fully separate the quiet clean sweep from playing level).
**v1.4 P7 (2026-07-29) refit it** — Q 0.45→**0.50**, off-drive 0.5→**0.55**, slope 13.0→**10.909**,
max 4.6→**6.0**, pivot 185 unchanged — so it now carries the retired treble shelf's share too. That
also **dissolved the clean-sweep regression noted above** — G2/G3 clean-sweep nulls −14…−18 →
**−23…−25**. So that regression was not the level-dependence it was attributed to; it was the
double-counted correction, showing up worst on the one sweep where no clipping masked it. (The
level-dependence hypothesis was never tested against the alternative — it was the only one on offer
at the time.) A separate **fixed HF-trim
high-shelf** (`hfTrim*`, −1.3 dB @ 4.5 kHz) eases the slightly-hot top end to match the captures within
~0.3 dB across 2–4.5 kHz (above that the captures roll off/alias — 6 kHz has a spurious −15 dB dip — so
the trim is conservative and by-ear-confirmable, NOT fit to those artifacts). See dsp.md drive-shelf section.

> **Deferred note — Red drive-shelf keying:** these drive-dependent EQ shelves are keyed to the raw DRIVE
> knob and fit to the Yellow captures. On Red, gain/clipping/harmonics track the ACTUAL gain (so Red@d ≈
> Yellow@(d+1⁄6) there), but the knob-keyed EQ correction does NOT shift by 1⁄6 → Red over-cuts the low
> mids ~1–2 dB at LOW drive. Left as-is (Red has no NAM reference; unvalidatable either way). Potential
> fix + rationale in dsp.md drive-shelf section ("Deferred refinement — Red drive-shelf keying").

### Linear stages run oversampled — top-octave warp fix (2026-06-29)
The remaining top-octave deficit (16 kHz ~−3.8 dB at every setting) was first wrongly blamed on NAM
capture aliasing — but NAM captures null to ~−50 dB and ARE accurate up there. It's **bilinear-
transform frequency warping** of the base-rate linear WDF solve (16 kHz deficit collapses −2.4 dB
@48k → −0.2 @96k → ~0 @192k when the linear stages run faster). Fixed by running the **whole channel
oversampled** (not just the clip span): both `prepareLinear` and `prepareClip` re-prep at the OS
rate. **Render/2x+ paths now match 50 Hz–16 kHz within ~1.2 dB at all gain/tone** (worst ~2.3 dB at
the tone-down top-octave corner). A **rate-scaled warp high-shelf** (`warp*` in MonarchChannel,
DC-normalized) corrects the residual finite-rate droop. **Recalibrated 2026-06-30:** it was
previously self-disabled by 2x (`×(48k/rate)^4`), which left the live default (2x) ~2–3 dB darker on
top than the render path (4x/8x) — a tone difference between playback and bounce. It's now FIT to the
warp-free-baseline-vs-8x deficit so **2x and 4x match 8x** through the audible top (DC–8 kHz ≤0.2 dB,
12 kHz ~0.4 dB; only the 16 kHz edge is ~1.8 dB short at 2x — a first-order shelf can't reach Nyquist
without over-brightening the 6–8 kHz presence band, so the moderate 6.5 k pivot is deliberate). The
DC-normalization (divide by H(z=1)) holds low/mid at exact unity at every rate (without it the
near-Nyquist prewarp droops the whole spectrum, several dB at 1x). **1x** stays the low-CPU/
approximate-top mode (its 16 kHz is still deficient — use 2x+ for full fidelity). CPU cost: the
linear WDF now runs at the OS rate too (relevant to the v1.1 perf pass).

### Accepted residuals (un-modeled second-order device physics, per user pref for circuit accuracy)
- **Sub-32 Hz shortfall (~2 dB at 20 Hz, near-constant across drive)** and the **LF THD gap it
  causes** (40 Hz, G10 Clean, −6 dB sweep: pedal 35.6% vs plugin 4.7%). Not a topology error.
  **Narrowed by P8 (done 2026-07-29)** from "sub-64 Hz, ~2.7 dB, and strongly drive-dependent" —
  refitting `bassBoost*` into a hump more than halved the drive-dependence of the 20 Hz error
  (−3.23…+2.95 → −2.12…+0.59 dB). What is left is genuinely stuck: an 85 Hz first-order shelf deep
  enough to reach 20 Hz overshoots 80 Hz, and below ~32 Hz the pedal's phase *leads*, so no
  minimum-phase filter matches it at all. See `FR_THD_AUDIT.md` P1/P8.
- **OD compresses ~3–4 dB lighter than the real pedal at hot input** (Distortion compression good,
  Δ~2 dB). **Not a flat offset — it has a shape** (P9, open): OD's THD falls off with frequency far
  faster than the pedal's, Δ −0.8 dB at 320 Hz → **−4.1 dB at 5 kHz** on the hot sweep, and the same
  fact makes OD the only mode carrying a tilt at every drive.
- A small genuine HF-harmonic difference >8 kHz (tone-stage rolloff); the captures' own 4–6 kHz
  energy is partly NAM aliasing (the plugin's 8× anti-aliased clip is the more-correct version).
- Per-mode capture levels are **normalized** (Boost/OD/Dist sit at the same level, physically
  impossible at fixed volume) — A/B and null tests must re-gain per mode. The plugin's
  Boost>OD>Dist hierarchy is physically correct (diode-clamp ratios).

---

## Key Circuit Facts

| Fact | Value |
|------|-------|
| Op-amp | JRC4580D per channel (matsumin label JRC4558D is wrong); modelled ideal + rail saturation |
| Stage 1 (IC_A) | Non-inverting — no `PolarityInverterT`; two-one-port solve (no R-type matrix) |
| Stage 1 Z_lower | C4(10n) series [ R4(27k) ∥ (R5(33k) + C3(10n)) ] — Theseus topology |
| Stage 1 Z_upper | (floor + DRIVE 0–100k) ∥ C2(100pF); Av(s) = 1 + Z_upper/Z_lower, DC gain 1 |
| Stage 1 feedback floor | **Yellow R2∥R3 ≈ 990 Ω** (stock) / **Red ≈ 17.7 k** (tamed Hi-Gain = R6_floor + DRIVE_max/6; voicing choice over the literal R2=100k — shifts Red's drive curve +⅙ knob, i.e. Red@d≈Yellow@(d+1/6); an earlier +⅓ tame A/B'd as still too hot). `hiGain` ctor flag |
| Input coupling cap | 22n (Theseus; matsumin 10n — both sub-audio) |
| Stage 2 (IC_B) | **Inverting** ×−22 (R10 220k / R9 10k); inversion via op-amp VCVS terminals; HPF 159 Hz (C7 100n) |
| Soft-clip SW-1 | MA856 ×4 = `[D4+D5]∥[D2+D3]` ≡ ONE `DiodePairT` n_eff=2·1.512≈3.024, Is=7.74e-13; +R11(6.8k), branch ∥ R10 |
| Hard-clip SW-2 | 1S1588 ×2 antiparallel ≡ ONE `DiodePairT` (Is=2.52e-9, n=1.752); shunt at node_HC via R12(1k) |
| Diode topology | **Symmetric pairs only** — `DiodePairT`, never `DiodeT` |
| DRIVE / TONE / Presence taper | 100kB / 25kB / 50kB — all **linear** (2-terminal rheostats; TONE is a 3-terminal tap) |
| VOL taper | 100kA **audio**, `pow(10, 1.8·(x−1))` (noon = −18 dB; fitted to captures) |
| Channel routing | **Red → Yellow** in series (real pedal flow); independently bypassable |
| Default mode | Overdrive (SW-1 ON, SW-2 OFF) per channel; Presence fully CCW |
| Gain peak | +12.85 dB @ ~4.1 kHz (Yellow, drive 0.5). Accurate at base rate — linear stages need no oversampling/prewarp |
| Op-amp rails | **Asymmetric**: +3.9 / −2.7 V soft knee @ 9V (3.3 V mean ± `railAsymV` 0.60). Generates Boost's even harmonics. Tone-safe in OD (diodes clip first, byte-identical); scaled OFF in Distortion, whose rail-clamped path drives ~25× the load. Rectified DC stripped at source (50 ms). Mean scaled by supply-voltage mod |
| Calibration | `circuitVoltsPerFS = 0.87` (real circuit volts internally, not normalized) |
| Oversampling | live 1/2/4/8x default **2x**; render default **4x** (auto via `isNonRealtime()`); wraps the **whole channel** (linear stages too, to kill near-Nyquist bilinear warp); bypassed channels skip it |

## Three Most Likely Implementation Mistakes

1. **`DiodeT` instead of `DiodePairT`** — both clipping stages use symmetric matched pairs.
2. **Audio taper on DRIVE or TONE** — both are linear (B-taper). Only VOL is audio taper.
3. **Reading an R-type output off a source port** — read passive ports only (the source port
   2-point-averages → a spurious HF droop; this once dragged Stage 1's peak down ~880 Hz). And
   Stage 2's inversion lives in the VCVS terminal assignment — the gate is the measured −22 gain.

## Real-Pedal Calibration Harness (`analysis/`)
- NAM captures (`analysis/pedal_export2/*.wav`, local-only) of a real KOT (single stock/Yellow
  channel) at labelled settings (e.g. "G6 T5 OD" = drive 60%, tone 50%, Overdrive).
- `test_signal_48k.wav` (the input) ← `gen_test_signal.py`. Schematics: `KoT_schematic_matsumin`,
  `KoT_schematic_Theseus.png`.
- `analyze.py` (Farina-ESS: freq response + THD-by-band + harmonics + IMD + dynamics),
  `null_test.py` (sub-sample-aligned null depth + per-mode LS-gain), `run_validation.py` (renders
  the plugin at every capture's settings, writes `VALIDATION_REPORT.md`), `internal_checks.py`
  (volume/knob/sample-rate/aliasing for axes with no hardware reference), `null_optimize.py`.
- `comprehensive_report.py` (renders all 44 captures → `reports/comprehensive_data.json`: FR, THD
  and H2–H7 per band per sweep level) + `dashboard_gen.py` (→ `reports/dashboard.html`).
  `fr_thd_audit.py` reads that JSON and produces the tables in **`FR_THD_AUDIT.md`** (the v1.4
  findings + plan) — its `raw` view strips `driveShelf()` to separate a mis-tuned correction shelf
  from a real circuit gap, its `alias` view shows which bands are not measurable at all, and its
  `evens` view is the whole-set even-harmonic rms/bias table P3/P3.1 are judged on (`--base
  OLD.json` prints before/after; the `silent` column counts cells where the plugin has *no* even
  mechanism engaged, which is what the Boost rows are actually reporting — see P3.2).
- `p31_harm_floor.py` measures the **capture chain's harmonic noise floor** by gating the Farina IR
  at fractional orders (between the harmonic impulses). Run it before fitting any quiet harmonic —
  it is what proved the H4/H6 targets were real signal (39 dB median margin) and not noise.
- **Bands that are NOT trustworthy:** FR above ~8 kHz (±18 dB capture-side spread) and THD above
  ~5 kHz (Farina is H2-only there; the discrete-tone fallback aliases onto the fundamental at 6 and
  8 kHz — the captures read up to 291% THD). Don't fit anything to them. See FR_THD_AUDIT.md §4.
- `tools/PedalRender` renders a WAV through the real processor (Yellow-only) for A/B:
  `PedalRender in.wav out.wav drive tone vol pres clip`.

## References

- CCRMA paper: https://ccrma.stanford.edu/~kaichieh/KingOfTone.pdf
- Theseus kit documentation: https://aionfx.com/app/files/docs/theseus_kit_documentation.pdf
- Schematics: `analysis/KoT_schematic_matsumin` (matsumin Ver2), `analysis/KoT_schematic_Theseus.png`
