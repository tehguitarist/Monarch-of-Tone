# Build Rules

## CMake

- CMake only (no Projucer). C++17 minimum, `cmake_minimum_required(VERSION 3.15)`, macOS 10.13+.
- JUCE 8+ and `chowdsp_wdf` (header-only) as submodules; xsimd optional (SIMD).
- Targets: AU (primary, macOS only), VST3 (macOS/Windows/Linux), Standalone.
- `-Wall -Wextra`, warnings-as-errors in CI. Vendored deps (chowdsp_wdf, xsimd) marked SYSTEM in
  CMakeLists so their header warnings don't break `-Werror`. Our build is warning-clean.

```cmake
juce_add_plugin(Monarch
    COMPANY_NAME "Leigh Pierce"
    PLUGIN_MANUFACTURER_CODE LeP1
    PLUGIN_CODE Mon1
    FORMATS AU VST3
    PRODUCT_NAME "Monarch of Tone")
```

If xsimd is used, `add_subdirectory(libs/xsimd)` before chowdsp_wdf and
`target_link_libraries(Monarch PRIVATE xsimd)`; `#include <xsimd/xsimd.hpp>` before
`#include <chowdsp_wdf/chowdsp_wdf.h>`.

## Build Commands

```bash
git submodule update --init --recursive
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target Monarch_AU         # or Monarch_VST3 / Monarch_Standalone / (all)
```

## Project Structure

```
src/
  PluginProcessor.{h,cpp}   AudioProcessor, APVTS layout, processBlock, presets, TiltShelf (off)
  PluginEditor.{h,cpp}      Peripheral shell (trim/VU/oversampling strip), resizable window
  Presets.h                 5 factory presets (native programs API)
  dsp/  Stage1.h Stage2.h SW1SoftClip.h SW2HardClip.h ToneStage.h VolumePot.h
        MonarchChannel.h     full single-channel chain (instantiated twice)
  ui/   MonarchLookAndFeel.{h,cpp} PedalFace.{h,cpp} VUMeter.h LEDIndicator.h ClipSwitch.h
        VoltageSelector.h Assets.h
  utils/TaperUtils.h
tests/   per-stage DSP validation programs (see Validation Gates)
tools/   PedalRender, ControlSweep, UISnapshot, r_solver_sympy.py (R-type matrix solver), netlists/
analysis/  NAM captures (local-only), test signal, Python validation suite (see README)
libs/    JUCE, chowdsp_wdf, xsimd (submodules)
```

## Code Style

`.clang-format`: LLVM base, IndentWidth 4, ColumnLimit 120, BreakBeforeBraces Attach,
AllowShortFunctionsOnASingleLine Inline, SortIncludes false.
`.clang-tidy`: `clang-diagnostic-*,clang-analyzer-*,modernize-*,readability-*,-readability-magic-numbers`.

## Validation Gates

The DSP was built and validated stage-by-stage (run the `dsp-validator` agent after any DSP change;
don't proceed on FAIL). All gates currently **PASS** (auval PASS). Each has a dedicated test in
`tests/`:

| Stage | Test | Key validated result |
|-------|------|-----------------------|
| RC smoke | `SmokeTest_RC` | −3 dB at the theoretical corner |
| Stage 1 | `Stage1_FreqResponse` | peak gain +12.85 dB, DC shelf ≈unity, DRIVE monotonic; peak freq accurate at base rate |
| Stage 1 Hi-Gain | `Stage1_HiGain` | Red hotter everywhere, monotonic, Red@d ≈ Yellow@(d+1/6) |
| Stage 2 | `Stage2_Gain` | inverting −22 passband, HPF corner 159 Hz |
| SW-1 soft | `SW1SoftClip_Sine` | symmetric soft clip, knee ≈1.64 V (confirms n_eff≈3.024) |
| SW-2 hard | `SW2HardClip_Sine` | symmetric hard clamp ≈±0.55 V |
| Tone | `ToneStage_FreqResponse` | treble-cut control, presence lifts hi-cut, DC divider exact |
| Volume | `VolumePot_Taper` | audio taper 0/−10/−20/−30/−40 dB exact |
| Full chain | `FullChain_DualChannel` | Boost>OD>Dist hierarchy, Boost rail-bounded, Red→Yellow series stable, no NaN |
| Oversampling | (auval + DSP regression) | clip-span only; voicing OS-independent |
| Final sweep | `tools/ControlSweep` | full range × all clip combos × 4 OS factors + bypass + render: 0 non-finite, bounded, stable |

**Calibration / null validation** (Step 11, real-pedal A/B): see CLAUDE.md. The plugin nulls
against 44 NAM captures at −6.4 to −22.3 dB (median −16.6, v1.3). Harness: `analysis/null_test.py`,
`run_validation.py` (writes `analysis/VALIDATION_REPORT.md`), `internal_checks.py`.
