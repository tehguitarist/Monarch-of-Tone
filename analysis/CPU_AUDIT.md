# CPU Audit — where the per-sample cost actually goes (v1.5)

Audit dates **2026-07-29** (step 1) and **2026-07-30** (step 2). Companion to
`analysis/FR_THD_AUDIT.md`, which owns the *accuracy* work; this file owns the *cost* work. CLAUDE.md's
v1.5 roadmap entry carries the headlines — the evidence, the measurement protocol, the rejected levers
and the ordered plan live here.

Everything below is measured on the real DSP, never estimated from instruction counts.
**Sign convention:** ns/sample per **pedal channel** at 8x, drive 0.7, unless stated. Multiply by
×OS ×2 pedal channels ×2 audio channels to reach an output frame.

---

## 0. The two rules this audit is built on

1. **Oversample what can ALIAS, not what is merely inaccurate.** A linear stage's error under the
   bilinear map is frequency *warp*, which a filter can correct afterwards. A nonlinearity's error is
   *aliasing*, which nothing downstream can undo. Paying ×OS for the first kind is the expensive way
   to buy something cheap. `analysis/perf_split_probe.cpp` exists to tell the two apart.
2. **`PerfBenchmark` is load-sensitive; the span probes are not a substitute for it.** Never measure
   right after a build; never compare two numbers taken at different times. Rebuild both arms, measure
   back-to-back, ≥2 passes, nothing else running, and keep a **no-op control arm** (1x, where an
   OS-span change cannot apply — if it moves, the measurement is contaminated).

---

## 1. Where the cost is (8x, drive 0.7, one pedal channel)

| block | Boost | Overdrive | Distortion | can it alias? |
|---|---|---|---|---|
| Stage 1 WDF (two one-port solves) | 18.0 | 18.0 | 18.0 | no — linear |
| drive/bass-cut/warp/HF-trim shelves + `driveMakeup` | 3.4 | 3.4 | 3.4 | no — linear |
| **IC_A rail-sat** (≈4 map + **≈18 ADAA**) | **22.3** | **22.3** | **22.3** | yes |
| = **`processPre`** | **43.7** | **43.7** | **43.5** | |
| WDF clip solve (Stage 2 / SW-1 root / SW-2) + `railDcBlock` | ~7 | ~29 | ~26 | yes |
| IC_B rail + `sw1Ceil` ADAA overhead | 7 | 19 | 6.5 | yes |
| **`injectEvenHarmonic`** (3× `tanh`, 5 one-poles) | **27.5** | **39.7** | **36.6** | yes |
| `odLowShelf` | 1.5 | 7.5 | 1.5 | yes |
| = **`processClip`** | **44.0** | **95.1** | **70.4** | |
| **`processPost`** — Tone (3-port R-type) + Volume, **base rate** | 27 | 27 | 27 | no — linear |
| **channel total** | **114** | **166** | **140** | |

Component costs measured standalone as a cross-check: `Stage1` 18.0, `ToneStage` 24.1, `VolumePot` 5.5
(→ `processPost` 27, adding up). Baseline arms reproduce to ~1 % across passes.

### The headline

**The two EMPIRICAL correction blocks cost more than every real circuit solve combined.** In Boost,
`injectEvenHarmonic` (27.5) + total ADAA overhead (25) = **52.5 ns** against **23 ns** for Stage 1 +
Stage 2. And `IC_A`'s rail-sat — added by v1.4 P9, documented **inert** (44/44 captures within
±0.02 dB, compression unchanged to 0.01 dB) and kept on correctness grounds — costs **more than
Stage 1 itself**.

That is not an argument for deleting them. It is the reason this audit measures before optimising: the
expensive code is not where circuit-modelling intuition puts it.

---

## 2. Step 1 — SHIPPED: `processPost` at the base rate

`MonarchChannel::postAtBaseRate`, with `prepareLinear (rate, postRate)` and the Tone/Volume loop moved
after `processSamplesDown`. Tone + Volume are linear, so they cannot alias; the OS span bought them
only warp accuracy, at 24 % of the per-sample channel cost, paid ×OS.

**CPU −20 % at 4x/8x** (`PerfBenchmark`, idle, both arms rebuilt): 8x Boost 20.4 → 16.3, OD 35.1 →
27.2, Dist 25.8 → 21.6, render 18.6 → 15.0; 2x −11…−17 %. **1x unchanged (4.2 → 4.1)** — the built-in
control arm.

**The null also improved — median −23.1 → −23.4 dB, 34 of 44 deeper, 0 shallower — for the WRONG
REASON.** The gain is entirely HF (2–6 kHz −0.45 dB, 6 kHz+ −0.41, LF flat at −0.02/−0.03). A base-rate
tone stack is *less* faithful to the analog prototype, not more; it droops the top octave, and the
plugin was already slightly hot up there, which is what `hfTrim` exists to trim. So this change quietly
supplied the rest of an **under-fitted** `hfTrim`.

