# FR / THD / Harmonic Audit — plugin vs real King of Tone captures

Audit date **2026-07-26**, against the 44 NAM captures (`analysis/pedal_export2/`, local-only) via
`analysis/reports/comprehensive_data.json` (OS 8×, 30 third-octave bands, 4 sweep levels).
Regenerate every table below with `analysis/fr_thd_audit.py` (see the last section).

Started from four shortcomings spotted by eye in the comprehensive dashboard. **Three survived
scrutiny, one was largely a measurement artifact, and one turned out to be a symptom of the other
two rather than an independent defect.** Findings first, then the work plan.

Sign convention throughout: **plugin − pedal, dB, normalized at 1 kHz.** Negative = the plugin is
short of the real pedal.

---

## Verdicts on the four observations

| # | Observation | Verdict |
|---|---|---|
| 1 | Universally light 20–80 Hz | **Confirmed** — largest defect in the plugin; present in the raw circuit at every drive. *(2026-07-28: confirmed real, but CLOSED as not correctable — the excess carries a phase lead, so every minimum-phase EQ that fixes the magnitude worsens the null. See P1.)* |
| 2 | A touch hot from 800 Hz up; maybe knob variance | **Confirmed as real, rejected as knob variance** — it does not track the TONE knob at all |
| 3 | The FR peak moves and the plugin doesn't match it | **Confirmed, and it WAS a separate mechanism.** Both earlier verdicts here were wrong: "a readout of #1/#2" (this row, from the ±0.2 oct *mean*) and "mode-differentiated" (P6's first framing, from per-capture signs). It is a mode-independent **gain-vs-DRIVE-knob deficit above G5** — fixed in P6 by `driveMakeup` |
| 4 | THD fine overall, clean harmonics off, no 6–8.5 kHz THD | **Split** — the 6–8.5 kHz gap is mostly a measurement artifact; the clean/even-harmonic problem is real and much bigger than it looked |

---

## Finding 1 — sub-64 Hz shortfall (the big one)

Present in every capture, every mode, every sweep level. At mid drive (0.5–0.7, where the bass-cut
bell is already off and the high-drive bloom is small):

| Hz | 20 | 25 | 32 | 40 | 50 | 64 | 80 | 101 | 127–640 |
|---|---|---|---|---|---|---|---|---|---|
| Boost | −2.03 | −1.94 | −1.80 | −1.47 | −0.98 | −0.44 | +0.05 | +0.42 | ±0.7 |
| Overdrive | −3.87 | −3.72 | −3.52 | −3.17 | −2.64 | −2.11 | −1.62 | −1.27 | −1.0 |
| Distortion | −2.86 | −2.69 | −2.48 | −2.07 | −1.50 | −0.90 | −0.33 | +0.08 | ±0.4 |

**The decisive test was stripping the correction shelves.** `fr_thd_audit.py raw` evaluates
`MonarchChannel::driveShelf()`'s exact coefficients (parsed live from the header, at the report's
384 kHz OS rate) and removes them from the measured plugin FR, leaving the **raw WDF circuit** vs
the pedal:

```
   band      G2      G3      G4      G5      G6      G7      G8     G10
     20   -2.81   -3.44   -4.38   -5.05   -5.43   -5.41   -5.77   -5.69
     32   -1.65   -2.40   -3.49   -4.43   -5.16   -5.38   -5.96   -6.35
     50   +0.42   -0.49   -1.78   -3.03   -4.23   -4.82   -5.95   -7.12
     80   +2.50   +1.48   +0.09   -1.37   -2.88   -3.71   -5.19   -6.86
    160   +3.97   +2.96   +1.55   -0.05   -1.59   -2.35   -3.45   -5.22
    320   +3.08   +2.25   +1.09   -0.17   -1.00   -1.27   -1.70   -3.04
    640   +1.01   +0.68   +0.26   -0.22   -0.32   -0.44   -0.72   -0.98
```

Three things fall out of that grid:

- **At G5 the raw circuit matches the pedal to ±0.3 dB from 100 Hz to 800 Hz** — and is still
  3–5 dB short below 64 Hz. So the low mids are already right and the deficit is specifically at
  the bottom.
- **The 20 Hz row is 2.8–5.9 dB short at *every* drive.** This is a drive-independent gap.
- The G2 low-mid bump (+4 dB at 130–200 Hz) that `bassCut` exists to remove is confirmed real and
  correctly targeted; the G8–G10 broad deficit that `bassBoost` targets is confirmed too, and is
  still 5–7 dB under-served at 40–80 Hz.

Nothing in the current chain addresses 20–64 Hz: `bassPivotHz` is 105 Hz, drive-gated, and capped
at 4.2 dB; `bassCutPivotHz` is 185 Hz and works the other direction.

**Shape of the fix.** Because 100–800 Hz already matches at G5, this is *not* a high-pass corner
error — lowering the dominant LF pole would lift 100–200 Hz too and break a band that is currently
correct. What's needed is a pole/zero pair with its knee around **45–55 Hz**, roughly **+3.5 dB**,
**drive-independent**.

> ⚠️ **Superseded 2026-07-28 — see P1 below, which is CLOSED.** The magnitude reading above is
> correct and was reproduced, but the fix does not exist in a real-time-viable form: the pedal's
> LF excess comes with a **phase lead** (+33° at 20 Hz), and every minimum-phase EQ that adds
> magnitude adds *lag*, so it makes the time-domain null worse — measured at 1–4 dB worse on the
> best-matching captures. Only a zero-phase (linear-phase FIR) correction helps, and only by
> ~0.6 dB, for tens of ms of latency. The per-drive detail below is still the best record of the
> deficit's shape; it just no longer implies an action.

This deficit also **causes** the LF THD shortfall in Finding 4: the plugin's low end never reaches
the rails. G10 T5 Clean at the −6 dB sweep, 40 Hz: pedal **35.6%** THD, plugin **4.7%**. Since the
LF gap is not correctable, that THD gap is now an **accepted residual** too, not a work item.

---

## Finding 2 — broad +1 dB tilt from 1.6 to 5 kHz (real, not knob variance)

From the core table above (mid drive, all modes):

| Hz | 1016 | 1280 | 1613 | 2032 | 2560 | 3225 | 4064 | 5120 |
|---|---|---|---|---|---|---|---|---|
| error | 0.00 | +0.13 | +0.28 | +0.45 | +0.57 | +0.65 | +0.88 | +1.00 |

Smooth, monotonic, and **independent of both drive and tone**. Grouping all 44 captures by TONE
position kills the knob-variance hypothesis outright:

```
tone   n    1613   2560   3225   4064   5120
0.20   9   +0.07  +0.25  +0.43  +0.67  +0.58
0.50  24   +0.36  +0.57  +0.62  +0.77  +1.01
0.80   9   +0.39  +0.59  +0.70  +0.83  +1.08
```

`hfTrim` (−1.3 dB @ 4.5 kHz) is the existing correction here, but its pivot is too high to reach
1.6–3 kHz.

**The apparent capture-to-capture variance is almost entirely above 8 kHz** (spread runs +18.8 to
−4.4 dB) — that is the capture-side unreliability already documented in CLAUDE.md, not plugin
error. Below 5 kHz the between-capture sd is ~0.3 dB. Do not fit anything above ~5 kHz.

---

## Finding 3 — the wandering FR peak (revised 2026-07-28: it is NOT only a readout)

Overall-FR peak location, plugin vs pedal, in octaves (`fr_thd_audit.py peaks`, `sweep_clean`):

- **G2 Clean: +0.16 to +0.30 oct** — the bass-cut bell over-cutting at low drive.
- **G8–G10: +0.35 to +1.61 oct** (G10 T8 Clean: plugin 1109 Hz, pedal 362 Hz) — the documented
  high-drive bass bloom.
- **G3–G7 — the original "within ±0.2 oct" claim was wrong.** It came from the summary line
  (`G3–G7 only: mean +0.01`), and that mean is near zero only because the errors **cancel by
  mode**: Distortion runs *negative* through G3–G5 (−0.20, −0.24, −0.25, −0.28) while OD runs
  *positive* at G6–G7 (+0.38, +0.43). Max |error| in that band is **0.43 oct**, not 0.2. Opposite
  signs in different modes at the same drive is a structural signature, not scatter.

The G2 and G8–G10 rows do sit on top of tilt errors already identified. The G3–G7 mode-split does
not obviously, so the peak is now its own open item — see **P6**.

**Resolved 2026-07-28 (P6), and the "structural signature" sentence above is the trap.** Opposite
signs by mode at the same drive is *not* evidence of a mode-differentiated mechanism. It is one
mode-independent drift in DRIVE crossing zero at a different knob position per mode, because each
mode has a different low-drive intercept. Both readings of this table failed the same way — the
mean cancelled the effect, the per-capture signs mis-attributed it — because **neither plots the
error against the drive axis**, which is the axis it lives on. See P6.

> **Metric note:** always read this table per sweep level and never trust its mean. On
> `sweep_drv_-6` every peak drops to 60–350 Hz (LF-dominated once compressed) and the sign pattern
> is different again — Dist goes to −0.9…−1.1 oct at G10 while OD goes +0.9…+1.2 at G2–G4. The
> `all: mean` there is +0.05, which describes none of it.

---

## Finding 4 — harmonics: odd orders excellent, even orders wrong in three ways

**Odd orders are the good news.** H3/H5/H7 match within ~1 dB nearly everywhere, in all three
modes, at all three anchors. The WDF circuit is doing its job.

**Even orders are the problem** (plugin − pedal, dB):

| Case | Result |
|---|---|
| **Boost @ −6 dB sweep** | H2 **−25.7**, H4 **−33.8**, H6 **−45.9** — while H3 +1.2, H5 +4.4, H7 +0.3 |
| **OD/Dist @ −18/−12** | H4 **−5 to −20**, H6 **−11 to −26**; H2 within ±3 |
| **OD/Dist @ −6, 100–200 Hz** | H2 **+10 to +18 (too hot)** — pedal's H2 *falls* with level, plugin's *rises* |

