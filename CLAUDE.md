# Monarch of Tone — Project Memory

Monarch of Tone is a circuit-level emulation of the Analog Man King of Tone overdrive pedal,
built as an AU/VST3 plugin using JUCE 8+ and chowdsp_wdf Wave Digital Filter modelling.
**Author/Company:** Leigh Pierce

The King of Tone is a **dual-channel** Bluesbreaker-derived overdrive. Both channels are
modelled — 1-to-1 digital clone. Channels run in series and are independently bypassable.
The two channels are named externally by their LED colour: **Yellow** (first) → **Red**
(second). The Theseus Hi-Gain mod is a **fixed** part of the **Red** channel only (not a
runtime toggle); Yellow is always stock.

---

## Rule Files (read before touching any code)

@.claude/rules/circuit.md      ← circuit topology, all component values, signal flow
@.claude/rules/dsp.md          ← WDF implementation rules, API reference, diode parameters
@.claude/rules/architecture.md ← plugin structure, APVTS parameters, threading model
@.claude/rules/ui.md           ← layout, controls, colour scheme
@.claude/rules/build.md        ← CMake setup, project structure, validation gates

**Agents:**
- `dsp-validator` — invoke after implementing each DSP stage. Do not proceed to the next
  stage without a PASS verdict.
- `schematic-checker` — invoke for any circuit topology question. Never guess a component
  value or connection from memory.

---

## Quick Reference

```bash
# First-time setup
git submodule add https://github.com/juce-framework/JUCE libs/JUCE          # JUCE 8+
git submodule add https://github.com/Chowdhury-DSP/chowdsp_wdf libs/chowdsp_wdf
git submodule add https://github.com/xtensor-stack/xsimd libs/xsimd         # optional SIMD

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build

# Individual targets
cmake --build build --target Monarch_AU     # AU (primary)
cmake --build build --target Monarch_VST3   # VST3

# Code quality
clang-tidy src/**/*.cpp
clang-format -i src/**/*.{cpp,h}
```

**CMake minimum:** 3.15 | **C++ standard:** 17 | **macOS target:** 10.13+

---

## Repository State

> **CURRENT: Step 11 — Real-pedal (NAM capture) A/B calibration in progress.** Engine + UI are
> complete & validated; we are now matching the model to NAM captures of a real King of Tone
> (single Yellow/stock channel) in `analysis/`. A schematic re-check (2026-06-20) found a
> **fundamental Stage-1 Z_lower topology error** — see circuit.md §6 and Step 11 below.

The full audio engine is done & validated (all stages, `MonarchChannel`, `processBlock`,
oversampling — Step 7/8). **The UI is now complete:**
- **Peripheral (shared-look):** `MonarchLookAndFeel` (palette + halo trim knob + main knurled
  knob + OS combo boxes + bypass footswitch + scaling button font), `VUMeter`, `LEDIndicator`,
  Input/Output side panels (trim halo + TRIM + VU), the oversampling strip (LIVE/RENDER combos
  bound to `oversampling_realtime`/`oversampling_render` + UI-size scale button), and a
  resizable/aspect-locked window (base 592×354, 0.5–2.5×) with per-session (APVTS `uiScale`) +
  cross-session (`ApplicationProperties` `defaultScale`) persistence.
- **Pedal face (`PedalFace`):** royal-purple body (`#6A0956`, lit gradient) with gold (`#C6A15B`)
  Papyrus lettering + gold compass rose (ref `pedal_picture.png`). Per channel: Volume/Drive/Tone
  black knurled knobs, a small Presence trim flanking the compass, a 3-way `ClipSwitch`
  (Boost/OD/Dist) at the edge (Red mirrored), an LED, and a bypass footswitch. "MONARCH OF TONE"
  logo (no ANALOG.MAN). Verified by headless render (`UISnapshot` → /tmp/monarch_ui.png) + auval.

**Remaining:** Step 10 final sweep (all controls full-range, no clicks/NaN); ADAA optional.
**Step 11 (CURRENT): real-pedal A/B calibration** — see the Build Sequence Step 11 below.

Build helpers: `Standalone` plugin format (run the UI without a DAW), the `UISnapshot` console
app (headless editor→PNG; no display needed — `./build/.../UISnapshot [scale]`), and
**`tools/PedalRender`** (offline WAV render through the real processor, Yellow-only; for A/B vs
the captures — `PedalRender in.wav out.wav drive tone vol pres clip`).