> **⚠️ Consequence: a double-correction trap is armed.** `hfTrim` and `warp*` were both fitted with the
> tone stack warp-free at 4x/8x. Anything re-fitting either must re-fit **with step 1 in place**, or the
> same HF excess is corrected twice. Fifth instance of P7's overlapping-corrections rule.

Don't quote the span table as a plugin-level saving: the oversampler's own up/downsample FIR is
unchanged, so the realisable figure sits below the arithmetic — 20 % measured against ~24 % predicted is
exactly that gap. **Size on `PerfBenchmark`, not on the span table.**

---

## 3. Step 2 — SHIPPED: the zero-coefficient injection branch

In Boost, `injectEvenHarmonic`'s mid/high term is multiplied by `asymBoost`, a **compile-time `0.0`**
(P2 moved Boost's mid/high evens to the asymmetric rails; `asymLowBoost` = −0.017 keeps the *low* path
live). So the gate `tanh` and the multiply-add were computing a value and discarding it, per
oversampled sample.

Hoisting the coefficient out and branching on it: **−6.9 ns of Boost's 44 ns clip span (−16 % of the
span, −4 % of the channel)**; OD/Dist take the branch and are unchanged. Verified **byte-identical** by
`analysis/byte_identity_probe.cpp` over 72 static configs (2 channels × 3 modes × 4 drives × 3 tones)
plus mid-stream mode changes. Nine per-stage gates + all six ctest gates PASS.

### The other half is NOT free, and that is the transferable finding

Skipping `soft`/`meanSq` as well doubles the saving to **−12 ns** — and is *not* byte-identical.
`meanSq` is a 50 ms running mean of `soft²`, **read only when the coefficient is live**, so in Boost its
only job is to be *warm for a later mode switch*. The first `cmp` failed at exactly one byte out of
38 MB: the first sample of the OD segment after a **Boost→OD switch**. The term it lands in is
O(0.03 V) ≈ −31 dB, swelling over 50 ms rather than clicking.

> **Rule: "multiplied by zero" is not the same as "dead" when the branch also maintains state another
> mode reads.** Verify byte-identity across **mid-stream mode changes**, not only steady-state renders
> per mode. This is the second half of the probe's job and the reason it is committed.

The remaining 6 ns is therefore a *judged* change (one `tanh` traded against mode-switch state
coherence), not a free one, and is left unshipped.

---

## 4. Step 2 — REJECTED: rate-gating ADAA

The most attractive lever in the DSP, and the measurement is not close.

ADAA costs **25 ns/sample in Boost, 37 in OD, 25 in Dist — 18–22 % of the channel**. It and
oversampling appear to buy the same thing (suppressed aliasing from a hard-ish knee), so the plugin
looks like it is paying twice: keep ADAA at 1x/2x where it does the work, drop it at 4x/8x, bank 22 %.

`MonarchChannel::setAdaaEnabled` (+ a forwarder on the processor) was added to A/B it, and
**`OSFidelity` section (c1)** measures it through the real oversampler. Boost, drive 0.85, 9 kHz at
0.5 V pk, alias energy summed at the **named fold bins** 3/6/12/15/21 kHz:

| OS | alias, ADAA on | ADAA off | ADAA is worth |
|----|---|---|---|
| 1x | −31.2 dB | +2.1 dB | **33.3 dB** |
| 2x | −38.2 | −7.4 | **30.8** |
| 4x | −62.9 | −17.8 | **45.1** |
| 8x | −68.3 | −22.4 | **45.9** |

**Oversampling does not subsume ADAA at any factor.** At 8x it is still removing ~46 dB of alias energy
the decimation filter does not touch, because at hard clip the rail knee's harmonic series reaches far
past the OS Nyquist. dsp.md's "in addition to oversampling" was right and is now a number.
**Do not re-propose dropping or rate-gating ADAA.**

### ⚠️ The first version of this measurement returned the OPPOSITE answer

Reusing `OSFidelity` (b)'s broadband harmonic-vs-alias residual gave **+0.22 dB at 1x and 2x, −0.05 at
4x, −0.01 at 8x** — i.e. "ADAA does almost nothing, and nothing at all at 4x/8x", which would have
shipped a 22 % saving straight through a 46 dB regression.

The metric was **floor-limited**: (b)'s own alias figure moves only **−47.6 → −48.8 dB from 1x to 8x**,
so it is dominated by things that are not aliasing (DC-block settling, the injection's envelopes,
Goertzel leakage) and cannot resolve a lever that acts on aliasing.

