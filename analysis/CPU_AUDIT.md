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

**That per-rate table is the refit budget the early-out was blocked on.** Spent in §5b — and it turned
out to be an *under*-estimate of how much of `warp*` was ADAA, because (a) was itself mis-measured.

---

## 5b. Step 3 — SHIPPED: the ADAA identity-region early-out, and the `warp*` refit it forced

`MonarchChannel::adaaIdentityEarlyOut`. When the WHOLE interval [x₋₁, x] lies inside the linear
region, return `x` — on all three maps (`railSaturateADAA` ×2 op-amps, `sw1CeilADAA`). The state pair
is still maintained (F = ½x² there, no transcendental), so the first sample that crosses the knee gets
an exact difference quotient and **nothing above the knee changes** — every dB of §4's alias
suppression is kept, because all of it happens above the knee.

`OSFidelity` (c2) now reads **0.00 dB in every cell**: in the identity region ADAA-on is bit-identical
to ADAA-off, which is the whole claim, verified rather than argued.

### The CPU saving is real but HALF what §6.1 predicted — because it is signal-dependent

Measured with the variant-header method, 5 alternating passes, medians (ns/sample/channel at 8x):

| | pre | clip | post | **total** |
|---|---|---|---|---|
| Boost off → on | 29.7 → 23.2 | 37.4 → 38.0 | 27.0 → 26.9 | **93.8 → 88.3 (−5.5, −5.9 %)** |
| Overdrive | 28.6 → 25.0 | 120.8 → 118.8 | 27.0 → 26.9 | **175.9 → 171.1 (−4.9, −2.8 %)** |
| Distortion | 27.4 → 24.4 | 68.4 → 67.4 | 27.0 → 26.9 | **122.8 → 118.4 (−4.4, −3.6 %)** |

§6.1's **−12 / −13 / −11 ns was an over-estimate**, and the reason is worth keeping: it was derived
from the *total ADAA overhead* figures in §1, which is what you would save if the early-out fired on
every sample. At the probe's operating point (drive 0.7, 0.45 V pk — deliberately hot) it does not:
|nodeG| passes the 2.4 V knee from ~G7 up and pin7 passes `sw1CeilKneeV` = 0.5 V routinely. And below
the knee the ADAA path was never expensive anyway — `railAntideriv` returns ½x² there, with **no
transcendental**, so what the early-out actually removes is a divide and two branches.

**Rule: an early-out's saving is a property of the SIGNAL, not of the code it skips.** Size it at the
operating point, never from the cost of the block it bypasses.

Below plugin level this is ~2–4 %, inside `PerfBenchmark`'s ±2 % reproducibility, so **no
plugin-level figure is claimed for step 3** and the README table is left alone. §0 rule 2 forbids
quoting a single-arm reading against a remembered one.

### The real payoff: `warp*` was 90 % ADAA compensation, and two instruments were invalid

Refit harness: **`analysis/v15_warp_refit.py`** (models `shelfCoeffs` exactly — including the DC
normalization, the ×2 for two series pedal channels, and the fact that the 8x *reference* carries the
shelf too, so the residual is `deficit + 2(S(rate) − S(8x))`).

Two instrument defects had to be fixed before any fit was meaningful, both in `tests/OSFidelity`:

1. **(c2)'s level.** At 0.01 FS with (c1)'s drive 0.85 left set, pin7 still reaches the rail knee at
   4 and 8 kHz, so those two cells were not identity-region measurements at all — which is exactly
   why the shipped table read +0.09 / +2.14 there against an arithmetic 1.21 / 5.00 while matching
   12.04 / 24.08 at 12 / 16 kHz. At 5e-4 the **whole table matches the arithmetic to 0.01 dB**.
2. **(a)'s level, same defect, bigger consequence.** (a) ran at 0.01 FS in Overdrive, and Stage 1's
   gain peaks near 4 kHz, so pin7 sat at ~0.7 V — past `sw1CeilKneeV` — in precisely the presence
   band `warpPivotHz` serves. **The tell was a model/measurement disagreement that no filter-model
   error can produce:** a candidate shelf's measured contribution matched its analytic response to
   **0.01 dB at 4x and 0.17 at 2x, but was off by 1.64 dB at 1x/8 kHz, NON-monotonically in
   frequency.** At 5e-4 the model reproduces every cell.

With the droop removed *and* the instrument linear, the **residual bilinear warp is nearly nothing**:

| (a), no shelf | 4 kHz | 8 kHz | 12 kHz | 16 kHz |
|---|---|---|---|---|
| 1x | +0.12 | −0.14 | −0.86 | **−3.08** |
| 2x | +0.03 | −0.03 | −0.16 | −0.44 |
| 4x | +0.01 | −0.01 | −0.03 | −0.08 |