### Real-pedal calibration harness (`analysis/`)
- `analysis/Pedal_export/*.wav` — NAM captures of a real KOT (single stock channel) at labelled
  settings (e.g. "G6 T5 OD" = drive 60%, tone 50%, Overdrive). `analysis/test_signal_48k.wav` is
  the input (segments: 1 kHz cal tone, clean & driven log sweeps, 1 kHz level steps, freq tones —
  see `gen_test_signal.py`). `analysis/analyze.py` does the A/B (THD, H2/H3, compression, EQ).
- `analysis/theseus schematic.png` + `theseus_kit_documentation.pdf` (parts list p29, IC voltages
  p30) — authoritative for the real KOT topology/values. **Verified 2026-06-20:** tapers (Drive
  100kB lin, Tone 25kB lin, Vol 100kA audio), all component values, 9.15 V supply / ~4.5 V bias.

---

## Build Sequence — Do Not Skip Ahead

1. ✅ **Schematic analysis** — complete. All circuit values verified, MA856 validated, Hi Gain mod characterised.
2. ✅ **JUCE CMake scaffold** — complete. CMakeLists.txt, PluginProcessor, PluginEditor, full
   APVTS parameter layout, MonarchChannel stub, and TaperUtils all in place. AU validated
   with `auval`. (Parameter set later trimmed: the two `hi_gain_*` params were removed when
   Hi Gain became a fixed Red-channel mod — see Step 5 / circuit.md Section 6.)
3. ✅ **chowdsp_wdf smoke test** — complete. `tests/SmokeTest_RC.cpp` implements a 1kHz RC
   lowpass via `chowdsp::wdft` (double precision); measured -3.018 dB at the theoretical
   -3dB corner, confirmed PASS.