> **Rule: before reading a difference off a metric, check that the metric responds to the axis you are
> asking about.** A floor-limited aggregate reads as "no effect" and is indistinguishable from a real
> null result. Fix: name the fold bins (9 kHz into 48 kHz puts harmonics 3–7 at 27/36/45/54/63 kHz,
> folding to 3/6/12/15/21 kHz, none coinciding with 9 or 18 kHz) and require the metric to
> **self-validate** — alias-on now falls 37 dB across the OS range, which is what proves it is measuring
> aliasing at all.
>
> Same family as P4's marginalisation trap, one level lower: not the wrong aggregation of a good
> instrument, but the wrong **instrument sensitivity**. Corollary: size a safety mechanism at its
> alias-prone operating point (hard clip, HF input), never at a comfortable one.

---

## 5. Step 2 — the by-product: the identity-region droop, quantified

Below the knee `railAntideriv` returns `½x²`, so the ADAA difference quotient is **`(x + x₋₁)/2`** — a
half-sample delay and a `|cos(πf/fs_os)|` one-zero rolloff. Per stage at 16 kHz: **6.02 dB at 1x, 1.25
at 2x, 0.30 at 4x, 0.07 at 8x.** This is arithmetic, not a hypothesis.

**`OSFidelity` (c2)** measures it on the real processor at small signal (the rails never engage, so the
midpoint filter is all that is left). ADAA-off minus ADAA-on, dB:

| OS | 4 kHz | 8 kHz | 12 kHz | 16 kHz |
|----|---|---|---|---|
| 1x | +0.09 | +2.14 | +12.04 | **+24.08** |
| 2x | +0.04 | +0.35 | +2.75 | **+5.00** |
| 4x | +0.01 | +0.15 | +0.67 | **+1.20** |
| 8x | +0.00 | +0.04 | +0.17 | **+0.30** |

**Exactly 4.00× the per-stage prediction at every rate** (24.08/6.02, 5.00/1.25, 1.20/0.30) — and 4 is
the stage count in the small-signal Boost path: **2 op-amp ceilings × 2 series pedal channels**. (In OD
it is 6, with `sw1Ceil`.) The mechanism is confirmed, not merely plausible.

### This corrects a shipped explanation

dsp.md attributes the top-octave deficit to **bilinear warping**. Against `OSFidelity` (a)'s total 1x
deficit vs 8x (−3.61 / −13.12 / −32.52 dB at 8 / 12 / 16 kHz), the droop accounts for **12.04 of the
13.12 dB at 12 kHz and 24.08 of the 32.52 at 16 kHz.** `warp*` was fitted to the combined deficit, so it
has been mostly compensating **ADAA**, not warp — the fourth instance of P7's rule (two corrections
overlapping in band *and* in keying, fitted as one).

**At 2x the droop (5.00 dB at 16 kHz) is LARGER than the net 2x-vs-8x deviation (−3.51 dB)**, i.e.
`warp*` is actively over-correcting to cover it. So an early-out that removes the droop at 2x, shipped
without a refit, over-brightens 16 kHz by ~5 dB.

**That per-rate table is the refit budget the early-out was blocked on.**

---

## 6. Open levers, in order

The gate on all of them: **FR generates the hypothesis, the 44-capture null decides.**

### 6.1 ADAA identity-region early-out — NEXT, and now unblocked

Return `x` when both `x` and `x₋₁` are below the knee, on all three maps (`railSaturateADAA` ×2,
`sw1CeilADAA`). Measured cost saving: **−12 / −13 / −11 ns** (Boost/OD/Dist totals 114 → 102, 166 →
153, 140 → 127), ~−8…−11 %.

It keeps every dB of §4's alias suppression, because that all occurs **above** the knee, and removes
only the droop, which occurs **below** it. In OD it is close to unconditional: the feedback clipper
holds |pin7| ≤ 1.64 V against knees of 2.4/3.6 V, so `railSaturateADAA` there is *only ever* the
midpoint filter.

**It is a voicing change, not an optimisation.** Order, unchanged:
1. Ship the early-out.
2. Re-fit **`warp*` AND `hfTrim` in ONE pass** — §5 for the per-rate budget, §2 for why `hfTrim` is
   under-fitted. Fitting either alone re-creates the double-correction.
3. Judge on the 44-capture null. Expect `OSFidelity` (c2) to go to ~0 by construction; that is a
   check, not the verdict.

### 6.2 Rational `fastTanh` in `injectEvenHarmonic` / `odLowShelf`

Measured **−15 / −16 / −14 ns** (−10…−13 %) with a Padé-7/6 rational clamped at |x| > 4. The terms it
serves are a *fitted empirical* H2 injection at ~−40 dBc and a clip-depth gate, so ~1e-6 of shaper
error is far below anything the fit itself resolves — but it changes the audio, so it is a null
decision. Cheapest remaining win per unit of risk.

Note the two overlap: with `fastTanh` in place the §3 branch skip is worth much less, so measure them
together, not additively.

