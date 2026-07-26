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
| 1 | Universally light 20–80 Hz | **Confirmed** — largest defect in the plugin; present in the raw circuit at every drive |
| 2 | A touch hot from 800 Hz up; maybe knob variance | **Confirmed as real, rejected as knob variance** — it does not track the TONE knob at all |
| 3 | The FR peak moves and the plugin doesn't match it | **Confirmed it moves, but it is a readout of #1/#2 + the known bloom** — no separate mechanism |
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

This deficit also **causes** the LF THD shortfall in Finding 4: the plugin's low end never reaches
the rails. G10 T5 Clean at the −6 dB sweep, 40 Hz: pedal **35.6%** THD, plugin **4.7%**.

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

## Finding 3 — the wandering FR peak is a readout, not a mechanism

Overall-FR peak location, plugin vs pedal, in octaves (`fr_thd_audit.py peaks`):

- **G3–G7, all three modes: within ±0.2 oct.** The plugin tracks the pedal wherever the tilt is right.
- **G2 Clean: +0.16 to +0.30 oct** — the bass-cut bell over-cutting at low drive.
- **G8–G10: +0.35 to +1.75 oct** (G10 T8 Clean: plugin 1216 Hz, pedal 362 Hz) — the documented
  high-drive bass bloom.

Every peak error sits directly on top of a tilt error already identified above. **No separate work
item** — re-measure after Findings 1/2 are fixed and use it as a check.

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

Boost in the plugin is effectively a **symmetric** clipper; in the pedal it is strongly
**asymmetric**. That is the "clean harmonics look off" observation, and it is a 26–46 dB gap, not a
subtle one. The pedal's H2 also has strong frequency structure that the plugin's flat injected H2
does not reproduce at all (`fr_thd_audit.py h2`, −6 dB sweep):

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

Ordered. **P1 changes what P2/P3 must be fitted against** (more low end into the clipper changes LF
THD and the H2 gate), so do not run them in parallel — finish P1, re-run
`comprehensive_report.py`, then fit the nonlinear work against the corrected baseline.

### P0 — harness hygiene (cheap; changes what the dashboard tells you)

- Route THD bands above ~5 kHz to `na` in `build_band_source_map`, or annotate them: the Farina
  path is H2-only up there and the discrete-tone fallback aliases onto the fundamental at 6/8 kHz.
- Mark FR bands above ~8 kHz as capture-unreliable in `dashboard_gen.py`. The ±18 dB spread there
  currently reads as plugin error.
- Add **H2 vs frequency** as a first-class chart. It is the single most diagnostic view of the
  even-harmonic problem and it is not in the report today (only 3 anchor frequencies are).

### P1 — sub-64 Hz LF extension  *(biggest win; drive-independent; fixes LF THD in all modes for free)*

1. **Run `schematic-checker` before fitting anything.** Specific question: what produces a
   pole/zero pair below ~60 Hz that the 2-terminal DRIVE approximation drops? The literal
   3-terminal wiring's `pin3 → R6 → C5 → Stage2` path is the obvious suspect — it was rejected for
   its *gain* behaviour (circuit.md §7) but its **LF contribution was never separately evaluated**.
   Also re-verify C7/R9 against both schematics.
2. If there is a topology fix, take it. If not, add a **separate, drive-independent low-shelf**
   (~+3.5 dB, pivot ~50 Hz) to the `driveShelf` chain. It must **not** be folded into `bassCut` or
   `bassBoost` — the 100–330 Hz fit is already good to ±0.3 dB at G5 and either would break it.
3. Validate with Farina `linear_tf` (this is a pure linear-EQ question) **plus** the time-domain
   null. Expect the G8–G10 nulls and the LF THD rows to improve as a side effect.

### P2 — asymmetric op-amp rail saturation  *(circuit-accurate, not empirical)*

Real JRC4580 output saturation is asymmetric (Voh and Vol differ), and the Theseus measured pins
put bias at 4.5 V against VCC/2 = 4.575 V. Give `railSaturateADAA` **separate positive/negative
ceilings**.

This generates the whole even series H2+H4+H6 with the correct internal ratios automatically —
which is exactly what the data demands, since H3/H5/H7 already match and only the evens are
missing. Fit the asymmetry to the Boost H2/H3 ratio (pedal: H2 −21.2, H3 −15.9 at the −6 dB sweep,
100 Hz). Then check whether the empirical `asymBoost` (0.35) can be **deleted entirely**.

**Guard:** confirm OD/Distortion stay byte-identical. They clamp at the diodes (±1.64 / ±0.584 V)
far below the rails — that is the existing tone-safety argument and it must survive.

### P3 — even-harmonic series shape in OD/Distortion  *(depends on P2's outcome)*

If P2's asymmetric rails plus an asymmetric diode-clamp offset produce the H4/H6 series naturally,
prefer that over more injection. Otherwise re-fit `asymLowOD` / `asymLowDist` so the low-band path
**washes out at high clip depth** the way the mid/high `tanh` path already does — the level trend
is currently backwards (pedal H2 falls −45.8 → −52.1 from the −12 to the −6 sweep; plugin rises
−43.9 → −34.5).

**Validate with the harmonic anchors + the time-domain null, NOT `linear_tf`** — it mis-reads
clip-gated corrections (the odLowShelf lesson, dsp.md).

### P4 — the 1.6–5 kHz tilt  *(~1 dB; do it last)*

Lower the `hfTrim` pivot to ~1.5–2 kHz with a shallower depth, or add a second gentle shelf.
Confirm **by ear as well as by null** — it is the band the ear is most sensitive in, and a 1 dB
change barely registers in the null test.

### P5 — FR peak: verification only, no separate work

Re-run `fr_thd_audit.py peaks` after P1/P4. G3–G7 should stay within ±0.2 oct and G2/G8–G10 should
tighten. The G8–G10 residual is the documented bass-bloom limit.

---

## Regenerating the tables

```bash
python3 analysis/comprehensive_report.py            # refresh comprehensive_data.json (needs captures)
python3 analysis/fr_thd_audit.py all                # every table in this doc except h2
python3 analysis/fr_thd_audit.py raw                # Finding 1 — driveShelf removed
python3 analysis/fr_thd_audit.py bands --by tone    # Finding 2 — the knob-variance test
python3 analysis/fr_thd_audit.py peaks              # Finding 3
python3 analysis/fr_thd_audit.py harm               # Finding 4 — H2–H7 anchors
python3 analysis/fr_thd_audit.py alias              # Finding 4 — why 6/8 kHz THD is invalid
python3 analysis/fr_thd_audit.py h2                 # Finding 4 — H2 vs frequency (renders; needs PedalRender)
```

Captures are local-only/gitignored, so all of the above need `analysis/pedal_export2/` present.
The `raw` view parses the shelf constants live out of `src/dsp/MonarchChannel.h`, so it stays
correct as the shelves are retuned — but **the numbers quoted in this doc are the 2026-07-26
baseline** and will shift once P1–P4 land. Re-run before drawing new conclusions.