So the shelf that was correcting a 32 dB deficit at 1x/16 kHz has ~3 dB left to do, and **nothing at
all below 12 kHz at any rate**. Refit (weighted to the presence band; constrained to vanish at 8x so
the accuracy reference is not moved; constrained so the prewarped pole stays inside Nyquist):

`warpScaleDb` **10.6 → 1.0**, `warpExp` **2.20 → 1.80**, `warpPivotHz` **6500 → 17000**,
`warpMaxDb` **3.0 → 1.0**, plus a new `warpPoleMaxFrac` guard.

Before → after, both measured at the corrected level (dB vs 8x):

| | 4 kHz | 8 kHz | 12 kHz | 16 kHz |
|---|---|---|---|---|
| 1x before | +0.05 | −3.46 | −13.21 | −32.59 |
| **1x after** | +0.21 | +0.28 | +0.22 | **−0.75** |
| 2x before | +0.84 | +1.02 | −0.47 | −3.51 |
| **2x after** | +0.06 | +0.08 | +0.05 | **−0.14** |
| 4x before | +0.14 | +0.14 | −0.17 | −0.75 |
| **4x after** | +0.01 | +0.02 | +0.01 | **−0.02** |

**2x is the LIVE default and 4x the RENDER default**, so this is the live-vs-bounce agreement the
06-30 recalibration was aiming at and only half achieved: 2x went from ~1 dB hot through the presence
band and 3.5 dB short at 16 kHz to **within 0.14 dB everywhere**.

The pivot move is not taste. The old moderate 6.5 kHz pivot existed because the deficit it was fitted
to *started* in the presence band — and that part of the deficit was the ADAA droop, whose `|cos|`
shape does start low. Real bilinear warp is confined to the top octave, which is what a high pivot
fits. The price is a Nyquist hazard the old pivot never had: `shelfCoeffs` prewarps the pole to
`pivot·√ghi`, which at 17 kHz would cross π/2 at 1x on a 32 kHz session and hand back an **unstable**
filter. Guarded by sliding the pivot down with the rate (`warpPoleMaxFrac` = 0.42) — inert at 44.1 and
48 kHz, binding at 32 kHz (17.0 → 12.7 kHz). Verified finite and bounded over
{22.05, 32, 44.1, 48, 88.2, 96} kHz × {1,2,4,8}x.

### The arbiter: NEUTRAL, and that was the prediction

44 captures: median **−23.45 → −23.45**, mean +0.01 dB, **10 deeper / 11 shallower / 23 unchanged**,
largest move ±0.3 dB, range −27.0…−8.6 → −26.9…−8.6. FR rms median 2.21 → 2.29.

This is the expected result, not a disappointment: the null is rendered at 4x, and below 8 kHz the 4x
path moved by ≤0.17 dB — everything the change did lives at 1x/2x, or above 8 kHz where the captures
carry ±18 dB of spread and are not fit-worthy. **A null-neutral result is what a change to an
internal-consistency axis should look like**; if it had moved the null much, that would have meant the
refit was absorbing a capture-accuracy error it had no business touching.

### `hfTrim` was measured in the same pass and DELIBERATELY left at −1.3 dB

§2 armed this: fit either of `hfTrim` / `warp*` alone and the same HF excess gets corrected twice. So
both were put on the bench together. The two instruments **disagree in sign**, and both effects are
smaller than either resolves:

* Clean-sweep tilt (`shape_audit.py clean`, the least-nonlinear instrument, valid G2–G6): Boost reads
  −0.76 / −0.66 / −0.13 / −0.36 / −0.08 dB across 80–5120 Hz — the plugin is ~0.4 dB **dark**, i.e.
  wants *less* trim.
* The arbiter (`offline_null_probe null`, all 176 rows): +0.3 dB of HF is **+0.046 dB worse**, +0.6 dB
  is +0.115 worse, and −0.3 dB is 0.023 dB *better* — the null wants *more* trim.

That is `offline_null_probe`'s own standing caveat 2 (**driven-sweep nulls reward dulling** — the null
is partly matching distortion products, so an HF cut flatters it), and the standing rule is that when
the null and the clean sweep disagree about HF, the clean sweep is believed. Believing the clean sweep
here would buy ~0.4 dB of tilt at G2 while the arbiter says it costs 0.05 dB; believing the null would
be fitting a known bias. **Both are inside the noise, so neither moves, and the reason is recorded so
the next pass does not re-derive it.** `hfTrim` = −1.3 dB @ 4.5 kHz, unchanged since 2026-07-04.

