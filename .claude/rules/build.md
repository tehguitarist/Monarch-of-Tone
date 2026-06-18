# Build Rules

## CMake

- CMake build system only — no Projucer
- C++17 minimum standard
- JUCE 8+ via CMake submodule or FetchContent
- `chowdsp_wdf` as CMake submodule (header-only)
- Targets: AU (primary), VST3 (secondary)
- Author: Leigh Pierce | Company: Leigh Pierce

## Project Structure

```
monarch-pedal/
├── CMakeLists.txt
├── CLAUDE.md
├── .claude/
│   ├── agents/
│   │   ├── dsp-validator.md
│   │   └── schematic-checker.md
│   └── rules/
│       ├── circuit.md
│       ├── dsp.md
│       ├── architecture.md
│       ├── ui.md
│       └── build.md
├── src/
│   ├── PluginProcessor.h / .cpp
│   ├── PluginEditor.h / .cpp
│   ├── dsp/
│   │   ├── MonarchChannel.h         ← top-level single-channel DSP (instantiated twice)
│   │   ├── Stage1.h                 ← IC_A non-inverting amp incl. input network (C3/R4/R5)
│   │   │                              and feedback R-type at pin 2 (linear WDF)
│   │   ├── Stage2.h                 ← IC_B inverting amp; R-type at pin 2 (linear WDF)
│   │   ├── SW1SoftClip.h            ← MA856 back-to-back pairs (1x DiodePairT, n_eff=2n)
│   │   │                              + R11, in parallel with R10 (nonlinear WDF)
│   │   ├── SW2HardClip.h            ← 1S1588×2 true antiparallel shunt via R12 (nonlinear WDF)
│   │   ├── ToneStage.h              ← TONE 3-terminal pot tap (R-type) + C8/R13/Trim/C9 (linear WDF)
│   │   └── MonarchDSP.h             ← top-level dual-channel wrapper
│   ├── ui/
│   │   ├── MonarchLookAndFeel.h / .cpp
│   │   ├── KnobComponent.h / .cpp
│   │   ├── ClippingModeSelector.h / .cpp
│   │   ├── ChannelPanel.h / .cpp    ← one channel's controls (Yellow & Red instances; no Hi Gain toggle)
│   │   ├── VUMeter.h / .cpp
│   │   └── LEDIndicator.h / .cpp
│   └── utils/
│       └── TaperUtils.h             ← audio taper for VOL; linear for DRIVE/TONE/Trim
├── libs/
│   ├── JUCE/
│   └── chowdsp_wdf/
└── tests/
    ├── SmokeTest_RC.cpp
    ├── Stage1_FreqResponse.cpp       ← input network + gain stage; validates peak GAIN
    │                                    +13.88 dB, DC shelf, DRIVE monotonicity (peak freq
    │                                    bilinear-warped — see dsp.md). PASS.
    ├── Stage1_HiGain.cpp             ← verify +4 dB gain shift of Red's fixed Hi-Gain Stage 1 vs stock
    ├── Stage2_Gain.cpp               ← DC gain = –22; HPF corner 159 Hz (C7=100nF)
    ├── SW1SoftClip_Sine.cpp          ← MA856 symmetric soft clip; Vf ~0.82V onset
    ├── SW2HardClip_Sine.cpp          ← 1S1588 symmetric hard clip; Vf ~0.584V onset
    └── FullChain_DualChannel.cpp     ← both channels in series; all modes
```

## Build Commands

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Monarch_AU
cmake --build build --target Monarch_VST3
cmake --build build
```

## Plugin Metadata

```cmake
juce_add_plugin(Monarch
    COMPANY_NAME "Leigh Pierce"
    PLUGIN_MANUFACTURER_CODE LeP1
    PLUGIN_CODE Mon1
    FORMATS AU VST3
    PRODUCT_NAME "Monarch of Tone"
)
```

## Submodule Setup

```bash
git submodule add https://github.com/juce-framework/JUCE libs/JUCE
git submodule add https://github.com/Chowdhury-DSP/chowdsp_wdf libs/chowdsp_wdf
git submodule add https://github.com/xtensor-stack/xsimd libs/xsimd  # optional, SIMD accel
```

If XSIMD included — add before chowdsp_wdf in CMakeLists.txt:
```cmake
add_subdirectory(libs/xsimd)
target_link_libraries(Monarch PRIVATE xsimd)
```
In DSP code: `#include <xsimd/xsimd.hpp>` BEFORE `#include <chowdsp_wdf/chowdsp_wdf.h>`

## Compiler / Standard

- `-std=c++17`
- `cmake_minimum_required(VERSION 3.15)`
- Target macOS 10.13+
- Enable `-Wall -Wextra`; treat warnings as errors in CI

## Code Style

`.clang-format`:
```yaml
BasedOnStyle: LLVM
IndentWidth: 4
ColumnLimit: 120
BreakBeforeBraces: Attach
AllowShortFunctionsOnASingleLine: Inline
SortIncludes: false
```