4. **Stage-by-stage DSP** ← CURRENT STEP — implement and validate each before moving on:
   - ✅ `Stage1` (IC_A, non-inverting) — **DONE & validated (dsp-validator PASS).** Input
     network (C3/R4/R5 high-pass sub-filter) + root R-type op-amp gain stage (Z_lower =
     (R7+C5)∥(R8+C6), Z_upper = C4∥(R6+DRIVE)). Scattering matrix from `tools/r_solver_sympy.py`
     (validated sympy port of R-Solver) with ideal-op-amp limit. `tests/Stage1_FreqResponse.cpp`:
     peak +12.85 dB @ 4120 Hz (96k), DC shelf −0.20 dB, DRIVE +0.67→+17.60 dB monotonic.
     Accurate at base rate — no oversampling/prewarp needed (an output-reconstruction bug, since
     fixed, had caused a ~−880 Hz error). **Yellow DRIVE floor = 1k** (Theseus stock R2∥R3, a
     nearly-clean min: +0.67 dB at drive 0); matsumin labels R6=10k but we use 1k to err cautious
     / keep Yellow transparent (decision 2026-06-19). Single constant. See `src/dsp/Stage1.h`.
   - ✅ `Stage1` Hi Gain — **DONE & validated (dsp-validator PASS, Step 4b).** Fixed mod on the
     **Red channel only** (decision 2026-06-17), not a runtime toggle. Topology RESOLVED via
     Theseus page-28 hi-res trace: SW1B switches R3(1k) in parallel with the Stage-1 feedback
     floor R2(100k) — i.e. the mod **raises the Z_upper floor resistor**, shifting the DRIVE
     range up ("9 o'clock acts like noon"). Implemented as a single floor-resistance change
     (`Stage1(bool hiGain)`, Red `HiGain_floor=39k` vs Yellow `R6_floor=1k` — the literal Theseus
     100k would be ~+13 dB, far hotter than the documented subtle mod). The R-type matrix
     recomputes live from port impedances, so no solver re-run. `tests/Stage1_HiGain.cpp`:
     hotter everywhere (+10.4→+2.3 dB over Yellow's clean min), monotonic, Red@9:00=13.79 dB ≈
     Yellow@noon=12.80 dB (+0.98 dB). See `src/dsp/Stage1.h`.
   - ✅ `Stage2` (IC_B, inverting) — **DONE & validated (dsp-validator PASS).** Root R-type
     (op-amp VCVS), input C7(100nF)+R9(10k), feedback R10(220k). `tests/Stage2_Gain.cpp`:
     passband 21.90× (−22 target), −3 dB corner 159 Hz exactly, signed gain −21 (inverting).
     Inversion via VCVS terminals (in+=BIAS, in−=pin6−), no PolarityInverterT; output read off
     passive R10 port. SW-1 diodes (R10 ∥ [R11+diodes]) added next. See `src/dsp/Stage2.h`.
   - ✅ `SW1SoftClip` — **DONE & validated (dsp-validator PASS).** ONE `DiodePairT`
     (n_eff=2×n_MA856≈3.024 via the `nDiodes` arg), R11(6.8k) series, R10 parallel.
     Implemented as current-source/diode-root (op-amp virtual ground → known i_in drives
     R10 ∥ [R11+diode], diode = nonlinear root). `tests/SW1SoftClip_Sine.cpp`: small-signal
     −21.5 (inverting), perfectly symmetric, soft knee ≈1.64V (confirms n_eff). See `src/dsp/SW1SoftClip.h`.
   - ✅ `SW2HardClip` — **DONE & validated (dsp-validator PASS).** 1S1588×2 true antiparallel
     (ONE `DiodePairT`, n=1.752), shunt at node_HC via always-present R12=1k. Current-source...
     no — series-R + shunt-diode-root. `tests/SW2HardClip_Sine.cpp`: gain ≈+1, symmetric, HARD
     clamp ±0.55V (rises only to 0.66V @ 10× drive = diode log). See `src/dsp/SW2HardClip.h`.
   - **Run `dsp-validator` after each stage. Do not proceed on FAIL.**
5. **3 clipping modes per channel** — Boost/OD/Dist (`clipping_mode_*`; "Both" dropped 2026-06-19
   for the 3-way hardware toggle). Hi Gain is not a runtime axis (Yellow fixed-standard, Red
   fixed-Hi-Gain), so each channel exposes just its 3 clipping modes (circuit.md Section 18).
6. ✅ **Tone stage** — **DONE & validated (dsp-validator PASS, Step 6).** Passive RC; TONE is
   a 3-terminal pot tap modelled as a 3-port WDF **parallel** adaptor at the wiper (no R-type
   matrix — no op-amp): R_a (series from node_HC source) / R_b+C8 (to BIAS, treble-cut) / R13
   (to node_T_out). node_T_out loaded by Presence (Trim+C9) and the **VOL pot body (100k)**.
   Output = V(node_T_out). `tests/ToneStage_FreqResponse.cpp`: treble-cut control (TONE 1→0
   darkens; 5 kHz −7.6→−27.7 dB), presence lifts hi-cut (5 kHz −18.7→−8.7 dB), passband
   −2.1 dB, DC divider matches to 0.01 dB. See `src/dsp/ToneStage.h`. **Contract:** VolumePot
   must NOT re-load node_T_out with the VOL body (already included here) — only the wiper
   audio-taper tap + C11/R14.
   - ✅ `VolumePot` — **DONE & validated.** VOL 100kA audio-taper wiper tap (scalar; VOL body
     load lives in ToneStage) + C11/R14 output HPF (0.16 Hz, WDF). `tests/VolumePot_Taper.cpp`:
     0/−10/−20/−30/−40 dB exact. See `src/dsp/VolumePot.h`.
   - ✅ `MonarchChannel` full chain — **DONE & validated (dsp-validator PASS, Step 7).** Wires
     Stage1 → Stage2/SW1 → op-amp rail-sat (±3.3 V) → SW2 → Tone → Volume with clipping-mode
     routing. `tests/FullChain_DualChannel.cpp`: all 4 modes both channels, clipping hierarchy
     Boost>OD>Dist>Both, Boost on rails, Red hotter, Yellow→Red series stable, no NaN.
7. ✅ **Oversampling** — **DONE & validated (dsp-validator PASS; auval PASS).** Wraps ONLY each
   channel's nonlinear clip span (`processClip`: Stage2/SW1 + rail-sat + SW2), so the factor
   changes anti-aliasing only — the linear voicing is OS-independent (linear stages prepared once
   at base rate, never re-prepared on factor change). 1x/2x/4x/8x via `oversampling_realtime` /
   `oversampling_render` (isNonRealtime → render); **IIR low-latency live, FIR max-quality
   render**; 2 oversamplers (Yellow, Red), each multi-channel; bypassed channels skip it; latency
   reported. Factor/quality rebuild is on the audio thread, gated to only-on-change (rare). ADAA
   is an optional later refinement.
8. ✅ **Dual-channel integration** — **DONE (audible; auval PASS).** `processBlock` wires
   APVTS → both channels, ±12 dB input/output trim (1 V/FS calibration, `circuitVoltsPerFS`),
   per-channel ~5 ms bypass crossfade, peak meters, and **dual-mono stereo** (a `ChannelStrip`
   {Yellow,Red} per audio channel). Params read once per block via cached APVTS atomic pointers;
   Yellow→Red in series. Oversampling not yet wrapped (Step 7).
9. ✅ **UI implementation** — **DONE.** Shared-look peripheral shell (side panels + OS strip +
   resizable window) and the unique purple/gold `PedalFace` (knobs, 3-way clip switches, presence
   trims, LEDs, footswitches, compass rose, logo). Yellow/Red, no Hi Gain toggle. auval PASS.
10. **Final sweep** — all controls full range, no instability, clicks, or NaN output
11. **Real-pedal (NAM capture) A/B calibration** ← CURRENT — match the model to NAM captures of a
    real KOT (single Yellow/stock channel) via `tools/PedalRender` + `analysis/analyze.py`:
    - ✅ **Signal calibration** `circuitVoltsPerFS = 0.66` (commit ba86c69) — the clean/Boost
      rail-clip onset matched to the capture (−18 dBFS 1 kHz); OD/Dist THD then matches. We trust
      the capture's calibration over the bench voltage measurement (revisit absolute level later).
    - ✅ **Capture-match tilt shelf + Yellow floor → 10k** (commit 684ec5b) — a fixed first-order
      high-shelf (−2.6 dB LF / +1.4 dB HF, pivot 1.4 kHz, unity at 1 kHz) correcting a gain-
      INVARIANT ~1 kHz-pivot EQ tilt; `TiltShelf` in PluginProcessor.h, gated out on full bypass.
      **ARTIFICIAL — NOW RETIRED** (`TiltShelf::kEnabled=false`) by the Stage-1 Z_lower fix below,
      which reproduces the tilt circuit-accurately. (Also the Yellow floor 10k → real ~990 Ω.)
      Code kept; flip kEnabled to A/B.
    - ✅ **Even-harmonic asymmetry** (commit ffa5c64, `MonarchChannel::injectEvenHarmonic`) — the
      KOT clips symmetrically by design (→ no even harmonics), and the topology STRUCTURALLY
      rejects the circuit-accurate bias-shift (the feedback soft-clip and hard shunt resist an
      internal DC offset; a diode mismatch only shifts clamp LEVELS → DC → blocked). So H2 is
      injected EMPIRICALLY at the clip output, sourced from a bounded soft-saturation of the
      pre-clip drive (squares up at high drive → washes out, matching the captures' non-monotonic
      H2-vs-gain), clip-depth-gated (clean stays symmetric), DC-free via a slow running mean.
      Matches OD/Dist H2 within ~3–4 dB across drive (G2/G6/G10) and level, 440 Hz–5 kHz. **Known
      gaps:** Boost's true (falling) trend is settled for moderate constant warmth; **<440 Hz H2
      is under-injected because the model under-drives low notes** (the Stage-1 deficit below).
    - ✅ **Stage-1 Z_lower topology fix (circuit-accurate)** — DONE. Rebuilt `Stage1.h` to the real
      Theseus Z_lower (`C4 series [R4 ∥ (R5+C3)]`) + real floors (Yellow R2∥R3 ≈ 990 Ω, Red R2 =
      100k) + C1 = 22n. NO R-type matrix — an ideal op-amp decouples Z_lower/Z_upper, so it solves
      two WDF one-ports (V-source → Z_lower → current → I-source → Z_upper → voltage), validated to
      ~0.1 dB of analytic. Clean-EQ vs captures: G4 ≈ 0.4 dB RMS, G2/G6 within ~1.5 dB (now drive-
      dependent, as the real pedal). **Tilt shelf RETIRED** (`TiltShelf::kEnabled=false`).
      circuitVoltsPerFS re-cal 0.66 → 0.87. All DSP tests + auval PASS.
    - ⏳ **Driven-path re-validation (NEXT)** — the Stage-1 rebuild changed drive levels into the
      clip span, so: (a) compression depth (real still compresses ~3 dB harder at hot levels —
      pre-existing, partly improved by the re-cal); (b) the even-harmonic shaper coeffs
      (MonarchChannel `asym*`) were tuned to the OLD Stage 1 and need re-tuning vs the captures;
      (c) the low-note (<440 Hz) THD/H2 — confirm whether the higher drive now reaches it.

---

## Key Circuit Facts

| Fact | Value |
|------|-------|
| Op-amp | JRC4580D per channel (matsumin schematic label JRC4558D is wrong) |
| Stage 1 (IC_A) | Non-inverting — no `PolarityInverterT` |
| Stage 2 (IC_B) | **Inverting** — `PolarityInverterT` required |
| Stage 2 DC gain | –22 (R10=220k feedback / R9=10k input) |
| Stage 2 HPF | 159 Hz (C7=100nF, R9=10k) |
| Soft-clip diodes SW-1 | MA856 ×4 as `[D4+D5]∥[D2+D3]` back-to-back series strings ≡ ONE `DiodePairT` with n_eff=2×1.512≈3.024, Is=7.74e-13; in series with R11=6.8k, branch ∥ R10=220k, gated by SW-1 |
| Hard-clip diodes SW-2 | 1S1588 ×2 true antiparallel pair; one `DiodePairT` shunt at node_HC, fed via R12=1k (always-present Stage 2 output R); Is=2.52e-9, n=1.752 |
| Diode topology | **Symmetric pairs** — `DiodePairT` only, never `DiodeT` |
| Hi Gain | **Fixed mod on the Red channel only** (not a runtime toggle; no `hi_gain_*` param). Yellow always stock. ✅ Implemented & validated (Step 4b): SW1B switches R3(1k) ∥ R2(100k) in the Stage-1 feedback floor → **raises Z_upper floor**, shifting DRIVE range up. Modelled as a single floor-resistance change, `HiGain_floor=39k` (tuned to "9-o'clock≈noon" on the matsumin 10k base; not the old R29∥R8 model). See circuit.md Section 6. |
| Stage 1 feedback | Z_lower=(R7+C5)∥(R8+C6); Z_upper=C4∥(R6+DRIVE 0-100k); Av(s)=1+Zupper/Zlower |
| DRIVE taper | 100kB **linear**; 2-terminal rheostat inside Stage 1 Z_upper only |
| TONE taper | 25kB **linear**; 3-terminal pot tap (R-type adaptor at wiper) — see circuit.md Section 11 |
| VOL taper | 100kA **audio** (`pow(10, 2x-2)`) |
| Presence taper | 50kB **linear**; default fully CCW; 2-terminal rheostat (like DRIVE) from node_T_out |
| Tone stage | Passive RC only — no diodes; 3-terminal TONE pot tap, not two parallel branches |
| Channel routing | Yellow → Red in series; independently bypassable |
| Channel names | Yellow (first, stock) → Red (second, fixed Hi-Gain), after the LED colours |
| Default mode | Overdrive (SW-1 ON, SW-2 OFF) per channel |
| Gain peak | **+12.85 dB @ 4120 Hz (96k, Yellow drive 0.5, floor 1k)**. Accurate at base rate — linear stages need no oversampling/prewarp (earlier large error was an output-recon bug, fixed; see dsp.md). |
| DRIVE floor | Yellow **1k** (Theseus stock R2∥R3, nearly-clean min +0.67 dB); Red **39k** (Hi-Gain, min +11 dB). matsumin labels R6=10k — we use 1k for a cautious/transparent Yellow min. |
| Oversampling live | 1x/2x/4x/8x; default **4x**; bypassed channels skip oversampler |
| Oversampling render | 1x/2x/4x/8x; default **8x**; auto via `isNonRealtime()` |

## Three Most Likely Implementation Mistakes

1. **`DiodeT` instead of `DiodePairT`** — both clipping stages use symmetric matched pairs.
2. **Audio taper on DRIVE or TONE** — both are linear (B-taper). Only VOL is audio taper.
3. **Missing `PolarityInverterT` on Stage 2** — it is inverting; omitting it produces wrong polarity.

## References

- CCRMA paper: https://ccrma.stanford.edu/~kaichieh/KingOfTone.pdf
- Theseus kit documentation: https://aionfx.com/app/files/docs/theseus_kit_documentation.pdf
- Primary schematic: `king_of_tone_schematic.png` (in project root)