---

## 5c. Step 4 — SHIPPED: rational `fastTanh` in `injectEvenHarmonic` / `odLowShelf` (2026-07-30)

`MonarchChannel::fastTanh` — a Padé [7/6] rational (`x(135135+x²(17325+x²(378+x²))) /
(135135+x²(62370+x²(3150+28x²)))`), clamped to exactly ±1 beyond |x| ≥ 4.97 (the raw polynomial's
denominator has lower degree than its numerator, so past ~5 it diverges unbounded rather than
saturating). Matches `std::tanh` to **<1.2e-5 abs error over [0,4]** and **<1e-4 near the clamp** —
several orders of magnitude tighter than the ~1 % (−40 dBc) precision the terms it feeds were fitted
to. Swapped into all **four** `std::tanh` calls in `injectEvenHarmonic` (`soft`, the mid-band gate's
`k`, `softLow`) and `odLowShelf` (`gate`) — the fitted empirical even-harmonic injection and the
OD clip-depth gate. **Not** used in `railSaturate` or `sw1Ceil`, whose ADAA antiderivative is the
exact closed-form `log(cosh)` of `std::tanh` and would need re-deriving to match an approximation.

**Measured with the same before/after protocol as §5b** (git-stash the header, rebuild
`perf_split_probe` and `PedalRender` for each arm, idle machine, back-to-back, ≥2 runs). `pre`/`post`
spans are the built-in control — both unchanged between arms, confirming the change is isolated to
`clip`, where it lives:

| mode | clip before | clip after | Δ clip | channel total Δ |
|------|------------|-----------|--------|-----------------|
| Boost | 37.6 ns | 27.6 ns | **−10.0 ns** | −9.9 ns (−11 %) |
| Overdrive | 118.0 ns | 86.6 ns | **−31.4 ns** | −29.5 ns (−17 %) |
| Distortion | 67.1 ns | 56.6 ns | **−10.5 ns** | −9.9 ns (−8 %) |

**Bigger than the original −15/−16/−14 ns estimate, and unevenly so — because the call count is
mode-gated, not fixed.** `soft`/`softLow` run in every mode (2 calls); Distortion's `k` gate adds a
third (`kMid = asymDist ≠ 0`); Overdrive adds both `k` (`asymOD ≠ 0`) and the `odLowShelf` gate
(`sw1On`), for four. That ranks Overdrive > Distortion ≈ Boost, which is what was measured — the
uniform prior estimate didn't account for the per-mode branch pattern §3 already established.

**Confirmed at plugin level with `PerfBenchmark`** (idle, both arms rebuilt, back-to-back, 2 runs
each, latencies unchanged — the same protocol as §2). Every OS×mode cell dropped, well outside the
±2 % noise floor:

| OS | Boost before→after | Overdrive before→after | Distortion before→after |
|----|--------------------|-------------------------|--------------------------|
| 1x | 2.5 → 2.25 % | 3.8 → 3.65 % | 3.0 → 2.7 % |
| 2x | 4.2 → 3.75 % | 7.25 → 6.25 % | 5.55 → 5.05 % |
| 4x | 8.05 → 7.1 % | 14.15 → 12.05 % | 10.8 → 9.7 % |
| 8x | 15.6 → 13.6 % | 27.8 → 23.45 % | 21.1 → 18.95 % |
| render | — | — | 15.45 → 13.3 % |

README's Performance table updated to match (1x unchanged, 2x ~4–6 %, 4x ~7–12 %, 8x ~14–23 %,
render ~13 %).

### The arbiter: BIT-IDENTICAL at reported precision, not merely neutral

Two checks, both against the same before/after `PedalRender` rebuild used for the CPU measurement:

* **44-capture null** (`run_validation.py`): median **−23.4 → −23.4 dB**, range −26.9…−8.6 →
  −26.9…−8.6, **every one of the 44 captures identical to 0.1 dB**.
* **Even-harmonic series** (`fr_thd_audit.py evens`, the table P3/P3.1 fitted through these exact
  `tanh` calls): H2/H4/H6 rms and bias identical to 0.1 dB in all three modes, silent-cell count
  unchanged (0 in all nine rows).

Expected, given the error budget above sits ~40+ dB below what either instrument resolves — but
worth stating plainly: this is not "small enough to accept," the two 44-capture renders are
indistinguishable by the project's own arbiters.

---

## 5d. Step 5 — Stage 1 at the base rate (2026-07-30). Null-NEUTRAL and the last big linear span,
but it leaves a measured top-octave deficit whose correction is still an open decision