### 6.3 `processPre` → base rate

The largest remaining oversampled-linear share (Stage 1's 18 ns + 3.4 ns of shelves, paid ×OS). Blocked
historically because Stage 1's gain peak *sweeps* 2.8–5.0 kHz with DRIVE, which is why the 2026-06-29
note rejected a fixed prewarp. **That objection no longer transfers automatically:** `setDrive`
recomputes coefficients per block, so a **drive-dependent** prewarp is available in a way a fixed one
was not. Re-derive before assuming the old rejection holds.

Cheap sub-case, if wanted separately: the shelves are all LTI and commute with Stage 1, so `driveShelf`
could run at the base rate *before* the upsampler. Only 3.4 ns, and it changes the shelves' design rate
(hence their response), so the risk/reward is poor. Not recommended on its own.

### 6.4 Not worth it / do not re-attempt

- **Rate-gating or dropping ADAA** — §4. Costs 33–46 dB of alias suppression at every factor.
- **`DiodeQuality::Good` as an "Eco" setting** — v1.1: less accurate *and* slower in two of three modes
  (Best/Good CPU 0.74× Boost, 1.01× OD, 0.87× Dist), differing by 1.7e-07. `Best` is Wright-Omega and
  lands in ~one step; `Good` iterates. **Never assume the lower-quality setting is the faster one.**
- **The remaining 6 ns of the §3 branch** — costs mode-switch state coherence.

---

## 7. No HQ / Eco button — and the measurements now say so twice

A user-facing quality-vs-CPU control has been proposed twice and rejected twice on measurement: v1.1 on
diode quality (§6.4), v1.5 step 2 on ADAA (§4). What is left is either **free** (shipped in §2/§3), a
**voicing decision to be judged on the null** rather than exposed (§6.1, §6.2), or **the oversampling
factor** — which is already two controls (`oversampling_realtime`, `oversampling_render`) and is the
only setting that measurably changes the sound.

A second lever on the same axis would split it in two. The one thing step 2 changed: 4x and 8x are
**not** overpaying for antialiasing after all, so there is no "economy tier" hiding in there.

If an economy mode is ever wanted anyway, the honest implementation is a preset that sets the live OS
factor to 1x/2x.

---

## 8. Harnesses and how to reproduce

All header-only unless noted — ~1 s per arm, versus a plugin rebuild + 44-capture render.

**`analysis/perf_split_probe.cpp`** — the span split (§1). Times `processPre` / `processClip` /
`processPost` separately at 8x, per clip mode:

```bash
clang++ -std=c++17 -O2 -I. -isystem libs/chowdsp_wdf/include analysis/perf_split_probe.cpp -o analysis/.cache/perf_split_probe && analysis/.cache/perf_split_probe
```

**The variant-header method** — how every block-level and lever number above was obtained. Copy
`src/dsp` to a scratch tree, patch `MonarchChannel.h` (flip a `static constexpr bool`, stub a function
to `return x;`, insert a candidate), and compile the probe with the variant tree's `-I` **first**:

```bash
clang++ -std=c++17 -O2 -I/tmp/variant -I. -isystem libs/chowdsp_wdf/include analysis/perf_split_probe.cpp -o /tmp/split_variant
```

Build **all** arms first, then run them back-to-back, ≥2 passes (§0 rule 2).

> ⚠️ A caveat found while doing this: the `processPre` column wobbles ±10 % **between variant
> binaries** (code layout / i-cache), even though baseline reproduces to ~1 % across runs of the *same*
> binary. Trust the `clip` column's deltas and cross-check any `pre` delta against a standalone
> component timing (as §1 does with `Stage1` alone), rather than reading one arm's `pre` figure
> directly. Two arms whose stub should have been irrelevant to `pre` moved it by 10 ns.

**`analysis/byte_identity_probe.cpp`** — the byte-identity instrument (§3). Full-precision dump over
every mode × drive × tone × channel **plus mid-stream mode changes**; two arms compared with `cmp`.
Usage in its header comment. Locate a differing byte with `sample = (byte − 1) / 8`.

**`tests/OSFidelity`** (JUCE, drives the full processor and the real oversampler) — sections (a) FR vs
8x, (b) harmonic-vs-alias *(⚠️ floor-limited — see §4)*, **(c1) ADAA alias A/B at named fold bins**,
**(c2) ADAA small-signal droop A/B**. ctest gate is finite-only; the dB figures are reported, never
asserted against absolutes.

**`tests/PerfBenchmark`** (JUCE) — the only plugin-level CPU figure, and the source of the README
Performance table. Load-sensitive: §0 rule 2 applies in full.

**`tests/FeatureProfile`** (header-only) — CPU *and* accuracy of a candidate lever together, plus the
guard that the production diode path stays `Best` byte-for-byte.
