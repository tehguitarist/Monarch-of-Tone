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

**CMake minimum:** 3.15 | **C++ standard:** 17 | **macOS target:** 10.13+ | **Version:** 1.4.0
(single-sourced from `project(MonarchOfTone VERSION …)` in CMakeLists.txt → `MONARCH_VERSION_STRING`
→ the UI's version label; never hardcode it anywhere else)

---

## Roadmap

- **v1.0 — Project cleanup.** ✅ Done — repo/docs audited and condensed.
- **v1.1 — CPU / latency / memory optimization pass.** ✅ Done (2026-06-29).
- **v1.4 — FR / harmonic accuracy pass.** ✅ Done (2026-07-29). Full findings and the
  worked P0–P10 plan → `analysis/FR_THD_AUDIT.md`. The ordered plan is finished; all residual
  gaps are accepted and documented.
- **v1.5 — CPU pass + the ADAA identity-region droop.** ✅ Done (2026-07-30). Full
  findings, span split, every lever's measured cost, rejected levers with evidence, and
  measurement protocol → `analysis/CPU_AUDIT.md`. Steps 1–5 are shipped.

**Audit docs:** `analysis/FR_THD_AUDIT.md` (v1.4 accuracy), `analysis/CPU_AUDIT.md` (v1.5 CPU),
`analysis/MID_EQ_AUDIT.md` (mid-EQ correction audit), `analysis/VALIDATION_REPORT.md`
(per-capture null breakdown).

---

## Repository State

**Engine + UI complete and validated; matched to NAM captures of a real King of Tone.** All DSP
stages, `MonarchChannel`, `processBlock`, and oversampling are done (build.md Validation Gates,
all PASS, auval PASS). The UI is complete: peripheral shared-look shell (side panels, OS strip,
resizable window) + the purple/gold `PedalFace`. Factory presets (5) ship via the host's native
preset browser. Supply-voltage mod (9/12/18V) and rail-saturation ADAA are in. CI/CD
(`.github/workflows/`), cross-platform VST3, and per-platform installers (`installer/`) — see README.

**Calibration result:** the plugin nulls against 44 NAM captures (drive G2–G10, tone T2–T8,
Clean/OD/Dist) at **−8.6 to −27.0 dB, median −23.4 dB**, and is at **−22.0 dB or better on every
capture from G2 to G7**. Per-mode at G5 T5: Clean **−23.9**, OD **−24.8**, Dist **−23.1**. The
match is shallower only at very high drive (G8–G10) — an accepted device-physics / capture-aliasing
residual, not a topology error. The 44 captures (`analysis/pedal_export2/`, 842 MB) are
**local-only, gitignored**.

### Performance (v1.5 step 5, 2026-07-30)

`PerfBenchmark` — one CPU core, 48 kHz, both channels active in series, stereo:

| OS | Boost | Overdrive | Distortion |
|----|-------|-----------|------------|
| 1x | 2.1% | 3.8% | 2.8% |
| 2x | 2.8% | 5.3% | 3.6% |
| 4x | 4.8% | 9.8% | 6.5% |
| 8x | 8.6% | 18.8% | 12.1% |
| render | — | — | 11.1% |

### Accepted residuals

- **Sub-32 Hz shortfall** (~2 dB at 20 Hz). Not a topology error; the pedal's phase *leads*
  below ~32 Hz, so no minimum-phase filter matches. P1 tried, P8 narrowed it significantly.
  Documented in `FR_THD_AUDIT.md` P1/P8.
- **G8–G10 clip-path behaviour** — the OD ceiling over-corrects at G10 (wrong-signed by
  construction: the pedal's OD compresses *less* at G10 while a level-keyed ceiling bites
  *more*). About half is target-side noise (the pedal's OD curve varies 1.19 dB across TONE,
  a knob that categorically cannot affect it). P10 step 3 measured the target's floor and
  accepted the residual. See `FR_THD_AUDIT.md` P9/P10.
- **HF harmonic difference above 8 kHz** (tone-stage rolloff; the captures' own 4–6 kHz energy
  is partly NAM aliasing).
- **Per-mode capture-level normalization** — A/B and null tests must re-gain per mode.
- **Red drive-shelf keying** — drive-dependent EQ shelves are knob-keyed and Yellow-fitted.
  Red has no NAM reference; ~1–2 dB low-mid over-cut at low drive. Deferred. See dsp.md.
- **Diode-stage ADAA** — chowdsp_wdf doesn't support it for nonlinear root solves. 4x
  oversampling on render is sufficient in the meantime.

### Performance measurement protocol

`PerfBenchmark` is load-sensitive. Protocol: rebuild both arms, measure back-to-back, ≥2
passes, nothing else running. Keep a no-op control arm (1x) — if it moves, the measurement is
contaminated. Never compare two numbers taken at different times. See `CPU_AUDIT.md` §0.

---

## Key Circuit Facts

| Fact | Value |
|------|-------|
| Op-amp | JRC4580D per channel (matsumin label JRC4558D is wrong); modelled ideal + rail saturation |
| Stage 1 (IC_A) | Non-inverting — no `PolarityInverterT`; two-one-port solve (no R-type matrix) |
| Stage 1 Z_lower | C4(10n) series [ R4(27k) ∥ (R5(33k) + C3(10n)) ] — Theseus topology |
| Stage 1 Z_upper | (floor + DRIVE 0–100k) ∥ C2(100pF); Av(s) = 1 + Z_upper/Z_lower, DC gain 1 |
| Stage 1 feedback floor | **Yellow R2∥R3 ≈ 990 Ω** (stock) / **Red ≈ 17.7 k** (tamed Hi-Gain = R6_floor + DRIVE_max/6; voicing choice over the literal R2=100k — shifts Red's drive curve +⅙ knob, i.e. Red@d≈Yellow@(d+1/6). `hiGain` ctor flag |
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

---

## Three Most Likely Implementation Mistakes

1. **`DiodeT` instead of `DiodePairT`** — both clipping stages use symmetric matched pairs.
2. **Audio taper on DRIVE or TONE** — both are linear (B-taper). Only VOL is audio taper.
3. **Reading an R-type output off a source port** — read passive ports only (the source port
    2-point-averages → a spurious HF droop; this once dragged Stage 1's peak down ~880 Hz). And
    Stage 2's inversion lives in the VCVS terminal assignment — the gate is the measured −22 gain.

---

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
  `fr_thd_audit.py` reads that JSON and produces the tables in **`FR_THD_AUDIT.md`** — its `raw`
  view strips `driveShelf()` to separate a mis-tuned correction shelf from a real circuit gap, its
  `evens` view is the whole-set even-harmonic rms/bias table.
- `p31_harm_floor.py` measures the **capture chain's harmonic noise floor** by gating the Farina IR
  at fractional orders.
- `p6_peak_fit.py` (FR-peak + compression-tilt subset harness), `shape_audit.py` (FR error *shape*
  cross-tab), `p7_eq_refit.py` (drive-keyed EQ set refit), `offline_null_probe.py` (score EQ
  hypotheses without rebuilding).
- `p9_ceiling_fit.py` + `p9_pin7_probe.cpp` (P9 clip-path analysis: `static` = admissibility
  test, `floor` = resolvability, `need` = right shape), `p9_od_compression.py` (decay and knee
  analysis). All `p9_*` views read `/tmp/monarch_renders`.
- **CPU harnesses** → `analysis/CPU_AUDIT.md` §8. `perf_split_probe.cpp` (per-sample cost by
  SPAN) and `byte_identity_probe.cpp` (full-precision dumps with `cmp`, including mid-stream mode
  changes).
- `tools/PedalRender` renders a WAV through the real processor (Yellow-only) for A/B.
- **Absolute levels are a per-capture property on every axis** — each of the 44 captures is an
  independently trained NAM model. Only x-axis quantities (clip knee), self-anchored shapes, and
  dBc ratios cross captures. Never read a knob-indexed level difference as a plugin defect.
- **Bands not trustworthy:** FR above ~8 kHz (±18 dB capture-side spread) and THD above ~5 kHz.

### Key rules discovered during calibration

These are hard-won guardrails from v1.4/v1.5 — violating any of them has been shown to produce
wrong results. Full context in `FR_THD_AUDIT.md` and `CPU_AUDIT.md`.

1. **FR generates the hypothesis, the null decides.** FR rms is blind to phase and weights
   every band equally; the time-domain null is complex. For anything that can carry phase, use
   FR to find a candidate and the null to score it.
2. **Corrections that overlap in band AND in keying must be fit in one pass.** Fitting them
   independently over-corrects (P7/P8/step 1/step 3/step 5 — seven instances).
3. **An aggregate metric is a screen, not a verdict.** Decompose it along the axis you mean to
   fix before believing an improvement (P4/P6/P10 step 3 — five instances in the marginalisation
   family).
4. **Check your instrument before reading a difference off it.** A floor-limited aggregate reads
   as "no effect"; a "linear" instrument can be nonlinear at the far end of its axis (P7/P10);
   three of `OSFidelity`'s four sections have been caught above the clip knee. The observable
   must respond to the axis you are asking about.
5. **Guard both ends of a curve before reading anything off it.** A plateau needs a flat top,
   a tail needs unit slope (P10 step 2 — nine items had read these segments without either
   check).
6. **Count observables against unknowns before naming a defect.** P10 step 2 proved a
   suspected ceiling error was algebraically degenerate with a per-capture level offset.
7. **`PerfBenchmark` is load-sensitive.** Never measure right after a build; protocol in
   `CPU_AUDIT.md` §0.
8. **"Multiplied by zero" is not "dead"** when the branch maintains state another mode reads.
   Test mid-stream mode changes, not just steady-state renders per mode.
9. **An early-out's saving is a property of the signal, not of the code it skips.**
10. **An output-bounded check cannot see a blowup upstream of a clipper.** Assert the node,
    not the output.

---

## References

- CCRMA paper: https://ccrma.stanford.edu/~kaichieh/KingOfTone.pdf
- Theseus kit documentation: https://aionfx.com/app/files/docs/theseus_kit_documentation.pdf
- Schematics: `analysis/KoT_schematic_matsumin` (matsumin Ver2), `analysis/KoT_schematic_Theseus.png`
