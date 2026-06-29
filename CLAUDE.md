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

**CMake minimum:** 3.15 | **C++ standard:** 17 | **macOS target:** 10.13+ | **Version:** 1.0.0

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

---

## Repository State

**Engine + UI complete and validated; matched to NAM captures of a real King of Tone.** All DSP
stages, `MonarchChannel`, `processBlock`, and oversampling are done (build.md Validation Gates,
all PASS, auval PASS). The UI is complete: peripheral shared-look shell (side panels, OS strip,
resizable window) + the purple/gold `PedalFace`. Factory presets (5) ship via the host's native
preset browser. Supply-voltage mod (9/12/18V) and rail-saturation ADAA are in. Latest release
engineering: CI/CD (`.github/workflows/`), cross-platform VST3, and per-platform installers
(`installer/`) — see README.

**Calibration result (Step 11, real-pedal A/B):** the plugin nulls against 44 NAM captures (drive
G2–G10, tone T2–T8, Clean/OD/Dist) at **−7.6 to −21.7 dB, median −14.7**, down to ~−22 dB through
mid gain (G4–G6). Best per-mode null at the labelled mid-gain settings: Clean/Boost −20.7, OD
−18.5, Dist −17.6 dB. Excellent to mid gain; shallower only at very high drive (G8–G10) — an
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
- **Capture-match tilt shelf** (`TiltShelf`, an artificial fixed high-shelf): retired
  (`kEnabled=false`) once the corrected Stage-1 Z_lower topology reproduced the EQ tilt
  circuit-accurately. Code kept for A/B only. (Superseded by the drive-dependent two-shelf
  correction below — a *fixed* shelf cannot fix a tilt that reverses sign with drive.)
- **Literal 3-terminal DRIVE wiper-tap as the cause of the low-drive EQ collapse** (2026-06-29):
  re-derived the full wiper-tap transfer function (pin1→R2→NodeF, wiper=NodeG, pin3→R6→C5→Stage2)
  numerically — it has the *same* drive-dependence of the Stage-1 tilt as the 2-terminal model
  (the pot's dual action moves Stage 2's flat *level*, not Stage 1's *tilt*). So the real pedal's
  drive-INDEPENDENT clean EQ is not explainable by the linear topology; corrected empirically (below).

### Drive-dependent two-shelf capture-match correction (`MonarchChannel`, 2026-06-29)
Best-fit-gain-aligned EQ error (plugin vs captures, 40 Hz–16 kHz, every gain/tone) is a clean,
tone-independent **tilt that reverses with drive**: treble-short at low drive, bass-short/treble-hot
at high drive, crossing near G4. Corrected with two drive-scaled first-order shelves on Stage 1's
output (pre-clip): a **treble high-shelf** fading OUT with drive (restores the Stage-1 HF shelf
`Av=1+Z_upper/Z_lower` lets collapse at low drive — the "engaging it is dark" complaint) and a
**bass low-shelf** fading IN with drive (counters the bass-bloom-under-drive). Also *improves*
OD/Dist nulls at mid/high drive (G5 OD −18.4→−23.7, G5 Dist −14.9→−19.1).

### Linear stages run oversampled — top-octave warp fix (2026-06-29)
The remaining top-octave deficit (16 kHz ~−3.8 dB at every setting) was first wrongly blamed on NAM
capture aliasing — but NAM captures null to ~−50 dB and ARE accurate up there. It's **bilinear-
transform frequency warping** of the base-rate linear WDF solve (16 kHz deficit collapses −2.4 dB
@48k → −0.2 @96k → ~0 @192k when the linear stages run faster). Fixed by running the **whole channel
oversampled** (not just the clip span): both `prepareLinear` and `prepareClip` re-prep at the OS
rate. **Render/2x+ paths now match 50 Hz–16 kHz within ~1.2 dB at all gain/tone** (worst ~2.3 dB at
the tone-down top-octave corner). At **1x** the linear rate == session rate so the warp remains; a
rate-scaled warp high-shelf (`warp*`, `×(48k/rate)^4`) roughly fixes 8–12 kHz but 16 kHz+ stays
deficient (a first-order shelf can't match the near-Nyquist cliff) — 1x is the low-CPU/approximate-
top mode, use 2x+ for full fidelity. CPU cost: the linear WDF now runs at the OS rate too (relevant
to the v1.1 perf pass).

### Accepted residuals (un-modeled second-order device physics, per user pref for circuit accuracy)
- OD compresses ~3–4 dB lighter than the real pedal at hot input (Distortion compression good, Δ~2 dB).
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
| Stage 1 feedback floor | **Yellow R2∥R3 ≈ 990 Ω** (stock) / **Red R2 = 100k** (Hi-Gain). `hiGain` ctor flag |
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
| Op-amp rails | ±3.3 V soft knee @ 9V (Boost-only clip; tone-safe — diodes clip first in OD/Dist). Scaled by supply-voltage mod |
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
- `tools/PedalRender` renders a WAV through the real processor (Yellow-only) for A/B:
  `PedalRender in.wav out.wav drive tone vol pres clip`.

## References

- CCRMA paper: https://ccrma.stanford.edu/~kaichieh/KingOfTone.pdf
- Theseus kit documentation: https://aionfx.com/app/files/docs/theseus_kit_documentation.pdf
- Schematics: `analysis/KoT_schematic_matsumin` (matsumin Ver2), `analysis/KoT_schematic_Theseus.png`