`.clang-tidy`:
```yaml
Checks: "clang-diagnostic-*,clang-analyzer-*,modernize-*,readability-*,-readability-magic-numbers"
WarningsAsErrors: ""
```

## Validation Gates

- Step 2: AU and VST3 scan and load in a DAW
- Step 3: RC lowpass smoke test — correct -3dB point
- Step 4a: Stage 1 frequency response — ✅ PASS (2026-06-17). DC-servo shelf ≈ unity
  (−0.08 dB), peak +13.93 dB @ 3780 Hz (96k; analog 3803 Hz, −23 Hz bilinear warp), DRIVE
  +4.45→+18.22 dB monotonic. Accurate at base rate (−74 Hz @ 48k) — no oversampling/prewarp
  needed for the linear stages. An earlier ~−880 Hz error was an output-reconstruction bug
  (fixed: reconstruct V(NodeG) from passive ports, not the source port — see dsp.md).
- Step 4b: Stage 1 Hi Gain (fixed, Red only) — ✅ PASS (2026-06-18, dsp-validator). Topology
  resolved (Theseus page-28: SW1B switches R3=1k ∥ R2=100k in the Stage-1 feedback floor →
  raises Z_upper floor). Implemented as a single floor-resistance change, `HiGain_floor=39k`
  (tuned on the matsumin 10k base). `tests/Stage1_HiGain.cpp`: hotter everywhere (+6.6→+1.7 dB
  across DRIVE), monotonic, Red@9:00=13.79 dB ≈ Yellow@noon=13.90 dB (−0.12 dB, "9-o'clock acts
  like noon"). Stock Stage 1 unchanged by the refactor. Not a runtime toggle.
- Step 4c: Stage 2 — ✅ PASS (2026-06-17). Inverting passband gain 21.90× (−22 target,
  R10/R9 = 220k/10k); HPF corner **159 Hz** exactly (C7=100nF, R9=10k); signed gain −21
  (inverting). Inversion via op-amp VCVS terminals (no PolarityInverterT); output off passive
  R10 port. `tests/Stage2_Gain.cpp`, dsp-validator PASS.
- Step 5a: SW-1 soft-clip — ✅ PASS (2026-06-17). Small-signal −21.5 (inverting), PERFECTLY
  symmetric clipping, soft knee with output ≈1.63V @ 0.5V in rising to 2.67V @ 2V in (soft,
  not hard-clamped). The ~1.6V threshold (vs 0.82V single-diode) confirms n_eff≈3.024.
  Current-source/diode-root formulation. `tests/SW1SoftClip_Sine.cpp`, dsp-validator PASS.
- Step 5b: SW-2 hard-clip — ✅ PASS (2026-06-17). Gain ≈+1 (shunt, non-inverting), perfectly
  symmetric, HARD clamp ±0.55V @ 1V in rising only to 0.66V @ 10V (diode-log; ~1.6 dB out per
  20 dB in). 1S1588 true antiparallel via always-present R12=1k. `tests/SW2HardClip_Sine.cpp`,
  dsp-validator PASS.
- Step 6 (Tone stage): ✅ PASS (2026-06-18, dsp-validator). Passive TONE/Presence network
  (circuit.md §11): TONE 3-terminal pot tap modelled as a 3-port WDF parallel adaptor at the
  wiper (R_a series from the node_HC source; R_b+C8 to BIAS; R13 to node_T_out), Presence
  Trim+C9 and the VOL pot body (100k) loading node_T_out. `tests/ToneStage_FreqResponse.cpp`:
  treble-cut control (TONE↑ brightens, 5 kHz −27.7→−7.6 dB across the sweep), Presence reduces
  the hi-cut (5 kHz −18.7→−8.7 dB), passband −2.1 dB, no NaN; DC divider matches analytic to
  0.01 dB. **Contract for VolumePot/Step 7:** node_T_out already carries the VOL-body load — the
  VolumePot stage models only the wiper audio-taper tap + C11/R14; do NOT re-load node_T_out.
- Step 6 (per-channel modes): 4 clipping modes per channel (Boost/OD/Dist/Both) — Yellow on its stock Stage 1,
  Red on its fixed Hi-Gain Stage 1. (Hi Gain is no longer a runtime axis, so there is no
  8-combination matrix — 4 modes × 2 fixed-voicing channels.)
  **Boost mode must clip on the op-amp rails (≈±3.3V, soft knee) — not stay infinitely clean.**
  Diode modes must clip at the diode thresholds (≈±1.64V / ≈±0.584V), proving the rail
  saturation never engages there (tone-safe). See dsp.md "Op-amp Rail Saturation".
- Step 7: Yellow → Red in series — verify gain stacking, independent bypass, clipping interactions
- Step 8: Oversampling — confirm 4x live / 8x render split; bypassed channel skips oversampler
- Step 9: Full control sweep both channels, no instability, clicks, or NaN