`MonarchChannel::preAtBaseRate` + `processStage1` / `processPreOs`. §2's rule applied to the other
linear span: Stage 1 is a linear WDF, so it cannot alias, and the OS span only ever bought it a
smaller bilinear warp — at **18.0 ns/sample paid ×OS**, the largest such block in §1.

**Only Stage 1 moves, and that is forced, not chosen.** `processPre`'s other two blocks are
IC_A's rail-sat — a genuine nonlinearity, NodeG reaching 2.36–5.93 V against a +3.9/−2.7 V ceiling
from G6 up — and `driveShelf`, which sits *downstream* of it. So the only admissible cut is
`Stage 1 | [rail-sat → driveMakeup → driveShelf]`. `processPre` still composes both halves, so every
single-rate caller (1x, the per-stage tests, the `analysis/` probes, `processSample`) is bit-identical
to before the split, and `PluginProcessor` is the only caller that separates them.

### The gate, run BEFORE building anything: `analysis/v15_stage1_warp_probe.cpp`

Validated against the shipped `Stage1_FreqResponse` gate to **0.01 dB and 18 Hz** (it reports 12.66 dB
@ 6340 Hz at 2x; the probe 12.66 @ 6322) — so it is the same instrument, not a new one. Cost of the
change, Stage 1 at 48 kHz vs Stage 1 at the span's rate, **one** pedal channel:

| drive | 8 kHz | 12 kHz | 16 kHz |
|---|---|---|---|
| 0.2 | +0.01 | −0.04 | −0.24 |
| 0.5 | −0.03 | −0.29 | −1.21 |
| 0.7 | −0.08 | −0.51 | −1.86 |
| 1.0 | −0.16 | −0.84 | **−2.58** |

**≤0.02 dB at and below 6 kHz at every drive and every rate.** The 1x row is the built-in control —
at 1x there is no OS span, so the change must be a no-op, and it reads 0.00 in every cell.

**The peak is NOT what moves**, which retires the on-record blocker. §6.3 recorded the objection as
"Stage 1's gain peak sweeps 2.8–5.0 kHz with DRIVE, so no fixed prewarp can correct it." Measured, the
peak's **gain is identical to 0.01 dB** at every rate and drive and its frequency moves **≤0.062
octaves**. There was never anything there for a prewarp to chase.

**What is drive-dependent is the deficit's DEPTH, and the circuit says why.** Z_upper is
`R_leg ∥ C2(100 pF)`, so C2's corner is `1/(2π R_leg C2)`: **75.8 kHz at drive 0.2, 31.2 at 0.5,
15.8 at 1.0** — it walks *into* the top octave as DRIVE rises. The deficit is C2's own rolloff being
bilinear-warped, not a diffuse "warp".

**Confirmed on the real processor, three independent routes agreeing to 0.02 dB.** `OSFidelity` (a)'s
1x row rose **+0.10 / +0.77 / +2.98 dB** at 8/12/16 kHz. Predicted from the probe: Yellow@0.5
(−0.03/−0.29/−1.21) **plus Red**, whose 17.7 k floor makes it ≈ Yellow@0.67 (−0.07/−0.47/−1.75), two
channels in series → **−0.10/−0.76/−2.96**. (⚠ The flat **+0.15 dB** that (a) shows at 1x across
100 Hz–4 kHz is **pre-existing** — present in the baseline arm too. It was nearly mis-attributed to
this change by reading §5b's *documented* 8/12/16 kHz figures instead of rebuilding the baseline. §0
rule 2 applies to fidelity numbers, not just CPU ones.)

### ❌ REJECTED on measurement: prewarping C2 (`analysis/v15_stage1_warp_probe.cpp` + variant header)

The obvious "fix it at source" move, in the spirit of §5b — and it has real appeal: choose
`C2' = 1/(R_leg·K·tan(ω₀/K))` so C2's corner lands at the right *digital* frequency, computed in
`setDrive`, **zero fitted parameters**. It does not work:

| drive | 16 kHz before | C2-prewarped |
|---|---|---|
| 0.2 | −0.24 | −0.13 |
| 0.5 | −1.21 | **+0.61** |
| 0.7 | −1.86 | **+1.37** |
| 1.0 | −2.58 | −0.10 |

`|max|` improves only 2.58 → 1.37 dB, and it buys that by turning a **consistent, monotone** deficit
into a **sign-inconsistent, non-monotone** one — much harder for any downstream shelf to absorb. Worse,
it **displaces the peak**: `d_oct` ≤0.062 → **0.30–0.55 octaves**, peak gain ≤0.01 → **+0.30 dB**.
Matching one corner does not match `Av = 1 + Z_upper/Z_lower`.

