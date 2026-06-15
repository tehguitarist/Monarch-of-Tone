# Monarch of Tone — Project Memory

Monarch of Tone is a circuit-level emulation of the Analog Man King of Tone overdrive pedal,
built as an AU/VST3 plugin using JUCE 8+ and chowdsp_wdf Wave Digital Filter modelling.
**Author/Company:** Leigh Pierce

The King of Tone is a **dual-channel** Bluesbreaker-derived overdrive. Both channels are
modelled — 1-to-1 digital clone. Channels run in series (A → B), independently bypassable.

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

> **CURRENT: Step 4 — Stage-by-stage DSP**

The `.claude/` rules and agents are complete. The Step 2 JUCE CMake scaffold and Step 3
chowdsp_wdf smoke test are both in place and verified.

---

## Build Sequence — Do Not Skip Ahead

1. ✅ **Schematic analysis** — complete. All circuit values verified, MA856 validated, Hi Gain mod characterised.
2. ✅ **JUCE CMake scaffold** — complete. CMakeLists.txt, PluginProcessor, PluginEditor, full
   APVTS parameter layout, MonarchChannel stub, and TaperUtils all in place. AU validated
   with `auval` and all 18 parameters confirmed present with correct defaults.
3. ✅ **chowdsp_wdf smoke test** — complete. `tests/SmokeTest_RC.cpp` implements a 1kHz RC
   lowpass via `chowdsp::wdft` (double precision); measured -3.018 dB at the theoretical
   -3dB corner, confirmed PASS.
4. **Stage-by-stage DSP** ← CURRENT STEP — implement and validate each before moving on:
   - `Stage1` (IC_A, non-inverting) — combined input network (C3/R4/R5) + feedback network
     (Z_lower = (R7+C5)∥(R8+C6), Z_upper = C4∥(R6+DRIVE)); verify Av(s) shape and gain peak
     near ~4194 Hz at mid-DRIVE (re-measure exact value from the implemented model — see
     circuit.md Section 6)
   - `Stage1` Hi Gain (SW-3) — +4 dB gain range shift (Z_lower Branch2: R8_eff≈12.17k vs R8=27k)
   - `Stage2` (IC_B, inverting) — DC gain –22, HPF 159 Hz (C7=100nF, R9=10k)
   - `SW1SoftClip` (MA856×4 ∥ R10) — symmetric clipping, Vf ~0.82V onset
   - `SW2HardClip` (1S1588×2 shunt via R11) — symmetric clipping, Vf ~0.584V onset
   - **Run `dsp-validator` after each stage. Do not proceed on FAIL.**
5. **All 8 clipping modes** per channel — Boost/OD/Dist/Both × Standard/HiGain
6. **Tone stage** — passive RC treble-cut + Presence network
   - ⚠️ **Run `schematic-checker` first** — exact wiring of C8/C9/R13/Trim needs
     node-level verification from `king_of_tone_schematic.png` before coding
7. **Oversampling + ADAA** on both clipping stages — verify aliasing reduction
8. **Dual-channel integration** — channels A→B in series, independent bypass and Hi Gain
9. **UI implementation** — both channel panels, Hi Gain toggles, oversampling controls
10. **Final sweep** — all controls full range, no instability, clicks, or NaN output

---

## Key Circuit Facts

| Fact | Value |
|------|-------|
| Op-amp | JRC4580D per channel (matsumin schematic label JRC4558D is wrong) |
| Stage 1 (IC_A) | Non-inverting — no `PolarityInverterT` |
| Stage 2 (IC_B) | **Inverting** — `PolarityInverterT` required |
| Stage 2 DC gain | –22 (R10=220k feedback / R9=10k input) |
| Stage 2 HPF | 159 Hz (C7=100nF, R9=10k) |
| Soft-clip diodes SW-1 | MA856 ×4; two `DiodePairT` in ∥ with R10; Is=7.74e-13, n=1.512 |
| Hard-clip diodes SW-2 | 1S1588 ×2; one `DiodePairT` shunt via R11=6.8k; Is=2.52e-9, n=1.752 |
| Diode topology | **Symmetric pairs** — `DiodePairT` only, never `DiodeT` |
| Hi Gain SW-3 | R29=22k ∥ R8=27k → R8_eff≈12.17k in Z_lower Branch2; Stage 1 +4 dB gain shift |
| Stage 1 feedback | Z_lower=(R7+C5)∥(R8+C6); Z_upper=C4∥(R6+DRIVE 0-100k); Av(s)=1+Zupper/Zlower |
| DRIVE taper | 100kB **linear**; 2-terminal rheostat inside Stage 1 Z_upper only |
| TONE taper | 25kB **linear** |
| VOL taper | 100kA **audio** (`pow(10, 2x-2)`) |
| Presence taper | 50kB **linear**; default fully CCW |
| Tone stage | Passive RC only — no diodes |
| Channel routing | A → B in series; independently bypassable |
| Default mode | Overdrive (SW-1 ON, SW-2 OFF, SW-3 OFF) |
| Gain peak | ~4194 Hz at mid-DRIVE (re-measure from corrected Stage 1 model) |
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