*(Row 1 fixed 2026-07-28 by P2's asymmetric rails; row 3 fixed the same day by P3's band split +
low-band wash-out — H2's bias is now 0.0/+0.2 dB. Row 2, the H4/H6 shortfall, is still open and
went ~3 dB further short as a P3 side effect — the mechanism can reach it (see P3.1), it just
hasn't been retuned for it yet.)*

Boost in the plugin is effectively a **symmetric** clipper; in the pedal it is strongly
**asymmetric**. That is the "clean harmonics look off" observation, and it is a 26–46 dB gap, not a
subtle one. *(Fixed 2026-07-28 by P2's asymmetric rails — Boost's whole even series now lands
within ~2 dB; the OD/Distortion rows were then fixed by P3.)* The pedal's H2 also has strong frequency structure that the plugin's flat injected H2
did not reproduce at all (`fr_thd_audit.py h2`, −6 dB sweep — the numbers below are the
**pre-P3 baseline**; the plugin rows have since changed, the pedal rows have not):

```
                  100    200    400    800   1600   3200   6400
G5 T5 Clean ped  -20.5  -23.5  -28.4  -41.5  -40.4  -31.5  -21.9
           plug  -46.8  -44.0  -38.1  -34.4  -34.9  -36.0  -36.4
G5 T5 OD    ped  -51.8  -51.7  -35.2  -30.7  -27.2  -33.5  -35.0
           plug  -34.6  -34.2  -31.2  -28.2  -29.4  -30.9  -31.0
```

(Don't chase the absolute 6.4 kHz magnitude — the fundamental is rolled off there, so the ratio
inflates. The *shape* mismatch at 400 Hz–3 kHz, where levels are strong, is the solid part.)

### The 6–8.5 kHz THD gap is mostly a measurement artifact

Two independent reasons, both now encoded as guardrails in `fr_thd_audit.py`:

1. **The discrete-tone estimator is invalid at 6 and 8 kHz at FS = 48 kHz.** `analyze.thd` sums
   k = 2..8; at f0 = 6000, **H7 folds exactly onto the fundamental** (and H8 onto DC); at
   f0 = 8000, H5 and H7 fold onto 8 kHz and H6 onto DC. The pedal reads **291% THD at 8 kHz** on
   G10 T5 Clean — physically impossible. (`fr_thd_audit.py alias` prints the full landing map.)
2. **The swept Farina bands above ~5 kHz are H2-only** (the order limit drops H3+ past Nyquist) and
   read inconsistently between adjacent bands of the *same* capture — 1.8% at 6.4 kHz vs 30.9% at
   8.1 kHz on one capture.

`comprehensive_report.py`'s `build_band_source_map` currently routes 6451/8128 Hz to the Farina
path (they sit under `thd_max_measurable_hz(max_order=2)`), so the dashboard renders a dramatic THD
cliff there that is not real.

The valid signal underneath it is the H2-vs-frequency shape mismatch above — **same root cause as
the even-harmonic finding, not a separate HF issue.**

---

## Work plan

~~Ordered. **P1 changes what P2/P3 must be fitted against**~~ — **no longer true.** P1 is closed
with no DSP change, so the harmonic baseline does not move. **P2 and P3 can be fitted directly
against the current `comprehensive_data.json`.**

### P0 — harness hygiene — ✅ **DONE 2026-07-28**

- ~~Route THD bands above ~5 kHz to `na`~~ — done, at the **H3** limit (~6.3 kHz), not 5 kHz: THD is
  an RSS dominated by H3 for a symmetric clipper, so a band that has lost H3 reports H2, not THD.
  Routes 6451/8128 Hz to `na`. Plus `discrete_tone_is_valid()`, which independently rejects any
  fallback tone whose k = 2..8 harmonic folds onto the fundamental or DC (condemns 6 kHz and 8 kHz).
- ~~Mark FR bands above ~8 kHz as capture-unreliable~~ — done, and enforced at source: the trust
  band lives in `comprehensive_report.py` (40 Hz–8 kHz), `dashboard_gen.py` reads it out of `meta`
  so the two cannot drift. Also fixed `compute_summary`, which scored plain all-band rms with no
  offset removal — the per-mode tiles disagreed with the heatmap above them *and* were inflated by
  capture spread. Now the same median-removed shape metric, over the trusted band only.
- ~~Add **H2 vs frequency** as a first-class chart~~ — done: `h2` block in the JSON, a per-mode
  chart on the dashboard, and `fr_thd_audit.py h2` now reads the JSON instead of re-rendering (so
  it joins `all`).

### P1 — sub-64 Hz LF extension  — ⚠️ **SUPERSEDED by P8 (CLOSED 2026-07-29): partially correctable**

> **Read P8 before acting on anything below.** P1's measurements are all correct and its two
> shelves really were rejected by the null. Its *conclusion* — "un-fixable by any filter a real-time
> plugin can use" — over-generalised from those two points. The phase lead that blocks a
> minimum-phase fix exists **only below ~32 Hz**; from 40–80 Hz the pedal *lags*, which is what a
> minimum-phase low-shelf supplies. The "do not re-attempt with any IIR EQ" instruction below is
> withdrawn.
>
> **P8 shipped the fix, and it is not a new shelf.** P1 framed this as "add an LF-extension filter",
> and so did P8's own plan — but the band already had a drive-keyed instrument in it (`bassBoost*`),
> and the correction is drive-keyed too, so the two had to be fit as one set (P7's rule). P8 refit
> `bassBoost*` from a monotone ramp into a hump in drive and added nothing: **median null −21.5 →
> −22.6 dB, 38 of 44 captures deeper.** `lfExt*` stays retired and is now redundant, not merely
> rejected.

The deficit in Finding 1 is real and reproducible. Full record below; the code sits disabled in
`MonarchChannel::lfExt*` (`lfExtEnabled = false`) for A/B.

**Step 1 — topology (`schematic-checker`): no fix exists.** Every pole/zero-capable RC in both
schematics was traced against actual values — signal path *and* the bias/supply network, whose
exclusion was re-verified by computing the bias node's own impedance (Z_VB ≈ 32 Ω @ 50 Hz, 80 Ω @
20 Hz — negligible against the 6.8 k–1 M it references, so treating it as an AC ground is
specifically valid at 20–60 Hz, not just generally). **Nothing lands near 45–55 Hz.** The only
audio-path corners are the input HPF (7.2 Hz), Stage 2's C7/R9 (159 Hz), Stage 1's feedback ladder
(~589 Hz) and the output HPF (0.16 Hz). The named suspect — the literal 3-terminal DRIVE wiper-tap
the 2-terminal rheostat approximation drops — traces to **R6 = 10 k, C5 = 100 n, i.e. the same
159 Hz corner already modelled as R9/C7**. It contributes no sub-60 Hz content at all; it only
redistributes gain, which is why it was already rejected on separate grounds (circuit.md §7).

**Step 2 — the empirical shelf was built, fitted twice, and rejected by the null.**

| fit | FR result | null result |
|---|---|---|
| +3.5 dB @ 60 Hz (min FR rms) | improved 33/42, median rms 2.31→1.98 | **worse on 27/42, mean +0.95 dB** |
| +5.0 dB @ 25 Hz (confined to the drive-agreed band) | improved 33/42, median 2.31→2.00 | **worse on 28/42, mean +1.08 dB** |

The regressions land on the *best-matching* captures: G6 T5 Clean −22.0 → −17.7, G7 T5 Dist −17.9 →
−13.5, G5 T5 Dist −20.6 → −18.1. Confining the shelf to 25 Hz did not help, which disproved the
first hypothesis (that it was spilling into the energy-carrying 80–160 Hz band).

**Step 3 — the cause, measured.** Complex transfer function, pedal vs plugin, clean sweep,
1 kHz-normalised (G5 T5 Clean; G6 T5 Dist gives the same shape):

| Hz | 20 | 25 | 32 | 40 | 50 | 64 | 80 | 101 |
|---|---|---|---|---|---|---|---|---|
| \|ped\|−\|plug\| dB | +2.66 | +2.77 | +2.45 | +1.91 | +1.28 | +0.54 | −0.04 | −0.48 |
| phase ped−plug | **+33°** | +21° | +10° | +2° | −3° | −6° | −7° | −6° |

The pedal is **louder at 20–40 Hz *and* leads in phase**. A minimum-phase low-shelf adding +3 dB at
20 Hz necessarily brings about **−15° of lag** with it. So the magnitude error goes to zero while
the phase error grows 33° → 48°, and the complex residual gets *bigger*: |1.36∠33°−1| = 0.76
before, |0.96∠48°−1| = 0.81 after. The null measures exactly that.

**Step 4 — proof it is the phase, not the magnitude.** The identical magnitude correction applied
offline to the same renders, minimum-phase vs zero-phase (null depth, dB, clean sweep):

| capture | baseline | min-phase | zero-phase |
|---|---|---|---|
| G5 T5 Clean | −21.0 | −18.7 | −21.3 |
| G5 T5 OD | −21.1 | −19.1 | **−22.4** |
| G6 T5 Dist | −19.6 | −15.6 | **−20.6** |
| G6 T5 Clean | −21.8 | −18.4 | −21.8 |
| G7 T5 Dist | −17.6 | −13.6 | −18.1 |
| G2 T5 Clean | −15.2 | −16.1 | −15.9 |

Zero-phase helps everywhere; minimum-phase hurts almost everywhere. **The magnitude reading was
right and the fix direction was right — the instrument was wrong.** A zero-phase shelf reaching
25 Hz is a multi-thousand-tap FIR (tens of ms of latency) for a mean gain of ~0.6 dB. Not worth it,
and unusable live. Ruled out on cost, not on principle.

> **Corrected by P8:** "minimum-phase hurts" is true of *these two shelves* — both fit to zero the
> FR magnitude, and both about twice the complex-optimal depth. A minimum-phase shelf at roughly
> half the depth and an octave higher helps. The depth axis was never searched with a phase-aware
> metric — and neither was the DRIVE axis, which is where the real answer turned out to be: the
> correction is a hump in drive, and P8 delivered it by refitting `bassBoost*` rather than adding a
> shelf. See P8.

**Step 5 — "what about changing a component value instead?" (asked and answered, don't re-ask).**
No: it is strictly *worse*, for two reasons.

Re-cornering the Stage-2 coupling cap C7 changes the plugin's transfer by exactly
`(s+w_old)/(s+w_new)` — which **is** a low shelf, but one whose gain and pivot are locked together
(`gain = f_old/f_new`, `pivot = √(f_old·f_new)`). You cannot reach 20–40 Hz without dragging the
low mids along:

| C7 | corner | ≡ shelf gain | pivot | @20 Hz | @80 Hz | @160 Hz |
|---|---|---|---|---|---|---|
| 100n *(today)* | 159 Hz | — | — | 0.00 | 0.00 | 0.00 |
| 150n | 106 Hz | +3.52 dB | 130 Hz | +3.44 | +2.55 | +1.40 |
| 220n | 72 Hz | +6.85 dB | 107 Hz | +6.60 | +4.36 | +2.18 |
| 330n | 48 Hz | +10.37 dB | 88 Hz | +9.75 | +5.61 | +2.61 |

(the free fitted shelf put only **+0.13 dB** at 160 Hz). Measured null depth, clean sweep, applied
exactly offline:

| capture | base | C7 150n | C7 220n | C7 330n | input cap 22n→47n |
|---|---|---|---|---|---|
| G5 T5 Clean | −21.0 | −15.2 | −11.3 | −8.8 | −20.4 |
| G5 T5 OD | −21.1 | −16.0 | −11.4 | −8.5 | −20.6 |
| G6 T5 Dist | −19.6 | −14.3 | −9.5 | −6.5 | −18.2 |
| G7 T5 Dist | −17.6 | −12.7 | −8.5 | −5.8 | −16.2 |

And the general argument, which covers every value of every part: **all of these networks are
minimum-phase, so magnitude determines phase.** Any network delivering +2.7 dB at 20 Hz delivers
≈−15° of lag with it; the pedal *leads* by +33°. Magnitude-up *and* lead-up together requires a
right-half-plane zero, which passive RC + op-amp stages do not produce. Adding a part rather than
changing one (e.g. a resistor across C7) just buys back independent gain/pivot — i.e. it rebuilds
the free shelf, which is the **best case of the whole family and already fails**. Schematic
fidelity was never the constraint here.

**Consequences for the rest of the plan.** The LF THD shortfall (Finding 4's 40 Hz row: pedal 35.6%
vs plugin 4.7%) was diagnosed as a *consequence* of this LF gap, so it is not separately fixable
either — it should be reclassified as an accepted residual alongside it. **P2/P3 therefore no
longer need to wait for P1** — the harmonic baseline is not going to move, so fit them against the
current `comprehensive_data.json` directly.

**Method note worth keeping.** Two metrics disagreed and the null was right. FR rms weights every
third-octave band equally and is blind to phase; the time-domain null is complex and weights by the
sweep's actual energy (an exponential sweep carries equal energy per octave, so 20–40 Hz is *fully*
weighted — the "there's no guitar energy down there" intuition does not apply to this test signal).
For any correction that could carry phase, **the null is the arbiter and FR rms is the hypothesis
generator** — the same lesson the reverted 335 Hz presence bump taught, arrived at from the
opposite direction.

### P2 — asymmetric op-amp rail saturation — ✅ **DONE 2026-07-28**

The plan was right and it worked: `railSaturate` / `railAntideriv` now take **separate
positive/negative ceilings** (`railAsymV = 0.60 V` around the unchanged 3.3 V mean), and that one
change generates the entire even series with the correct internal ratios. Boost, at the −6 dB
sweep, 100/200/400 Hz — plugin − pedal, dB:

| | H2 | H4 | H6 | H3 | H5 | H7 |
|---|---|---|---|---|---|---|
| before | −21.3 / −19.3 / −10.2 | −28.8 / −32.3 / −20.8 | −39.3 / −45.7 / −33.4 | +2.1 | +8.6 | −0.6 |
| after | **−1.0 / −2.0 / −2.2** | **+0.4 / −2.0 / −0.4** | **−4.8 / −0.8 / +0.2** | +0.8 | +2.9 | −1.1 |

The odd orders were already right and stayed right. `asymBoost` is **retired to 0** as hoped —
with the rails in, restoring its old 0.35 moves the harmonics ≤0.3 dB and the null 0.01 dB.

Three things the plan did not anticipate, each of which changed the answer:

**1. The sign is not determined by the harmonics — the null decided it.** ±0.30 V give *identical*
harmonic magnitudes (magnitude spectra can't see which way a waveform leans) and identical
clean-sweep nulls. They differ only on the driven nulls: +0.30 improves them, −0.30 degrades them
by the same amount (+1.20 dB at the −12 sweep). So the **positive** ceiling is the higher one.

**2. An asymmetric clipper rectifies, and the 0.16 Hz output cap smears the DC for a second.**
This was the whole apparent cost of the change and it was an artifact. The first fits looked bad —
the clean-sweep null lost up to 5.7 dB at mid drive — and the reason was *not* the clean sweep
distorting (with symmetric rails the plugin's clean sweep is exactly linear, all harmonics at
−150 dB). It is that `sweep_clean` directly follows the 1 s 1 kHz calibration tone, which in Boost
is hard against the rails; the DC step that leaves decays through C11/R14's 0.16 Hz corner with a
~1 s tail, straight into the sweep. Measured on that tone: **plugin 8.7 % DC vs pedal 0.08 %**,
still 2.5 % a second later. The captures carry no such tail because the NAM capture chain is
AC-coupled far above 0.16 Hz. Fix: strip the rectified DC at source with a 50 ms one-pole mean
(3.2 Hz — an octave below the 20 Hz sweep floor, so it cannot touch a real harmonic), exactly as
`injectEvenHarmonic` already does for its own even term. This is **independently worth having**:
at `railAsymV = 0` it alone improves the OD/Distortion nulls by **0.5–0.7 dB**, so the clip path
had been carrying DC all along.

**3. The guard was wrong about Distortion, and a fixed asymmetry cannot satisfy both modes.**
The plan assumed OD and Distortion both "clamp at the diodes far below the rails". That is true of
**OD** — its output is byte-for-byte identical, exactly as argued. It is **false of Distortion**,
whose linear Stage 2 ×−22 reaches ~13.9 V and *is* rail-clamped (dsp.md already said so). The
captures then contradict any fixed offset outright: the pedal's Boost is strongly asymmetric
(H2 −21.2 dBc) while its Distortion is nearly perfectly symmetric (H2 −51.5 dBc). Ungated,
`railAsymV = 0.60` lands Distortion's whole even series ~26 dB hot, and zeroing `asymDist` barely
dents it (+25.6 → +24.6) — the rails are the source, not the injection.

- **The discriminator is the load, which the clip switch sets.** Boost/OD leave pin 7 driving only
  the 25 k tone stack (~0.13 mA); Distortion's 1S1588 pair clamps node_HC to ±0.584 V so pin 7
  drives ~3.3 mA — about 25×. Op-amp saturation voltages are specified per load precisely because
  both ceilings collapse toward the supply rails under load, taking the asymmetry with them.
- So the asymmetry is scaled by `railAsymLoadedScale` when SW-2 is loading the output, **fitted
  to 0**. Distortion returns to its pre-P2 state exactly. Its remaining even-harmonic error
  (+16.1 dB at H2) is pre-existing and belongs to P3.
- The injections must **stay** in Distortion: zeroing `asymDist`/`asymLowDist` as well overshoots
  the other way, leaving it ~60 dB short of the pedal's H2.

**Whole-set effect** (`run_validation.py`, all 44 captures, same harness before and after): null
range **−22.3…−6.4 → −22.7…−6.8**, both ends deeper; mean **−0.12 dB deeper**, better on 21,
worse on 19, unchanged on 4; by mode Clean/Boost **−0.29 dB**, OD +0.01, Dist −0.06. The gains
concentrate at high drive (G7 T5 Dist −1.3, G7 T5 Clean −1.1, G8 T5 Clean −1.0) and the small
regressions at low drive (≤ +0.7, G2–G4). FR rms is unchanged (median 2.31 → 2.33). The median
null reads 0.2 dB shallower (−16.6 → −16.4) purely because it sits among the low-drive captures;
the mean is the honest summary and it improved. All per-stage gates, `ControlSweep` and `auval`
still PASS.

> **Gate fixed along the way.** `FullChain_DualChannel`'s "Red hotter than Yellow" check compared
> peak output in **Boost at a hot input** — where both channels are pinned against the same rail,
> so their peaks agreed to ~0.02 % and the check was decided by rounding. The asymmetric rails
> flipped that coin, which is how it was found. It now measures the same thing at 3 mVpk, where
> neither channel clips and Red reads a real 1.11× of Yellow.

### P3 — even-harmonic series shape in OD/Distortion — ✅ **H2 DONE 2026-07-28, H4/H6 → P3.1**

H2 is fixed and its level trend now runs the right way. H4/H6 regressed slightly as a side effect
(see "Cost" below) and were initially written up here as structurally unfixable by this
mechanism — **that was wrong**, corrected the same day; see P3.1.

The plan named the right symptom — "make the low-band path wash out at high clip depth the way the
mid/high `tanh` path already does" — but attributed it to one path when **both** were wrong, for
the same underlying reason: Stage 1's high-shelf makes `nodeG` small below a few hundred Hz.

**Ablation first, and it changed the diagnosis.** Rendering with each path disabled in turn
(`asymLowOD`/`asymLowDist` = 0, then `asymOD`/`asymDist` = 0) split the −6 dB sweep's +18.8 dB
OD H2 error at 100 Hz cleanly in two — and the **mid/high path was the larger share**, still
+15.5 dB hot on its own:

| −6 sweep, H2 plugin−pedal (dB) | 100 Hz | 200 Hz | 400 Hz |
|---|---|---|---|
| both paths (baseline) | +18.8 | +19.7 | +1.8 |
| mid/high path alone | +15.5 | +14.1 | +3.0 |
| low path alone | +8.0 | +4.3 | −25.1 |

That is not what the plan assumed. The mid path's wash-out **depends on `tanh(asymDriveScale·nodeG)`
squaring up**, and with `nodeG` shelved down it never leaves its linear region — so instead of
collapsing, the injection grew as `nodeG²`. Its wash-out was working correctly at 400 Hz (+3.0) and
failing everywhere below. The low path, meanwhile, was the *only* supplier of the 400 Hz even
orders' opposite: it contributes essentially nothing there (−25.1).

**Two fixes, one per path.**

1. **`asymMidFc` = 400 Hz high-pass on the mid path's source.** The counterpart the low band's
   150 Hz low-pass always implied but never had — the two paths now split the spectrum instead of
   overlapping it. Fitted over {200, 300, 400, 800}; 400 won on H2 at every anchor and level.
2. **`asymLowWash` = 25 / `asymLowThresh` = 0.15 V — the low band's own depth envelope.** The low
   path cannot wash out on its own because its source `x` is **clamped**, so its low-passed square
   stops growing while the pedal's H2 keeps falling. It also **cannot borrow `clipEnv`**: that
   envelope's 0.37 V threshold is never reached at low frequency — precisely the shelving this
   path exists to cover. Measured: a wash keyed to `clipEnv` moved H2 by 0.7 dB at the extreme,
   i.e. nothing. So the low band carries `lowEnv`, keyed to a low-passed `nodeG` with a threshold
   scaled to the drive that actually arrives down there.

**The threshold is what makes the shape right rather than merely smaller.** Below it the wash is
inert, so the clean and −18 sweeps (already matching within ~3 dB) are untouched and only the two
hot sweeps come down. `asymLowOD`/`asymLowDist` are then raised 1.4× (−0.015 → −0.021, −0.042 →
−0.059), which the wash pays for at the hot end and which recovers the quiet end.

**Result, all 44 captures, driven sweeps** (`comprehensive_report.py`, cells with pedal > −70 dBc):

| | H2 rms err | H2 mean bias | H3/H5/H7 | H4/H6 mean |
|---|---|---|---|---|
| Overdrive | 10.3 → **6.9** | +2.8 → **0.0** | 4.03 → 4.03 | −12.7 → −16.3 |
| Distortion | 10.2 → **7.9** | +3.0 → **+0.2** | 2.73 → 2.73 | −11.3 → −14.6 |
| Boost | unchanged | unchanged | unchanged | unchanged |

At the −6 anchor specifically (100/200/400 Hz): OD +18.8/+19.7/+1.8 → **+0.7/+3.4/+0.1**, Dist
+16.5/+9.4/+7.3 → **−0.7/+1.3/+3.0**. The level trend now falls with drive as the pedal's does.

**Cost, and it is real.** H4/H6 go ~3.3 dB further short (they were already 11–13 dB short). Part
of the LF H2 overshoot had been standing in for them. This was accepted because H2 sits 10–20 dB
above H4/H6 in the pedal and carried a systematic bias, which is now gone — but the regression is
a real open item, not a closed one: see P3.1.

**Guards.** Odd orders are **bit-identical** in both modes (4.03 / 2.73 unchanged) — the change is
purely even-order. **Boost renders are byte-identical** (`asymBoost` = `asymLowBoost` = 0). The
time-domain null is **unchanged on all 44 captures** to the report's precision: range −22.7…−6.8,
median −16.35, mean −16.084, FR rms median 2.325, all identical before and after. All 14 per-stage
gates, `ctest` (6/6), `ControlSweep` and `auval` PASS.

> The null being *exactly* neutral is the expected outcome, not a null result: these harmonics sit
> at −45 to −60 dBc, far below a −16 dB null's noise floor. The null's job here was to prove no
> harm; only the harmonic anchors can resolve the change. (Validated with the anchors + the null,
> **not** `linear_tf` — it mis-reads clip-gated corrections; the odLowShelf lesson, dsp.md.)

### P3.1 — H4/H6 in OD/Distortion — ✅ **DONE 2026-07-28, route 1 (both paths' tanh knee)**

P3's write-up originally claimed H4/H6 were structurally out of reach of the injection mechanism
("a squared source generates H2; the higher even orders only come from a genuinely asymmetric
clipper"). **That was false, and P3's own ablation data contradicted it:** the mid-path-alone row
above shows H4 at **+6.9 / +6.8 dB** (100/200 Hz) before the fix — an *overshoot*, not an absence.
`soft² = tanh(s·nodeG)²` is not squaring a sine: tanh already contains 3f/5f content, so its square
contains f×3f → H4 and 3f×3f, f×5f → H6.

Route 1 (re-fit the tanh knee) was the answer, but it took **two** knees, because the two injection
paths were failing for different reasons — and only one of them was the one P3.1 predicted.

**Step 0 — the prerequisite check, and it cleared.** Are the pedal's H4/H6 targets above the NAM
capture chain's floor? Measured with `analysis/p31_harm_floor.py`, which reads the floor out of the
*same* instrument that reads the harmonics: Farina's deconvolved IR puts the N-th harmonic at a
known pre-delay, so gating at **fractional** orders (2.5, 3.5 … 6.5) — between the harmonic
impulses, same window rule, same code path — gives a per-band floor in the same units.

| | n cells | median margin | min margin | below 6 dB |
|---|---|---|---|---|
| H2 | 96 | +56.1 dB | +16.9 | 0 |
| H4 | 96 | +47.9 dB | +12.0 | 0 |
| H6 | 96 | +39.0 dB | **+6.9** | 0 |

The floor sits at −80…−110 dBc against H6 targets of −38…−75 dBc. Even the quietest target clears
it by ~7 dB. **Nothing here is floor-limited** — the whole H4/H6 gap was real plugin error.

**Step 1 — the knee sets the ratio between orders.** For `tanh(a·sin)²`, the H4:H2 and H6:H2 ratios
are a pure function of how hard the source is driven:

| a | 0.5 | 1.0 | 1.7 | 2.5 | 4.0 | 8.0 |
|---|---|---|---|---|---|---|
| H4 − H2 (dB) | −28.0 | −17.0 | −13.0 | −6.0 | −2.7 | −0.7 |
| H6 − H2 (dB) | −57.4 | −35.4 | −24.2 | −10.0 | −6.3 | −1.8 |

The pedal wants H4 ≈ H2 − 9 dB and H6 ≈ H2 − 12 dB; the plugin was sitting at −21 and −37, i.e. far
too soft a knee. `asymDriveScale` (1.70, carried over unswept from before P2) → **3.50**.

**Step 2 — but that only moved the mid path's own band, and the reason is the second finding.**
Sweeping `asymDriveScale` alone left the 100/200 Hz anchors *completely unmoved* on the −12 sweep.
Those anchors belong to the low path, and **that path could not produce H4/H6 at all, by
construction**: its source is a 150 Hz low-pass of the clip output, and the low-pass strips the
clipped waveform's own harmonics, so `xLp` is very nearly a **sine** — and a squared sine is pure
H2 with no H4 whatever. So the low path got the same treatment: `tanh(asymLowDriveScale·xLp/clamp)²`,
normalised by the mode's clamp voltage (OD ±1.64 V, Distortion ±0.584 V) so **one** knee constant
sets the same operating point in both modes and the per-mode coefficients keep carrying level only.
Fitted to **4.90**.

**Step 3 — fit the knee first, then re-zero the bias with the coefficients.** A coordinate descent
over all eight constants (H2 weighted ×2) reached a good total score by letting H2 drift **+3.7 /
+5.1 dB hot** — i.e. it spent P3's headline result to buy H4/H6. Cutting all four gains afterwards
(OD ×0.65, Dist ×0.55) put H2's bias back to zero and cost only ~1 dB of the higher orders. **A
gain moves the whole even series together; the knee does not.** That asymmetry is the whole method
here, and it is why the two must be fitted in that order.

**Result, all 44 captures, driven sweeps, cells where the pedal reads above −70 dBc**
(`fr_thd_audit.py evens --base <old>.json`):

| mode | order | rms was → now | bias was → now |
|---|---|---|---|
| Overdrive | H2 | 6.9 → 7.4 | −0.0 → **−0.0** |
| Overdrive | H4 | 13.0 → **6.4** | −11.1 → **−1.5** |
| Overdrive | H6 | 23.4 → **11.4** | −22.0 → **−7.4** |
| Distortion | H2 | 7.9 → 8.5 | +0.2 → **−0.0** |
| Distortion | H4 | 13.4 → **6.3** | −11.2 → **−2.1** |
| Distortion | H6 | 20.2 → **10.2** | −18.2 → **−5.8** |
| Boost | all | unchanged | unchanged (byte-identical renders) |

H2's rms is 0.5–0.6 dB worse and its **bias is still exactly zero** — the fit subset used during the
search put the bias at 0.00 there but +0.7/+1.4 over all 44, so the four gains were trimmed a final
~0.7/1.4 dB (OD ×0.92, Dist ×0.85) against the whole set. That trade is the right way round: the
bias is the systematic error, the rms includes per-capture spread the injection cannot address.

**Guards.** Odd orders **0.00 dB** change on every mode, sweep and anchor (the injection is
even-order only). **Boost renders byte-identical.** The time-domain null is unchanged — worst cell
+0.06 dB, mean +0.01/0.00/0.00 across the three sweeps. **IMD: SMPTE 60 Hz + 7 kHz identical to
0.00 dB** median *and* worst, which is the guard that matters — that twin-tone pair straddles
exactly the low/mid injection split. (CCIF 19+20 kHz moves up to 1.8 dB on one capture, but the
pedal's own CCIF readings there are not usable: several read products *above* the carriers, i.e.
capture-chain aliasing, per §4's trust bands.) All 14 per-stage gates, `ctest` 6/6, `ControlSweep`
and `auval` PASS.

**IMD is now measured, not assumed.** P3.1's plan flagged intermodulation as the risk a single
swept sine cannot see, and proposed a by-ear dyad check. The test signal already carries both
twin-tone segments, so `p2_rail_asym_fit.py` now reports IMD against the captures as a fourth guard
alongside harmonics / null / render hash — capture-referenced rather than subjective.

**Remaining residual (accepted, not a defect to chase with this mechanism):** H6 still runs ~5–7 dB
short on average. Pushing the knees further closes it but costs H2 rms faster than it gains H6 —
the single-knee shape cannot match the pedal's H2:H4:H6 *and* its level trend simultaneously. The
next real step there is route 3 (asymmetric diode pair, unequal `Is`, custom NR root), which would
let the empirical injections retire in OD/Dist the way `asymBoost` retired in P2. Route 2
(Chebyshev terms) is now *less* attractive than when it was written: per-order control would fix
H6 in isolation, but the knee re-fit shows the orders are coupled through one physical knee in the
real device too.

### P3.2 — Boost's evens vanish on the quiet driven sweeps — ✅ **DONE 2026-07-28**

The whole-set `evens` view exposed something the P2 write-up could not see, because P2 measured at
the −6 dB sweep only: on Boost, **30 of 143 H2 cells had the plugin at ≈−160 dBc** — not "a bit
short", but *no even-harmonic mechanism engaged at all* — while the pedal reads −18 to −59 dBc
there. Those cells are the low-drive/quiet driven sweeps, and the cause was structural: after P2,
Boost's evens came **entirely** from the asymmetric rails, so when the swing does not reach the
knee the plugin is a mathematically exact symmetric clipper. The real pedal has an even-order
mechanism that survives below rail clipping.

**Step 0 — the target is real.** `p31_harm_floor.py` over the Boost captures puts every even order
**+58.7 dB clear of the capture chain's harmonic floor at worst** (median +100), and the readings
track drive the way a harmonic does and a floor does not. None of this gap was measurement noise.

**The fix needed no new mechanism** — and that is the finding. The plan proposed either a small
always-on asymmetry or reviving `asymBoost`. Both were unnecessary: **P3.1's low path already does
exactly this job and was already running in Boost**, with `railV` as its clamp reference
(`clampRef`); only its coefficient `asymLowBoost` was zero. It has the one property the rails lack
— sourced from a low-pass of the clip output, it is *always-on* rather than knee-triggered — and
its `lowEnv` wash-out hands over to the rails as drive rises, so it fills the quiet regime and gets
out of the way in the loud one. **The entire change is one constant: `asymLowBoost` 0 → −0.017.**

Two candidate mechanisms were probed numerically first and rejected before this was found: a
`tanh²` always-on term added to `railSaturate` (produces H2 at the right level but H4/H6 30–50 dB
too weak — the same "a squared near-sine is nearly pure H2" trap P3.1 hit), and simply widening
`railKneeMargin` (right H2:H4:H6 ratio, but no single margin is both gentle enough to leave OD's
±1.64 V undisturbed and active down at 0.5 V).

**Fitted in two passes.** A coarse sweep of |asymLowBoost| ∈ [0.002, 0.12] on the Boost subset gave
a very broad rms minimum near 0.030; the whole 44-capture set then discriminated inside it, where
aggregate rms is flat to ~0.1 dB across 0.017–0.030 and **H2's bias is the only thing that really
moves** (+1.9 → +1.3 → +0.5 at 0.030 / 0.024 / 0.017). Zeroing H2 is P3.1's rule, so 0.017.

Whole set, driven sweeps (`fr_thd_audit.py evens`):

| order | silent | rms | bias |
| --- | --- | --- | --- |
| H2 | 30 → **0** | 47.7 → **6.9** | −22.5 → **+0.5** |
| H4 | 24 → **0** | 43.2 → **8.3** | −18.3 → **−1.7** |
| H6 | 11 → **0** | 31.5 → **11.1** | −12.1 → **−4.5** |

Boost's even series is now on a par with OD/Distortion's (H2 rms 7.4 / 8.5), and its H4/H6 bias is
the best of the three modes. `asymLowDriveScale` (4.90) turned out to give Boost the right
H2:H4:H6 ratio as it stands, so **no Boost-specific knee was added and P3.1's fit is untouched**.

**Guards.** OD and Distortion are **byte-identical** (sha256, verified by rebuilding with the
coefficient zeroed) — P3/P3.1's fits cannot have moved. Boost's time-domain null changes by at most
**0.005 dB**; SMPTE/CCIF IMD by ≤0.002 dB. Full 44-capture headline unchanged: **−22.7 to −6.8 dB,
median −16.4**. The quiet *clean* sweep also improves and stays SHORT rather than hot.

**Sign.** Unlike `railAsymV`, whose two signs give identical magnitude spectra, the signs are *not*
equivalent here — this injection rides on the rails' own fixed-polarity asymmetry and can reinforce
or partly cancel it. Small (subset rms 8.18 negative vs 8.46 positive, null within 0.01 dB either
way) but negative wins, and it agrees with `asymLowOD`/`asymLowDist`.

**What is left, stated plainly.** This closes the *silent* pathology, not the whole gap. The
residual concentrates at 100 Hz on the quietest sweep at **high** drive, where the pedal reads
≈−18 dBc — fully saturated — and the plugin, through the same 159 Hz Stage-2 HPF, simply does not
swing far enough to reach its rails. That is **P1's LF shortfall seen through the clipper**, not a
second missing even mechanism, and P1 established it is not correctable with any minimum-phase EQ.
Don't chase it by raising this coefficient: that trades H2's now-zero bias for a few dB of H6.

### P5 — FR peak: verification only, no separate work — ⚠️ **SUPERSEDED by P6 (2026-07-28)**

Re-run `fr_thd_audit.py peaks` after P1/P4. G2/G8–G10 should tighten; the G8–G10 residual is the
documented bass-bloom limit. **But the premise that G3–G7 is already clean was wrong** (Finding 3,
revised), so the mid-gain band is a work item, not a check — see P6. **P6 is now done**; the
G8–G10 residual it did not close is still the documented bass-bloom limit.

### P6 — mid-gain FR peak displacement — ✅ **DONE (2026-07-28). The premise was wrong: it is not a sign split.**

**Result first.** The item was framed as "OD's peak is too HIGH, Distortion's too LOW at the same
drive — so some mode-differentiated mechanism must exist", and three sessions of topology tracing
looking for that mechanism came back empty. There is no mode-differentiated mechanism. Reading the
same table as a **trajectory in drive** instead of a list of per-capture errors shows one
mode-independent effect: **the pedal's FR peak migrates ~1.4–1.7 octaves downward from G2 to G10;
the plugin's migrates only 0.2–1.0.** The sign split is that single monotone drift crossing zero at
a different knob position in each mode, because each mode starts from a different low-drive offset.

The cause is a **gain-vs-DRIVE-knob curve error above ~G5** — the plugin is short of drive into its
clip stages, ~0 dB up to G5 rising to ~+6.8 dB at G10 — and the fix is the half of the real
3-terminal DRIVE pot's dual action the 2-terminal model drops. `MonarchChannel::driveMakeup`, one
flat drive-keyed gain at NodeG, zero at and below G5. Peak-error rms over the P6 subset: **OD 0.44 →
0.14 oct, Dist 0.37 → 0.21, Clean 0.66 → 0.35**; null over all 44 captures **0.52 dB deeper on the
mean** (up to 4.9 dB in P6's own G6–G8 band); **every capture at or below G5 is byte-identical**, so
none of the G2–G5 fitting from earlier passes moved. Full numbers and the fit below.

**Still do P4 after this** — `driveMakeup` changes clip depth at G6+, so re-read the 1.6–5 kHz tilt
off fresh data before touching `hfTrim`. (The original reason for the ordering — "a
mode-differentiated peak error can't be explained by a mode-independent shelf" — was based on the
premise this item just overturned, but the ordering itself is still right, for the simpler reason
that P6 moved the data P4 must be fit to.)

---

*Everything from here down is the original investigation, kept because its measurements are sound
and several are still load-bearing. Its framing — that a mode-differentiated mechanism must exist —
is the part that was wrong.*

**Reported from the dashboard (user, 2026-07-28) and confirmed against the table.** The curve
*shape* is right; where it **peaks** is not, and by more than the tilt findings account for. Every
reported case reproduces exactly (`fr_thd_audit.py peaks`, `sweep_clean`, plug / ped / oct):

| capture | plugin | pedal | oct |
|---|---|---|---|
| G6 T5 OD | 488 | 376 | **+0.38** |
| G7 T5 OD | 457 | 339 | **+0.43** |
| G5 T5 Dist | 287 | 342 | **−0.25** |
| G4 T5 Dist | 342 | 404 | **−0.24** |
| G5 T2 Dist | 243 | 296 | **−0.28** |
| G10 T5 OD | 398 | 230 | +0.79 *(bloom band, already known)* |
| G10 T8 Clean | 1109 | 362 | +1.61 *(bloom band, already known)* |

#### The reframing that solved it — read the table as a trajectory, not a list

Same `sweep_clean` data, T5, arranged by drive (plugin / pedal Hz, and the octave error):

| mode | G2 | G3 | G4 | G5 | G6 | G7 | G8 | G10 | plugin migration G2→G10 | pedal | deficit |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Clean | 970/818 (+0.24) | 954/901 | 891/857 | 800/897 (−0.17) | 837/912 | 860/787 | 875/684 | 834/315 (+1.40) | **−0.22 oct** | −1.38 | **−1.16** |
| OD | 775/745 (+0.06) | 700/659 | 619/607 | 520/498 (+0.06) | 488/376 (+0.38) | 457/339 | 431/277 | 398/230 (+0.79) | **−0.96 oct** | −1.70 | **−0.74** |
| Dist | 452/498 (−0.14) | 401/462 | 342/404 | 287/342 (−0.25) | 281/279 | 274/240 | 265/191 | 253/150 (+0.76) | **−0.83 oct** | −1.73 | **−0.89** |

Read down the columns and the "sign split" disappears. **The pedal's peak slides 1.4–1.7 octaves
down as DRIVE is opened; the plugin's slides 0.2–1.0.** Every mode has the same sign and a similar
size of deficit. What differs per mode is only the **intercept**: at G2 Clean sits at +0.24, OD at
+0.06, Dist at −0.14. Distortion starts below zero, so the common upward drift crosses zero around
G6 and reads "too low" at G4–G5; OD starts at zero and reads "too high" by G6. Two rows of one
monotone trend, sampled either side of its own zero crossing.

**The methodological error was in the aggregation, twice over.** Finding 3 read the G3–G7 *mean*
and got ≈0 because the opposite signs cancel; P6 then read the per-capture *signs* and inferred a
mode-differentiated mechanism from what is a per-mode intercept. Neither view shows the drive axis,
which is where the whole effect lives. **When an error is indexed by a knob, plot it against the
knob before hypothesising a mechanism.**

**Why this is not just P1/P2/P4 restated:** those are broadband tilts, and a tilt moves every mode's
peak the same way. Here Distortion is displaced **down** and OD **up** at the same drive settings,
so a single global shelf cannot be the whole story.

**Investigation so far (2026-07-28):**
- **`driveShelf()` is not mode-aware** (`drive_shelf_db()` takes only `drive01`, applied identically
  to Boost/OD/Dist), so candidate 2 (shelves crossing sign near G4–G5) cannot on its own produce an
  *opposite*-signed error between OD and Dist at the same drive. Confirmed numerically: peak location
  with the shelf's dB response subtracted back out (`raw-peak`, mirroring the `raw` view's method)
  keeps the same sign as the shelved plugin output on every checked capture —
  G4 T5 Dist −0.24→−0.49 oct, G5 T5 Dist −0.25→−0.13, G6 T5 OD +0.38→+0.42, G7 T5 OD +0.43→+0.50.
  **Candidate 2 demoted** — the sign-split is in the raw circuit/clip behavior, not manufactured by
  the correction shelves (though the shelves are visibly *not neutral* on this metric either — G4
  Dist got worse with the shelf on, worth another look once the root mechanism is fixed).
- **The "clean" sweep is not actually quiet at the clip stage.** `sweep_clean` is −30 dBFS at the
  plugin input, but Stage 1 (+12.85 dB near-peak) and Stage 2 (fixed ×−22 / +27 dB) put ~+40 dB of
  gain in front of both clippers — at G6, −30 dBFS → Stage 2 output ≈2.7 V, well past both OD's
  ~±1.64 V feedback clamp and Dist's ~±0.584 V shunt clamp. So **both clip stages are genuinely
  active on every sweep level including "clean"**, which is why the plugin's own clean-sweep peaks
  differ hugely by mode at fixed drive/tone (G6 T5: Clean 837, OD 488, Dist 281 Hz) — this is
  expected structurally, not a bug. It also means candidates 1 and 3 (both clip-depth-dependent)
  remain live, and any linear/no-clipping explanation is ruled out.
- **schematic-checker consulted (2026-07-28) on candidate 1 (mode-dependent Tone-stage loading) —
  result: a real gap exists, but it is the WRONG SIGN to be the P6 cause; candidate 1 is ruled out.**
  - **Distortion:** `ToneStage.h` takes V(node_HC) in via an `IdealVoltageSourceT` — a zero-impedance
    handoff, in every mode, at every clip depth. That is circuit-inaccurate specifically for
    Distortion: node_HC there is NOT an op-amp output, it's a passive `R12(1k) ∥ r_d(dynamic)`
    junction (`SW2HardClip.h` correctly reads `voltage(dp)` off the diode pair, but `MonarchChannel`
    then feeds that voltage into Tone with no series impedance). So the model omits a real,
    level-dependent Thevenin resistance into the Tone stage's C8 shunt pole — genuinely a
    Distortion-only gap (confirmed: OD's SW-1 clamp sits *inside* Stage 2's feedback loop, so pin7
    stays a legitimate zero-impedance op-amp output at every clip depth — an ideal-op-amp
    consequence, not an approximation. No comparable gap on the OD side).
  - **But the direction is backwards.** `r_d = nVt/I` *shrinks* as clipping deepens, so the missing
    series R is *largest near the clip threshold* (light clipping) and *shrinks toward the model's
    always-zero baseline* as clipping deepens. A larger missing series R darkens the real pedal more
    (pulls its Tone corner lower) at light clip, converging toward the model at hard clip. That
    predicts the **model runs too bright / peak too HIGH** in Distortion, worst at light clip depth —
    the opposite of the observed "Distortion peak too LOW" in the P6 table. **Real finding, wrong
    sign — not the cause, don't fix it expecting it to move P6.** (It may still be worth modeling
    correctly on its own merits — it's a legitimate node_HC idealization gap — but that's separate
    from P6 and not prioritized here.)
  - **SW-1/Stage-2 HPF mechanism (candidate for OD's opposite sign): ruled out.** The 159 Hz corner
    (C7) is exactly level-independent by the same ideal-op-amp argument — no mechanism found that
    would move OD's peak the way P6 shows it moving.
  - **schematic-checker's own suggestion for where to look next:** the empirical, mode-asymmetric
    corrections — `odLowShelf` (OD-only by construction) and the even-harmonic injection's per-mode
    coefficients (`asymOD`/`asymDist`, `asymMidFc`/`asymLowFc` split) — or a measurement artifact in
    `_peak_hz`'s broadband argmax.
  - **Metric-artifact check done, ruled out:** pulled the raw per-band dB values around the peak for
    G4 T5 Dist and G6 T5 OD — both are smooth, single, broad maxima (no double-hump, no noise spike)
    genuinely centered one 1/3-octave band apart from the pedal's. `_peak_hz` is reading something
    real, not an interpolation artifact.
  - **`odLowShelf` checked and it's already pulling the right direction, not causing the error:**
    it's a low-shelf lifting *below* 520 Hz, which pulls a broadband peak DOWN — the correct
    direction for OD's too-high error (already accounted for in CLAUDE.md's "roughly halves the
    hot-drive deficit" note; not the cause of the residual, since removing it would make the OD
    error larger, not smaller). Also confirmed `odLowShelf` is NOT what `raw` strips (only
    `driveShelf()` is), so the raw-peak numbers above already include its effect — the true
    fully-uncorrected circuit error for OD is at least as large as the +0.42–0.50 oct raw numbers
    already show, likely larger.
  - **Where this leaves P6:** both circuit-topology candidates for the sign-split are now checked —
    Distortion's real gap is wrong-signed, OD has no comparable gap at all. The remaining live
    leads are the even-harmonic injection's per-mode spectral shaping and/or the accepted
    OD-under-compresses-3–4-dB residual (less compression preserves more HF content at a given
    input level, which biases OD's peak up — direction-consistent, not yet quantified). The
    topology side is exhausted — next step is an isolation experiment, not more circuit tracing.
  - **All of the above is sound and all of it was aimed at the wrong target.** There is no
    mode-differentiated mechanism to find, so its absence was the correct result — see the
    trajectory reframing above. Keep the tracing conclusions (the `node_HC` idealization gap is
    still real and still unmodelled); discard only the premise that drove the search. The planned
    isolation experiment on `injectEvenHarmonic` was **never run and is now moot** — the injection
    is knee-triggered and mode-keyed, and could not have produced a mode-*independent* drive-slope
    deficit of this size.

#### The measurement that identified the cause

**The FR peak is a clip-depth meter.** `analysis/p6_peak_fit.py --in-gain` pre-scales the plugin's
input and deconvolves against the *same* scaled reference, so the result stays a transfer function
and a pure gain cannot move the peak — any movement it produces is nonlinear. It moves it a lot,
and almost linearly: **−0.35 oct per +3 dB** of pre-clip level, consistently across modes and drive
settings. That converts every octave error in the table into a number of dB of missing drive:

| | G2 | G4 | G5 | G6 | G7 | G8 | G10 |
|---|---|---|---|---|---|---|---|
| OD, dB of drive the plugin is short | +0.5 | +0.3 | +0.5 | **+3.2** | +3.7 | +5.5 | +6.8 |
| Dist, same | −1.2 | −2.1 | −2.1 | +0.1 | +1.6 | +4.0 | +6.5 |

Flat to G5, then a steep rise. Two independent checks agree:

- **The best-fit gain the harness must add to the plugin** (`gain_db_applied`, `sweep_clean`) rises
  monotonically with drive in Boost/Clean — the mode with no diode clamp, so output level still
  tracks gain: **−0.69 dB at G2 → +4.83 dB at G10**, a 5.5 dB gain-vs-knob deficit measured without
  reference to the peak metric at all.
- **The time-domain null — the standing arbiter — splits at the same knob position.** Feeding extra
  pre-clip level *hurts* at G5 and *helps* from G6 up:

  | capture | +0 dB | +3 dB | +5 dB |
  |---|---|---|---|
  | G5 T5 OD | **−21.1** | −19.2 | −15.8 |
  | G6 T5 OD | −16.9 | **−22.1** | −20.0 |
  | G8 T5 OD | −12.1 | −14.5 | **−15.1** |
  | G8 T5 Clean | −16.2 | −18.9 | **−19.2** |

  This is what makes it a curve error rather than a level error: a single global gain cannot be
  simultaneously right at G5 and 5 dB short at G8, and the null says both.

#### The fix — `MonarchChannel::driveMakeup`, and why it is not another shelf

The deficit is a **flat level into the clip stages that should grow with the DRIVE knob**. That is
exactly what circuit.md §7's accepted simplification drops. The real DRIVE pot is 3-terminal
(wiper → Stage-1 feedback, pin3 → R6 → C5 → Stage 2); the model uses a 2-terminal rheostat, which
keeps the feedback action and discards the second one. The literal wiring was tried and **rejected
for over-swinging Stage-2 gain (~28 dB vs the measured ~10.6)**, and the 2026-06-29 re-derivation
established precisely what the discarded action does: it **moves Stage 2's flat LEVEL, not Stage 1's
tilt**. So the literal topology had the right shape and the wrong magnitude, and the measurement
above is that shape, fitted:

```cpp
driveMakeupOnset   = 0.5    // G5 — where the null and the peak metric both say the deficit starts
driveMakeupSlopeDb = 14.0   // dB per unit drive past the onset
driveMakeupMaxDb   = 6.0    // cap, reached ~G9
```

applied at NodeG in `processPre`, before `driveShelf`. Stage 1 is linear, so this cannot change
Stage 1's own voicing — only the level the clip stages see. **Below G5 the gain is exactly 1.0, so
every capture at or below G5 renders byte-identical** and none of the G2–G5 fitting from the
bass-cut-bell / drive-shelf passes is disturbed.

**Why no EQ shape is involved, and why that matters:** every previous attempt at this band was
EQ-shaped, and each one reversed sign with drive or lost more null than it gained (the fixed
presence bump, the LF-extension shelf, the tilt shelf). A gain-vs-knob error *looks* like a tilt in
any single-capture FR because clipping converts level into spectral shape — but it cannot be
corrected by one, since the required correction is a different amount at every knob position and
zero at the bottom of the range. **`driveMakeup` carries no filter state at all.**

#### Validation and what remains

Full 44-capture `fr_thd_audit.py peaks`, `sweep_clean` (the trusted level — see the level-dependence
note below):

| | before | after |
|---|---|---|
| all 44, mean | +0.24 oct | **+0.08** |
| all 44, median | +0.11 | **+0.06** |
| all 44, sd | 0.44 | **0.28** |
| G3–G7, max abs | 0.43 | **0.28** |

Per-mode rms over the P6 subset (`p6_peak_fit.py`): OD **0.44 → 0.14**, Dist **0.37 → 0.21**,
Clean **0.66 → 0.35**.

**The null — the arbiter — agrees, over all 44 captures** (`run_validation.py --captures
analysis/pedal_export2`): headline **−22.7…−6.8 dB, median −16.4 → −23.2…−6.6, median −16.6**;
mean **−16.07 → −16.59 dB**. 13 captures deeper, 5 shallower, **26 byte-identical** (every G2–G5
row). The gains land exactly in P6's band, in all three modes:

| deeper | | shallower | |
|---|---|---|---|
| G7 T5 OD | −15.8 → **−20.7** | G10 T2 OD | −8.4 → −7.5 |
| G8 T5 Clean | −16.2 → **−19.8** | G10 T2/T5/T8 Dist | −6.8/−6.8/−7.3 → −6.6/−6.6/−7.1 |
| G8 T5 OD | −12.1 → **−15.1** | G10 T2 Clean | −9.6 → −9.5 |
| G6 T5 OD | −17.0 → **−19.8** | | |
| G7 T5 Clean | −19.9 → **−22.6** | | |
| G6/G7 T5 Dist | −20.7/−19.1 → **−22.2/−20.8** | | |

All nine per-stage validation gates still PASS (`SmokeTest_RC` … `SW2HardClip_Sine`,
`FullChain_DualChannel`, plus `OSFidelity`/`PerfBenchmark`/`TrimLock`).

**Residuals, and why they are left:**
- **G2–G5 is untouched by construction** (gain is exactly 1.0 below the onset), so Distortion's
  −0.24/−0.25/−0.28 oct at G4–G5 survives. That is the *intercept* half of the decomposition, not
  the drift, and it is a genuinely mode-dependent offset — the one place the original "mode split"
  framing does apply. Small, and no mechanism identified; left open.
- **G10 still runs +0.2 to +0.9 oct high.** The measured need there is +6.8 dB and the cap is 6.0;
  raising the cap buys a little peak accuracy and gives it back on the G10 Distortion null. This is
  the documented bass-bloom/high-drive residual and is not fully a drive-curve effect.
- **G10 T2 OD moved −0.15 → −0.72 oct** and is also the one capture whose null gives back ~1 dB.
  Its pedal peak (381 Hz) is non-monotone against its own neighbours (G10 T5 = 230, G10 T8 = 344),
  so this is the one row where the capture, not the plugin, looks anomalous. Flagged, not fitted to.

**Method notes that still stand (do not re-learn these):**
- Read `peaks` **per sweep level** and per mode. Never use its `all:`/`G3–G7` mean — it cancels the
  sign split, which is exactly how Finding 3's wrong ±0.2 claim was produced.
- **`sweep_clean` is the trusted level for this metric.** On `sweep_drv_-6` the same captures do not
  reproduce the clean-sweep sign/magnitude (G5 T5 Dist flips to +0.11; G6/G7 T5 OD drop to
  ~−0.06/−0.10) because the sweep is heavily compressed and the broadband argmax starts tracking a
  different spectral feature. Level dependence here is not monotonic.
- **The "clean" sweep is not quiet at the clip stage.** It is −30 dBFS at the input, but Stage 1 +
  Stage 2 put ~+40 dB in front of both clippers, so at G6 it reaches ~2.7 V — past OD's ±1.64 V
  clamp and Dist's ±0.584 V. Both clip stages are active at every sweep level, which is why the
  plugin's own peaks differ so much by mode at fixed drive (G6 T5: Clean 837, OD 488, Dist 281 Hz).
  Structural, not a bug — and it is why a linear/no-clipping explanation was ruled out early.
- **Any fix must be validated against all 44 captures and against the null**, not the spot-check
  rows. A peak shift carries phase and FR rms is blind to phase.

**Do NOT re-solve this with a shelf.** Every EQ-shaped correction attempted in this band either
reversed sign with drive or lost more null than it gained. A gain-vs-knob error is not a tilt, even
though clipping makes it look like one in any single capture.

### P4 — the 1.6–5 kHz tilt — ❌ **PREMISE WITHDRAWN 2026-07-28. There is no fixed tilt.**

Re-measured on fresh post-P6 data, as the plan required. The tilt is not a fixed linear-EQ error
and `hfTrim` is not the instrument. **P4 is replaced by P7 (below).**

**The trap, for the third time.** Aggregated over all 176 capture × sweep rows the error looks like
a textbook fixed tilt: **+0.23 dB/oct, 99 % sign-consistent (excl. G10), ~1.8 dB total** across
40 Hz–6.4 kHz. Fitting a replacement `hfTrim` collapses the median shape error from **0.379 →
0.073 dB rms** (pivot 4500 → ~2000 Hz, depth −1.30 → −1.95). Every one-axis summary agrees. It is
an artifact: the error is indexed by **DRIVE and SWEEP LEVEL jointly**, and a median over either
axis alone averages the other away. Neither marginal shows the cell where both are extreme. This is
the same failure as Finding 3's "±0.2 oct" and P6's per-capture sign reading — the third instance,
and the first one found *while* building the aggregate.

`analysis/shape_audit.py cross` is the view that breaks it (total dB over 80 Hz–5.12 kHz):

| drive | shelf lift | clean | −18 | −12 | −6 |
|-------|-----------|-------|-----|-----|-----|
| G2 | 3.24 dB | **+3.02** | +1.60 | +1.37 | +1.31 |
| G3 | 2.06 | **+2.45** | +1.46 | +1.01 | +0.64 |
| G4 | 0.88 | +1.20 | +1.56 | +1.52 | +1.39 |
| G5 | 0.00 | **+0.29** | +1.48 | +1.25 | +1.26 |
| G8 | 0.00 | −0.10 | +0.66 | +0.72 | +1.25 |

**The instrument to trust is `shape_audit.py clean` — Boost, clean sweep**, the most nearly-linear
measurement in the set (on a driven sweep the H1 estimator still passes some harmonic energy, so a
mode that distorts more reads hotter at HF for reasons that are not EQ):

| drive | shelf lift | Boost | Overdrive | Distortion |
|-------|-----------|-------|-----------|------------|
| G2 | 3.24 dB | **+3.92** | +2.86 | +0.35 |
| G3 | 2.06 | **+2.45** | +2.47 | −0.69 |
| G4 | 0.88 | **+1.20** | +1.26 | −0.51 |
| G5–G8 | 0.00 | **−0.47 / −0.12 / +0.40 / −0.10** | +1.43 … +1.92 | +0.29 … +1.79 |
| G10 | 0.00 | **+4.89** | +1.61 | +1.70 |

**Where the drive shelf's treble lift is zero, the plugin is flat to ±0.5 dB.** There is no fixed
broadband tilt to correct. Where the lift is non-zero, the error tracks it **≈1:1** in Boost and
Overdrive. Distortion does not show it because the ±0.584 V hard clamp destroys the pre-clip tilt —
so the defect is pre-clip and mode-independent, and Distortion is merely insensitive to it.

**Confirmed on the arbiter, not just on FR.** Removing the lift offline from `G2_T5_Clean`:
FR shape rms **1.51 → 0.44 dB** *and* null **−15.10 → −25.23 dB**. Both metrics move together, which
is what distinguishes this from a magnitude-only illusion. Over the 17 captures with a non-zero
lift, removing 75–100 % of it deepens the null by **2.3–6.0 dB mean on every one of them**, and all
three sweep levels agree on the direction.

**Why the FR-optimal fixed shelf is rejected.** Scored on the null (`offline_null_probe.py null`),
the 2 kHz/−1.95 dB winner of the FR fit is a wash — mean −0.13 dB but median ≈0, 21/44 deeper, with
**+2.5 dB regressions at G5–G6 against −1.9 dB gains at G2**. It splits by drive, because all it was
ever doing was cancelling the treble lift from the other end at the knob positions where the lift
is large. The only fixed variants that are uniformly safe are high-pivot and small
(6500 Hz/−2.80 dB: 39/44 deeper, worst +0.01, mean −0.078) — i.e. the honest ceiling on a *fixed*
HF trim is ~0.1 dB, not the 0.3 dB the FR rms promised.

### P7 — refit the drive-keyed EQ instruments as one set — ✅ **DONE 2026-07-29**

**Largest single improvement in the project's null history: median −16.6 → −21.5 dB over all 44
captures**, best-null range −6.6…−23.2 → −6.6…**−25.1**, mean 2.46 dB deeper. 24 captures deeper
(up to **−9.1 dB**, G2 T6.5 Clean), **18 byte-identical**, 2 shallower (G5 T5 OD +0.1, G5 T8 OD
+0.9). FR rms better on 18, worse on 4 (all ≤0.06 dB). All nine per-stage gates still PASS.

The fix is **one constant set**: the 450 Hz treble lift is **retired** (`shelfMaxDb` 5.6 → 0,
`shelfSlopeDb` 11.8 → 0, guarded by a derived `trebleShelfEnabled` so the audio path and the
harnesses that parse this header cannot disagree), and the bass-cut bell absorbs its job —
`bassCutQ` 0.45 → **0.50**, `bassCutOffDrive` 0.50 → **0.55**, `bassCutSlopeDb` 13.0 → **10.909**,
`bassCutMaxDb` 4.6 → **6.0**. `bassCutPivotHz` stays 185 (it was already right) and `bassBoost*` is
**untouched** (see below). Harness: `analysis/p7_eq_refit.py`.

#### The rule P7 adds: P4's "least-nonlinear instrument" is necessary but NOT sufficient

P4 established *fit on Boost + clean sweep*. That is still right, but the clean sweep **stops being
linear part-way up the DRIVE knob**, and nothing in the pipeline was checking. THD of the clean
sweep, 250 Hz–2 kHz, plugin / pedal:

| G2 | G3 | G4 | G5 | G6 | G7 | G8 | G10 |
|----|----|----|----|----|----|----|-----|
| 0.08/0.87 | 0.09/0.58 | 0.10/0.64 | 0.11/0.79 | 0.43/1.11 | **4.64/4.36** | **7.63/7.71** | **10.45/14.76** |

So the fit window is **G2–G6**; G7 up is a distortion-spectrum difference read through an H1
estimator, not linear EQ. **Check that your instrument is still an instrument at the far end of
the axis you are sweeping** — a cell can be the least-nonlinear one available and still be useless.

**This retires P10's premise.** The "+4.9 dB G10 Boost discontinuity on the clean sweep, 3/3 tones"
is measured where the pedal reads 14.76 % THD and the plugin 10.45 %. It is not an EQ measurement.
See the revised P10 below.

#### The defect is ONE see-saw about 508 Hz

With all three drive-keyed instruments removed (`p7_eq_refit.py raw --base pre-p7`), the underlying
defect collapses to a single tilt. The **508 Hz band reads −0.16…−0.31 dB at every drive including
G10** — that near-constancy across the whole knob is what identifies it as the pivot, not an
assumption. The correction the plugin needs, in dB:

| drive | 202 Hz | 508 Hz | 3225 Hz | tilt (HF−LF) |
|-------|--------|--------|---------|--------------|
| G2 | −2.15 | −0.28 | +1.80 | **+3.95** |
| G3 | −1.54 | −0.25 | +1.19 | +2.73 |
| G4 | −0.90 | −0.23 | +0.35 | +1.25 |
| G5 | −0.46 | −0.16 | −0.26 | +0.20 |
| G6 | −0.20 | −0.24 | −0.18 | +0.02 |
| G7 * | +0.20 | −0.19 | −0.87 | −1.07 |
| G10 * | +3.50 | −0.31 | −2.44 | −5.94 |

The low side is a **bell** (it rolls off below 100 Hz, it is not a shelf); the high side plateaus
above ~1.6 kHz. The shipped law shape — `max(0, max − slope·drive)`, saturating near G5 — already
reproduces that trajectory almost exactly. **The law was right; only the magnitude was wrong.**

#### Why measuring one instrument alone lied

The treble lift and the bell were each supplying **about half of the same correction**, so together
they delivered ~6.6 dB of tilt at G2 where 3.95 dB is needed — a 1.65× overshoot that neither
instrument's own history would reveal. P4 measured the lift alone, found removing it fixed G2, and
concluded it was 100 % spurious. That reading was right about the *outcome* at G2 and wrong about
the *reason*: removing 3.04 dB of lift left the bell's 3.9 dB standing, which happens to be near
the 3.95 dB actually required. **When two corrections overlap in band and in keying, neither can be
read without the other in the same measurement.**

#### What was tested, and what the arbiter picked

Both candidates were fit on FR and then judged on the null over a 21-capture low-drive-weighted
subset (`p2_rail_asym_fit.py --captures …`, ~22 s per iteration):

| candidate | FR rms G2–G6 | null Δ clean | drv_−12 | drv_−6 |
|-----------|--------------|--------------|---------|--------|
| shipped | 0.941 dB | — | — | — |
| A: shelf reduced to 1.75 dB + bell 4.75/Q0.60 | 0.262 | −2.54 | −1.37 | −0.96 |
| **B: shelf DELETED + bell 6.0/Q0.50/off 0.55** | **0.259** | **−3.04** | **−1.76** | **−1.28** |

B wins on the arbiter in every mode *and* is one instrument simpler, so the simpler answer did not
have to be argued for. Its neighbours (Q 0.45; maxDb 6.5) both land within ±0.05 dB on the null —
a flat optimum, not a knife edge. FR shape rms over the fit window: **0.941 → 0.259 dB**, and G2's
rows individually 1.17–1.40 → 0.17–0.24.

**Leave-one-drive-out shows no overfit**: refitting without a drive and scoring on it gives
essentially the fitted numbers (G2 held out 0.253 vs 0.211 fitted; G4/G5/G6 identical to 3 dp).

**Containment is structural, not lucky**: both instruments are exactly zero at and above drive
0.55, so all 18 captures at G6 and above are **byte-identical**. Nothing P6 fitted above G5 moved.

#### The one axis neither metric scored — measured after the fact, and it is fine

Both instruments used here are **level-blind**: `p7_eq_refit.py` mean-removes every row, and the
null applies a best-fit gain. So the *flat* component of the refit was unconstrained — and it is
not zero (removing a treble lift and deepening a bass cut both take broadband energy out). Checked
afterwards on `gain_db_applied`, tone-averaged, Boost:

| | G2 | G3 | G4 | G5 | G6–G10 |
|---|----|----|----|----|--------|
| clean sweep, before | −0.95 | −0.49 | −0.08 | +0.22 | −0.17…+0.82 |
| clean sweep, after | **+1.17** | +0.89 | +0.58 | +0.42 | unchanged |
| **−6 dB driven sweep, Δ** | **+0.12** | +0.09 | +0.06 | +0.05 | 0.00 |

At **playing level the shift is ≤0.12 dB** — the clipper absorbs a pre-clip level change, which is
also why the driven nulls improved so much anyway. On the quiet clean sweep G2 moves ~2 dB, but the
*spread across drive is unchanged* (sd 0.52 dB before and after); the V-shape became a monotone
decline. No corrective instrument is warranted, and adding one pre-clip would fight the null gain
just won. Recorded because "the metric is blind to X" is how this project's last four errors
happened — here X was flat level, and it was checked rather than assumed.

(Ignore the OD/Distortion columns of that table for absolute level: per-mode capture levels are
**normalised**, so OD sits ~+7 dB and Distortion ~+14 dB by construction. Only within-mode
differences mean anything.)

#### `bassBoost*` deliberately left alone — it belongs to P8

Freeing the 105 Hz low-shelf in the same fit bought 0.262 → 0.242 dB and wanted to move its pivot
to **75 Hz**, i.e. straight into P8's band, on a fit window where the instrument is barely active
(its real job is G7+, which is exactly where the clean sweep stops being measurable). That is the
entanglement P8 already warned about, arriving from the other direction. Left untouched.

### P8 — the LF band is ONE drive-keyed instrument, and it was fit to one end of the axis — ✅ **DONE (2026-07-29)**

> **Result up front (all 44 captures, `run_validation.py`):** best-segment null **−25.1…−6.6,
> median −21.5 → −25.6…−8.7, median −22.6 dB**. Mean and median both **1.05 dB deeper**; **38 of 44
> deeper** (best −3.4 dB, G8 T5 Clean), **6 shallower by at most +0.2 dB**, none worse than that.
> The **worst capture in the set improved 2.1 dB** (G10 T2 Dist −6.6 → −8.7) — the first time
> anything has moved the G10 floor. All nine per-stage gates PASS; SMPTE IMD median +0.05 dB
> (worst +0.83), CCIF −0.00; FR rms err mean 2.12 → 2.06 dB.
>
> **And the fix is not what this section planned.** The plan was to ADD a min-phase low-shelf at
> ~100 Hz. What shipped is a **refit of the low-shelf that was already there** — `bassBoost*`, from
> a monotone ramp into a **hump in drive**. See "What it actually was" below.

P1 closed this as "real but not correctable — the pedal's LF excess carries a phase lead, so every
minimum-phase EQ that fixes the magnitude worsens the null." **That conclusion generalised from two
data points and is wrong for most of the band.** P1's *measurements* were all correct.

**What the complex transfer actually shows** (`offline_null_probe.py transfer`, clean sweep, G5/G6
where the drive shelf is off so the reading is uncontaminated; + = pedal LEADS):

| capture | 20 Hz | 32 Hz | 40 Hz | 50 Hz | 63 Hz | 80 Hz |
|---------|-------|-------|-------|-------|-------|-------|
| G5 T5 Clean | +3.5 dB / **+20°** | +2.7 / +4° | +2.1 / **−3°** | +1.4 / **−7°** | +0.7 / **−9°** | +0.1 / −9° |
| G6 T5 Clean | +2.3 / **+27°** | +1.8 / +11° | +1.4 / +4° | +1.0 / **0°** | +0.5 / **−3°** | +0.1 / −4° |

The phase lead is real **only below ~32 Hz**. From 40–80 Hz the deficit is still +0.5 to +2.1 dB and
the pedal **lags** by 3–9° — which is exactly what a minimum-phase low-shelf supplies. In that band
it is the *right* instrument, not the wrong one.

**P1 tested two shelves, both roughly twice too deep and centred too low**, because both were fit to
zero the **FR magnitude** rather than the complex residual. Reproduced on the null, all 44 captures ×
4 sweep levels, full segments so 20–40 Hz is inside the scored window:

| shelf | mean Δ null | median |
|-------|------------|--------|
| 60 Hz +3.5 dB | **+0.50** (worse) | +1.23 |
| 25 Hz +5.0 dB | **+0.78** (worse) | +0.79 |
| **100 Hz +1.0 dB** | **−0.44** (better) | **−0.64** |
| 100 Hz +1.65 dB | −0.46 | −0.66 |
| 100 Hz +2.5 dB | −0.10 | +0.25 |

Scored on the complex residual `|D/S − 1|` weighted equal-energy-per-octave (`offline_null_probe.py
shelf`), the optimum is **fc ≈ 75–100 Hz, +1.65 dB**, cutting the 30–120 Hz residual by **34–38 %**
— and it improves **every** sub-band including 20–32 Hz (0.527 → 0.412), because the magnitude error
down there is large enough that correcting *part* of it beats the phase penalty. P1's arithmetic
assumed full magnitude correction and a 33° lead; the measured lead at G5/G6 is 20–27°.

**Not a corner error.** To produce +2.7 dB at 20 Hz, Stage 2's 159 Hz HPF would have to sit at
~116 Hz, i.e. C7 = 137 nF against a 100 nF ±10 % part — not a tolerance. And a *lower* corner gives
*less* phase lead, the wrong direction for the 20 Hz reading. P1's topology tracing stands; the fix
is genuinely artificial EQ, which is authorised.

**Caveat that sets the order: this is drive-indexed too.** The +1.0 dB shelf gives −1.5 to −2.0 dB at
G2–G4 and **+0.6 to +1.5 dB at G6–G8**, mode-independent. At G2–G4 it is entangled with the treble
lift (a tilt error reads as "needs more bass" after normalisation), so **P8 must follow P7** and be
re-measured on the corrected baseline. The sub-32 Hz remainder stays non-minimum-phase — though the
lead needed is ~20° at 20 Hz ≈ **2.8 ms**, not the tens of ms P1 assumed, so even that is not
obviously out of reach if it is ever worth the latency.

#### What it actually was — the caveat was the whole finding

Re-measured on the post-P7 baseline (`comprehensive_report.py --keep-renders`, then
`offline_null_probe.py`), that "caveat" is not a caveat. It is the answer. The fixed +1.0 dB shelf
scores on the arbiter like this — all 44 captures × 4 sweep levels, mean Δ null by drive:

| candidate | mean | G2 | G3 | G4 | G5 | G6 | G7 | G8 | G10 |
|-----------|------|----|----|----|----|----|----|----|-----|
| fixed 100 Hz +1.0 dB | −0.30 | −0.96 | −1.59 | −1.63 | −0.93 | **+0.59** | **+1.54** | **+1.34** | **+0.58** |

Helps at G2–G5, hurts at G6–G10, in **every** mode. That is the probe's own stated tell — *a
knob-indexed result means a fixed filter is the wrong instrument* — and P7's rule says what to do
about it: **there is already a drive-keyed instrument in this band, so read them as one set.**
`bassBoost*` (105 Hz low-shelf, onset G2.5, 7.5 dB/unit, cap 4.2 dB) has occupied 20–120 Hz since
2026-06-29. P7 saw this coming from the other direction and deliberately did not touch it
(§"`bassBoost*` deliberately left alone — it belongs to P8").

Fitting the shelf depth **per drive** at a pinned 85 Hz pivot, scored on the complex residual
(`offline_null_probe.py shelf --pivot 85`), gives the correction the pedal wants *on top of*
whatever `bassBoost` already supplies:

| | G2 | G3 | G4 | G5 | G6 | G7 | G8 | G10 |
|---|----|----|----|----|----|----|----|-----|
| extra dB wanted at 85 Hz | +1.30 | +1.65 | +1.85 | +2.00 | +1.35 | +0.05 | **−0.95** | **−2.10** |
| shipped `bassBoost` (105 Hz) | 0.00 | 0.37 | 1.13 | 1.88 | 2.62 | 3.37 | 4.12 | 4.20 |

**The old law was fit to one end of the drive axis.** It exists to counter the high-drive bass
bloom, it was measured at high drive, and it was read as monotone because nothing had ever measured
the low end. Measured across the whole axis the pedal wants LF gain at **every** drive — already
+1.2 dB at G2, where the ramp gives exactly zero — rising to a peak near G5 and then **falling
back**. The ramp is ~1.2 dB short below G5 and ~2.4 dB **over** at G10, and the fall is a shape a
ramp cannot express at all.

So P8 ships **no new instrument**. `bassBoost*` becomes a hump: peak `bassBoostMaxDb` at
`bassPeakDrive`, falling `bassBoostSlopeDb` per unit drive below and `bassBoostFallDb` above,
floored at 0. Fitted on the arbiter (three rounds, ~20 candidate laws) to **85 Hz, 3.0 dB @ drive
0.50, 6.0 down / 2.5 up** — the floor is reached exactly at drive 0, so it never binds in range.

| | G2 | G3 | G4 | G5 | G6 | G7 | G8 | G10 |
|---|----|----|----|----|----|----|----|-----|
| was (ramp) | 0.00 | 0.37 | 1.13 | 1.88 | 2.62 | 3.37 | 4.12 | 4.20 |
| now (hump) | 1.20 | 1.80 | 2.40 | 3.00 | 2.75 | 2.50 | 2.25 | 1.75 |

The offline optimum is **broad** — every law within 85±10 Hz, 2.8–3.5 dB, peak 0.48–0.55 scored
within 0.07 dB of the best — so the constants were not ground finer offline than the
pre-clip/post-clip placement caveat can carry.

**Where it shows in FR** (clean sweep, plugin − pedal, by drive): the *drive-dependence* of the
20 Hz error more than halves, −3.23…+2.95 dB → **−2.12…+0.59**, and 40 Hz −1.57…+1.28 →
−0.59…−0.84. What is left at 20 Hz is a near-constant ~2 dB shortfall — **the sub-32 Hz
non-minimum-phase remainder P8 predicted would stay**, since an 85 Hz first-order shelf deep enough
to reach 20 Hz overshoots 80 Hz (which is already +0.9 dB at G2).

**FR at 80–320 Hz reads worse at G8–G10 while the null there gets better** (G8 T5 Clean −19.8 →
−23.2). Believe the null: per P7's rule the clean sweep carries 7.6–15 % THD at those drives and is
not a linear FR measurement, and the null is scored on all four sweep levels.

**Harness note — a stale-baseline trap that nearly landed.** `comprehensive_report.py
--keep-renders` writes `<label> tommy_test_signal_48k_plugin.wav`; `run_validation.py --render-dir`
writes `<label>.wav`. Both land in `/tmp/monarch_renders`, and `offline_null_probe.load_pairs`
matched only the second — so a fresh render run left the *older* set in place and the probe read it
without complaint. Caught by noticing the two namings, and the fit was re-run against a
known-good post-P7 set: identical to 4 decimals, so nothing here rests on it. `load_pairs` now
accepts both forms, takes the newest per label, and **warns when the renders in a directory span
more than two minutes**.

### P9 — Overdrive's mode-specific tilt and its THD roll-off  *(open, unworked)*

Overdrive carries **+1.4 to +2.5 dB of tilt at every drive, including where the drive shelf is off**
(`shape_audit.py clean`, Overdrive column). It is the only mode that does. The same fact shows up in
THD: on the hot sweep OD's distortion falls off far faster with frequency than the pedal's —
Δ −0.8 dB at 320 Hz growing monotonically to **−4.1 dB at 5 kHz**, with the pedal holding 11–15 %
while the plugin drops 11.9 → 6.9 %. This is the documented "OD compresses 3–4 dB lighter" residual,
but it has a **shape**, which has never been worked. Do not paper it over with a linear shelf — it
is a distortion-spectrum difference, and the accepted-residuals list should stop describing it as a
flat offset.

### P10 — the G8 → G10 Boost discontinuity  *(open — but the PREMISE is withdrawn, 2026-07-29)*

**Original claim:** G10 Boost reads +4.9 dB of tilt on the clean sweep at all three tone settings
against G8 Boost's −0.10 — a sharp, tone-independent discontinuity, 3/3 captures, not capture noise.

**P7 withdrew the measurement.** That reading comes from a sweep carrying **10.45 % THD (plugin) /
14.76 % (pedal)** — see P7's instrument-validity table. `shape_audit.py clean` restricts to the
least-nonlinear *cell*, but nothing was checking that the cell was still linear at the far end of
the drive axis, and above ~G6 it is not. A tilt measured through an H1 estimator on a 10–15 %-THD
sweep is a distortion-spectrum difference wearing an EQ costume. "3/3 tones, not capture noise" is
true and irrelevant — it is consistent because the *distortion* is consistent.

**What survives:** something at G10 is real (the nulls are −6.6 to −10.3 dB there, far the worst in
the set, and `driveMakeup` does cap at 6.0 dB against a measured G10 need of +6.8). But it is not
established to be linear EQ, and the clean sweep cannot decide it. **P10 needs a different
instrument before it needs a fix** — a discrete-tone or level-stepped measurement, i.e. it should
be folded into the never-audited dynamics/THD axes rather than pursued as a tilt.

### Axes never audited at all

`comprehensive_data.json` carries only `fr`, `thd`, `harmonics`, `h2`. These are in the captures but
have never been compared against the plugin, and P2/P3/P6 all changed the clip path underneath them:

- **IMD** (`imd_smpte`, `imd_ccif`) — used only as a *guard* in `p2_rail_asym_fit.py`, never as a target.
- **Dynamics / compression** (`lvl_-30` … `lvl_-3` steps) — the source of the "OD compresses lighter"
  claim, not re-measured since. Likely the same underlying thing as P9; look at them together.
- **Discrete-tone THD** (`tone_82` … `tone_8000`) — computed by `comprehensive_report.py` but not emitted.
- **Decay** (`decay_220`, `decay_1k`) — never examined.
- **Red channel** — no NAM reference; unvalidatable by construction.

---

## Regenerating the tables

```bash
python3 analysis/comprehensive_report.py            # refresh comprehensive_data.json (needs captures)
python3 analysis/fr_thd_audit.py all                # every table in this doc except h2
python3 analysis/fr_thd_audit.py raw                # Finding 1 — driveShelf removed
python3 analysis/fr_thd_audit.py bands --by tone    # Finding 2 — the knob-variance test
python3 analysis/fr_thd_audit.py peaks              # Finding 3
python3 analysis/fr_thd_audit.py harm               # Finding 4 — H2–H7 anchors
python3 analysis/fr_thd_audit.py evens --base OLD.json  # P3/P3.1 — even-series rms+bias, before/after
python3 analysis/fr_thd_audit.py alias              # Finding 4 — why 6/8 kHz THD is invalid
python3 analysis/p31_harm_floor.py                  # P3.1 step 0 — capture-chain harmonic floor
python3 analysis/p6_peak_fit.py                     # P6 — FR-peak + compression-tilt subset (~15 s)
python3 analysis/p6_peak_fit.py --in-gain 3 6       # ...the clip-depth calibration probe

python3 analysis/shape_audit.py cross               # P4/P7 — THE view: drive x sweep-level cross-tab
python3 analysis/shape_audit.py clean               # P4/P7/P9 — Boost clean sweep, the linear instrument
python3 analysis/shape_audit.py tilt                # ...the marginal that LOOKS conclusive and isn't

python3 analysis/p7_eq_refit.py raw --base pre-p7   # P7 — the defect with the drive-keyed set REMOVED
python3 analysis/p7_eq_refit.py seesaw              # ...the same defect as one see-saw about 508 Hz
python3 analysis/p7_eq_refit.py fit                 # ...refit all of them together (~3 min)
python3 analysis/p7_eq_refit.py score --base pre-p7 # ...per-capture residual, before/after

# P8 / any EQ hypothesis — needs renders kept from a comprehensive_report.py run:
python3 analysis/comprehensive_report.py --keep-renders /tmp/monarch_renders
python3 analysis/offline_null_probe.py transfer     # complex pedal/plugin D(f): magnitude AND PHASE
python3 analysis/offline_null_probe.py shelf --band 30 120          # fit on the COMPLEX residual
python3 analysis/offline_null_probe.py shelf --pivot 85 --drive 0.2 0.6  # ...best dB PER DRIVE
python3 analysis/offline_null_probe.py null --low-shelf 100:1.0     # score it on the arbiter
python3 analysis/offline_null_probe.py null --bass-law 85:3.0:0.50:6.0:2.5   # P8 — score a
#   REPLACEMENT bassBoost law (PIVOT:MAX:PEAKDRIVE:SLOPELO:SLOPEHI) as the delta against the
#   shipped one, parsed live from MonarchChannel.h. Repeating the shipped law must score 0.000 —
#   that is the harness's own sanity check, run it first.
python3 analysis/fr_thd_audit.py h2                 # Finding 4 — H2 vs frequency (renders; needs PedalRender)

python3 analysis/p2_rail_asym_fit.py --json run.json            # P2's fit/guard loop (~12 s)
python3 analysis/p2_rail_asym_fit.py --compare run.json         # ...and the A/B against a saved run
```

`p7_eq_refit.py` reads only the JSON, so its `--base` must be told what the JSON's renders were
made WITH whenever the header has moved on since (`--base pre-p7` is the shipped shorthand).
Everything else in this file parses `MonarchChannel.h` live and stays correct automatically —
`shape_audit.py` did NOT until P7 fixed it, and its hardcoded `SHELF_MAX_DB = 5.6` would have gone
on printing a treble lift that no longer exists.

`p2_rail_asym_fit.py` is the fast loop P2 **and P3** were fitted with (~16 s for a 16-capture
subset, against ~10 min for the full `comprehensive_report.py`): it renders a small capture subset and
prints only the three things a clip-nonlinearity change is judged on — the H2–H7 anchors (same
extraction as `fr_thd_audit.py harm`, so the numbers are comparable), the per-segment time-domain
null, and a render SHA-256 for byte-identical guards. Use it to iterate; use
`comprehensive_report.py` + `run_validation.py` to conclude. **It reads the clean sweep's harmonics
too** — that column is what showed the clean sweep was exactly linear and sent the investigation
to the DC tail instead of the clipper.

Captures are local-only/gitignored, so all of the above need `analysis/pedal_export2/` present.
The `raw` view parses the shelf constants live out of `src/dsp/MonarchChannel.h`, so it stays
correct as the shelves are retuned — but **the numbers quoted in this doc are the 2026-07-26
baseline** and will shift once P1–P4 land. Re-run before drawing new conclusions.