> **The rule it adds, and it inverts the objection it was testing.** The 2026-06-29 note said a prewarp
> fails *because* the peak sweeps with drive. The truth is the reverse: the peak was already
> **rate-immune**, and **prewarping is what moves it**. A single-element prewarp is not a free
> "fix at source" — it is a one-frequency fit inside a composite transfer function, and it pays for the
> corner it pins with error everywhere else.

### The arbiter: NEUTRAL

44 captures (`run_validation.py --captures analysis/pedal_export2`): median **−23.45 → −23.45 dB**,
range −26.9…−8.6 → **−27.0…−8.6**, mean **+0.002 dB**, **9 deeper / 26 unchanged / 9 shallower**,
largest move ±0.2 dB either way. Nine per-stage gates + all six ctest gates PASS.

Per-band null is where the change becomes visible, and it is confined to exactly one band:

| band | mean Δ | deeper / shallower / same |
|---|---|---|
| 100–300 Hz | −0.007 | 6 / 6 / 32 |
| 300 Hz–1 k | −0.036 | 6 / 5 / 33 |
| 1–2 k | +0.159 | 11 / 8 / 25 |
| 2–6 k | +0.048 | 18 / 15 / 11 |
| **6 k+** | **+0.141** | **11 / 28 / 5** |

`6k+` is the only band where more captures got *shallower* than deeper — the expected signature, and
nothing below 6 kHz moved.

> ⚠ **`run_validation.py`'s "FR RMS err" moved and it is the wrong instrument to read here** — mean
> **+0.266 dB**, worst **+2.54** (G10 T5 Dist). `analyze.frequency_response` averages third-octave
> centres from **25 Hz to 20 kHz with equal weight**, so **4 of its 30 bands (10k/12.5k/16k/20k) sit
> above the ~8 kHz limit** where the captures carry ±18 dB of spread — and G10 is exactly where the
> measured deficit is deepest. The probe puts the change at ≤0.32 dB at 8 kHz for both channels
> combined, which cannot produce a 2.54 dB rms move; the top four bands can. The per-band **null** is
> the arbiter and it says the trustworthy region did not move. (A trust-band-restricted FR
> recomputation was **not** run — it needs before-arm renders.)

### ⚠️ What is NOT settled, and why nothing is claimed as finished

1. **`warp*` structurally CANNOT correct this, and that is the load-bearing discovery.**
   `v15_warp_refit.py`'s fit is **constrained to vanish at 8x** — deliberately, so the shelf cannot
   re-voice the render path while buying the low factors. But Stage 1 now runs at the base rate at
   **every** factor, so this deficit is present **at 8x too**. `warp*` corrects *rate disagreement*;
   this needs a **rate-independent, drive-keyed** term. Different instrument, same band → P7's
   overlapping-corrections rule applies, and `hfTrim` (4.5 kHz, fixed) is in that band as well, so any
   fit has to put all three on the bench in one pass. **Seventh instance.**
2. **The plugin-level CPU figure is NOT measured.** Channel arithmetic from the measured 18.0 ns,
   per output frame as `N×(pre+clip)+post` (§1's "channel total" is a mixed-unit per-sample sum and
   must not be used for this):

   | OS | Boost | Overdrive | Distortion |
   |----|-------|-----------|------------|
   | 8x | −126 ns = **17.3 %** | **11.1 %** | **13.4 %** |
   | 4x | −54 ns = **14.3 %** | **9.3 %** | **11.2 %** |
   | 2x | −18 ns = **8.9 %** | **5.9 %** | **7.1 %** |

   Realisation ran ~83 % in §2 (20 % delivered against 24 % predicted) because the oversampler's own
   FIR is untouched, so expect ~14/9/11 % at 8x. **The first attempt to measure it was discarded**:
   run immediately after a build, it returned 8x Overdrive *below* 8x Boost and a render row swinging
   11.4 → 40.3 → 13.9 % within one arm. Interleaving the arms does **not** rescue this — §0 rule 2's
   hazard is random contention, not monotone drift.

**Status: superseded by §5e**, which resolved both (1) and (2) — the drive-keyed term was fitted, and
the plugin-level measurement was subsequently taken (2026-07-30, see §5e). The measured 8x figures
(8.6/18.8/12.1 %) beat the arithmetic prediction (11.7/21.3/16.9 %), likely because retiring
`warp*` also removes its per-OS-sample shelf computation.

---

## 5e. Step 5, part 2 — `warp*` RETIRED and replaced by a drive-keyed shelf. The headline is not the
shelf: it is that Stage 1 was ~97 % of all the bilinear warp in the plugin, so a *rate*-keyed
correction was never the right instrument

