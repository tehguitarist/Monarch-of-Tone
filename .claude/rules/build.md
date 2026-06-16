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
│   │   ├── HiGainToggle.h / .cpp    ← per-channel Hi Gain switch
│   │   ├── ChannelPanel.h / .cpp    ← one channel's controls (instantiated twice)
│   │   ├── VUMeter.h / .cpp
│   │   └── LEDIndicator.h / .cpp
│   └── utils/
│       └── TaperUtils.h             ← audio taper for VOL; linear for DRIVE/TONE/Trim
├── libs/
│   ├── JUCE/
│   └── chowdsp_wdf/
└── tests/
    ├── SmokeTest_RC.cpp
    ├── Stage1_FreqResponse.cpp       ← combined input network + feedback network; gain
    │                                    peak near ~4194 Hz at mid-DRIVE (CCRMA), measured
    ├── Stage1_HiGain.cpp             ← verify +4 dB gain shift in Hi Gain mode
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
- Step 4a: Stage 1 (combined input network + feedback network) frequency response — verify
  Av(s) shape (DC gain ≈1, gain peak in the few-kHz region per CCRMA Fig. 6 at mid-DRIVE;
  measure and record the actual peak frequency/gain from the implemented model)
- Step 4b: Stage 1 Hi Gain — verify gain increase (~+4 dB target) vs standard mode
- Step 4c: Stage 2 — DC gain = ×22 inverting (R10/R9 = 220k/10k); HPF corner **159 Hz** (C7=100nF, R9=10k)
- Step 5a: SW-1 soft-clip — symmetric sine clipping; onset ≈1.64V (2×Vf_MA856, via the
  back-to-back series-string diode network in series with R11, ∥ R10)
- Step 5b: SW-2 hard-clip — symmetric sine clipping; onset ~0.584V (1S1588 Vf, true
  antiparallel pair shunting node_HC via always-present R12); harder knee than SW-1
- Step 6: All 8 mode combinations verified per channel (Boost/OD/Dist/Both × standard/HiGain).
  **Boost mode must clip on the op-amp rails (≈±3.3V, soft knee) — not stay infinitely clean.**
  Diode modes must clip at the diode thresholds (≈±1.64V / ≈±0.584V), proving the rail
  saturation never engages there (tone-safe). See dsp.md "Op-amp Rail Saturation".
- Step 7: Both channels in series — verify gain stacking, independent bypass, clipping interactions
- Step 8: Oversampling — confirm 4x live / 8x render split; bypassed channel skips oversampler
- Step 9: Full control sweep both channels, no instability, clicks, or NaN