`MonarchChannel::s1Warp*` + `updateS1Warp`, with `warpScaleDb` → 0 behind a derived
`warpShelfEnabled`. §5d left one decision open; this is it, taken and measured.

### Why `warp*` had to go, and why it is a retirement rather than a refit

`warp*` is keyed to the **OS rate**. Step 5 moved Stage 1 to the **base** rate, which removes the
rate-dependence *at source* — and Stage 1 turns out to have been essentially all of it. With Stage 1
at the base rate and this shelf **disabled**, `OSFidelity` (a) reads 1x −0.18 / −0.24 / −0.26 dB at
8/12/16 kHz (was −0.29 / −1.01 / −3.23), 2x ≤0.06. So the factors already agree to a quarter of a dB
with **no shelf at all** — better than the ±0.28/−0.75 that shipped *with* the shelf in step 3.
Leaving it in is then a pure over-correction: measured, **1x reads +2.23 dB ABOVE 8x** at 16 kHz.

> **The rule: when a correction's keying is removed at source, the correction does not need retuning —
> it needs deleting.** Step 3 refit this shelf 10.6 → 1.0 dB after the ADAA droop was removed, and the
> instinct here was to refit it again. That is wrong twice over: its *key* (rate) no longer indexes the
> defect at all, and a refit would have kept a live instrument in a band where two others already sit.
> **Fourth time a `warp*` number has been attributed to the wrong mechanism** (06-30 fit, step 3's ADAA
> collision, step 3's above-the-knee instrument, and now its keying).

### What replaced it, and why the drive axis is the circuit's own

The defect did not disappear when the rate-dependence did — it became **absolute** (present at every
factor, render included) and **drive**-keyed. Z_upper is `R_leg ∥ C2(100 pF)`, so C2's corner
`1/(2π·R_leg·C2)` runs **75.8 kHz at G2 → 15.8 kHz at G10**: it walks *into* the top octave as DRIVE
rises, and bilinear-warping a corner that near Nyquist is the whole mechanism.

`s1WarpPivotHz` = 16 kHz, `s1WarpLift0` = **0.40**, DC-normalized, applied **inside `processStage1` at
the base rate** — not in `driveShelf`, which runs at the OS rate. A correction for Stage 1's own warp
has to live at Stage 1's rate.

* **Keyed on `R_leg`, not on the knob — which makes it right on RED for free.** Every other drive-keyed
  instrument here (`bassCut*`, `bassBoost*`, `driveMakeup`) is knob-keyed and Yellow-fitted, which is
  what dsp.md's standing "Red drive-shelf keying" note is about. This law reads Red's 17.7 k floor
  directly, so **one expression covers both channels**, and the fit was scored over both at once. It
  does not add to that deferred mismatch.
* **Zero fitted shape.** The warp of a one-pole at `fc` read at the pivot is closed form;
  `s1WarpLift0` only rescales it to the composite `Av = 1 + Z_upper/Z_lower`. **Verified: the shipped
  C++ matches the analytic law to 0.01 dB in all 24 (channel × rate × drive) cells.**
* **The target is EXACT, and this is the one EQ fit in the project where FR both generates *and*
  decides.** `v15_stage1_warp_probe fit` emits base-rate vs an **8x-of-base solve of the same filter**
  (own residual warp ~1/64 of what is measured): no captures, no NAM model, no noise. The null renders
  at 4x and the effect is above 8 kHz where the captures carry ±18 dB — its only job is to confirm
  nothing *else* moved. Harness `analysis/v15_s1warp_fit.py`.

### ⚠️ 0.40 is deliberately below the harness's own optimum (0.545) — a metric-weighting trap

The harness weights 4–11 kHz at 3–4× to protect the presence band. But the raw deficit there is already
~0, so that weighting **credits the 14–16 kHz repair almost nothing while charging full price for
presence-band over-correction**: its weighted rms only moves 0.329 → 0.262 dB, which reads as "the
shelf barely helps". Decomposed into cells it is nothing like that. Series pair (Yellow+Red), 48 kHz:

| `lift0` | 6–10 kHz cost | 16 kHz residual, G2→G10 |
|---|---|---|
| 0 (no shelf) | — | −1.0 … −5.4 |
| **0.40 (shipped)** | **≤ +0.23** | **−0.4 … −2.7** |
| 0.545 (weighted-best) | +0.30 … +0.40 | −0.2 … −1.7 |

0.545 spends 0.3–0.4 dB of presence band to buy the last 20 %. At 0.40 the 6–10 kHz residual stays
below what any instrument here resolves, so it **cannot be double-corrected later** by `hfTrim`
(4.5 kHz) or a P7 refit — the two things in that band. **Aggregate is a screen, cells are the verdict**
(P10 step 3's rule), and the weighting itself is the fifth instance of the marginalisation family.

### ⚠️⚠️ A real instability was found and fixed BEFORE shipping — and the gate written for it was
verified BLIND. This is the transferable part of step 5.

The lift law contains `tan(π·pivot/rate)`. On a **32 kHz** session the 16 kHz pivot lands exactly on
Nyquist, that tan diverges, and **Stage 1's output reaches ~5e6**. Cause: the pivot was clamped only
*after* the lift was computed — which is how the retired `warp*` did it, safely, because *its* lift came
from a rate power law that never touches tan. **Copying a clamp's placement instead of its reasoning is
what reproduced this.** Fixed with two clamps: a rate-only `pivot0 = min(pivot, 0.42·rate)` *before* the
lift, then the existing pole guard after. Both are inert at 44.1/48/88.2/96 kHz — all 24 fitted cells
re-verified bit-identical, so nothing that was fitted moved, and the in-flight null render stayed valid.

**The gate is the lesson.** A session-rate × OS-factor sweep was added to `ControlSweep` for this, and
then tested against the broken arm: it **PASSED**. The plugin's *output* never moves when Stage 1
blows up, because the diode clipper clamps ±1.64 V — output stays ~0.26 while the node is at 5e6.

> **An output-bounded check cannot see a blowup upstream of a clipper. The clipper is a perfect mask.**
> So the bound has to be asserted on the **node**, and it now is: `FullChain_DualChannel` reads NodeG
> over {22.05, 32, 44.1, 48, 88.2, 96} kHz × {1,2,4,8}x × 11 drives × both channels (worst |NodeG| =
> **2.89 V** good arm, **6.03e6** broken → FAIL). On the broken arm *every other check in that suite
> still reported "ok"* — the mask made visible. Two prior audit items claimed "verified finite over
> {22.05…96} kHz × {1,2,4,8}x" **by hand and left no gate behind**; this is that check made permanent.
> Same family as `OSFidelity`'s three invalid-instrument findings, one level lower again: not the wrong
> aggregation, not the wrong operating point — **the wrong observable**.

The `ControlSweep` rate sweep was kept anyway: it is cheap and it is real added coverage (nothing else
there ran at any rate but 48 kHz), but it is **not** the guard for this defect and must not be read as
one.

### The arbiter: NEUTRAL, as designed

44 captures. **vs step 5 bare:** median **−23.45 → −23.45**, mean **+0.011 dB**, 2 deeper / 36
unchanged / 6 shallower, largest move ±0.2 dB. FR rms err mean **−0.200 dB** (i.e. FR improved, the
expected direction). **vs the committed pre-step-5 baseline:** median **−23.45 → −23.45**, range
−26.9…−8.6 → **−27.0…−8.6**, mean +0.014 dB, 8 deeper / 25 unchanged / 11 shallower.

Per-band vs committed, the change is confined to where it should be: 100–300 Hz **−0.025**, 300 Hz–1 k
−0.025, 1–2 k +0.052, 2–6 k +0.034, **6 k+ +0.214** (29 of 44 shallower) — the residual top-octave
darkening, registering only in the band the captures cannot arbitrate. Nine per-stage gates + all six
ctest gates PASS.

> **A null-neutral result is the correct outcome here**, for the same reason as step 3: this is an
> internal-consistency axis measured against an exact reference. Had the null moved much, the fit would
> have been absorbing a capture-accuracy error it has no business touching.

### Plugin-level CPU, measured 2026-07-30

`PerfBenchmark` (§0 rule 2 protocol: idle, both arms rebuilt, back-to-back, ≥2 passes). The
arithmetic prediction was ~14/9/11 % at 8x; the realized figures are larger, likely because
retiring `warp*` (rate-keyed shelf computation at every OS sample) saves more than Stage 1's
span alone:

| OS | Boost | Overdrive | Distortion |
|----|-------|-----------|------------|
| 1x | 2.1 % | 3.8 % | 2.8 % |
| 2x | 2.8 % | 5.3 % | 3.6 % |
| 4x | 4.8 % | 9.8 % | 6.5 % |
| 8x | 8.6 % | 18.8 % | 12.1 % |
| render | — | — | 11.1 % |

1x is the built-in control — Boost moved 2.25 → 2.1 % (step 4 → step 5), within the ±2 %
noise floor. All OS factors ≥2× show clear improvement. README's Performance table updated
from these figures.

**`hfTrim` was left at −1.3 dB** and was *not* re-fit: the shipped shelf's contribution
at 4.5 kHz is ~+0.08 dB, an order below what either instrument resolves, so P7's joint-fit rule is
satisfied by inspection rather than by a refit.

---

## 6. Open levers, in order

The gate on all of them: **FR generates the hypothesis, the 44-capture null decides.**

### 6.3 `processPre` → base rate — **✅ SHIPPED as step 5. See §5d (the move) and §5e (the EQ).**

Superseded by §5d, which corrects this entry's framing on two counts:

* **Only Stage 1 can move**, not `processPre` — IC_A's rail-sat is a nonlinearity and `driveShelf` is
  downstream of it. So the prize is **18.0 ns**, not the span's 43.7.
* **The "peak sweeps with DRIVE" objection is retired**, but not the way this entry expected. The peak
  turns out to be **rate-immune** (≤0.062 oct, ≤0.01 dB), so no prewarp was needed for it — and a C2
  prewarp, the drive-dependent one this entry invited, was built and **rejected**: it is what *creates*
  peak displacement (0.30–0.55 oct). §5d.

Both parts shipped and **null-neutral** (median −23.45 unchanged either way). The decision this entry
left open was resolved in §5e: the drive-keyed term was fitted and `warp*` **retired** — it could not
have done the job, since its fit vanishes at 8x while this deficit is present at 8x, and more
fundamentally its *key* (rate) no longer indexes the defect. `hfTrim` stays at −1.3 dB (the new shelf
contributes ~+0.08 dB at 4.5 kHz, below what any instrument resolves). **Plugin-level CPU measured 2026-07-30** at 8.6/18.8/12.1 % (Boost/OD/Dist) — see §5e for the
full `PerfBenchmark` table. The arithmetic prediction was ~14/9/11 %; the realised savings are
larger, likely because retiring `warp*` also removes its per-OS-sample shelf computation.

Cheap sub-case, still untouched: the shelves are LTI and commute with Stage 1, so `driveShelf` could
also run at the base rate. Only 3.4 ns, and it changes the shelves' design rate (hence their response),
so the risk/reward is poor. Not recommended on its own.

### 6.4 Not worth it / do not re-attempt

- **Rate-gating or dropping ADAA** — §4. Costs 33–46 dB of alias suppression at every factor.
- **`DiodeQuality::Good` as an "Eco" setting** — v1.1: less accurate *and* slower in two of three modes
  (Best/Good CPU 0.74× Boost, 1.01× OD, 0.87× Dist), differing by 1.7e-07. `Best` is Wright-Omega and
  lands in ~one step; `Good` iterates. **Never assume the lower-quality setting is the faster one.**
- **The remaining 6 ns of the §3 branch** — costs mode-switch state coherence.

---

## 7. No HQ / Eco button — and the measurements now say so twice

A user-facing quality-vs-CPU control has been proposed twice and rejected twice on measurement: v1.1 on
diode quality (§6.4), v1.5 step 2 on ADAA (§4). What is left is either **free** (shipped in §2/§3/§5c),
a **voicing decision to be judged on the null** rather than exposed (§6.3), or **the oversampling
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

> ⚠️ **Three of this file's four sections have now been caught reading an invalid instrument, each in
> a different way — (b) floor-limited (§4), (c2) and (a) both measured above the clip knee (§5b). The
> level in (a) and (c2) is 5e-4 and it is load-bearing: raise it and the "small-signal FR" is being
> read through the soft clipper.** Before believing any number here, check the section responds to the
> axis it claims to measure — (c1) self-validates (alias-on must fall ~37 dB across the OS range),
> (a)/(c2) validate against the analytic shelf/`|cos|` models, which they now match to 0.01 dB.

**`analysis/v15_warp_refit.py`** — the `warp*` fit (§5b). Models `shelfCoeffs` exactly, including the
DC normalization, the ×2 for two series pedal channels, and the 8x reference carrying the shelf too.
Feed it a fresh `warpScaleDb = 0` run of (a) and it grid-searches (scale, exp, pivot, cap) under two
constraints that are not optional: the shelf must **vanish at 8x** (or it moves the accuracy
reference), and the prewarped pole must stay inside Nyquist at the lowest session rate.

**`tests/PerfBenchmark`** (JUCE) — the only plugin-level CPU figure, and the source of the README
Performance table. Load-sensitive: §0 rule 2 applies in full.

**`tests/FeatureProfile`** (header-only) — CPU *and* accuracy of a candidate lever together, plus the
guard that the production diode path stays `Best` byte-for-byte.
