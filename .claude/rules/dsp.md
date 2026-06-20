# DSP Rules

## WDF Implementation

- Use `chowdsp_wdf` (header-only, C++17) for ALL circuit modelling
- Use the **compile-time API** (`chowdsp::wdft` namespace) — not the runtime `chowdsp::wdf` namespace
- **Use `double` precision for all WDF types** — `float` causes audible errors in NR iteration for diode models at audio frequencies
- All passive networks modelled as WDF port trees
- Nonlinear elements use explicit per-component datasheet parameters — never generic defaults
- Op-amp stages use ideal op-amp WDF model (JRC4580D) for the linear gain solve, **plus an
  explicit output rail saturation** (see "Op-amp Rail Saturation" below) — required for
  correct Boost-mode behaviour. In diode-clipping modes the diodes clip well before the
  rails, so the saturation never engages and tone is unchanged.
- R-type adaptors required for feedback topologies — derive scattering matrix from nodal equations
- **Never reconstruct the WDF tree at runtime for switch changes** — precomputed scattering matrices, switch via `setSMatrixData()`
- VREF (VB) treated as signal ground throughout — model bipolar
- Stage 1 (IC_A, non-inverting): linear WDF tree, no Newton-Raphson
- Stage 2 (IC_B, inverting): linear WDF tree for op-amp, R-type adaptor at feedback node
- Nonlinear stages (SW-1 soft-clip, SW-2 hard-clip): Newton-Raphson via chowdsp_wdf nonlinear solver

### prepareToPlay Requirements
Every `CapacitorT` must have `.prepare(sampleRate)` called in `prepareToPlay`. Missing this produces silence or wrong behaviour. Call on every cap in every WDF stage in both channels. Also reset both oversamplers.

### Stage 2 Polarity Inversion
Stage 2 (IC_B) is an **inverting** amplifier — its output MUST be inverted (passband gain
**−22**, validated by a negative signed gain in `tests/Stage2_Gain.cpp`).

**How the inversion is realized (VCVS-root-R-type approach, as implemented):** the op-amp is
a VCVS inside the root R-type adaptor, and its terminal assignment carries the inversion —
`in+ = BIAS, in− = pin6(−)`, so V(out) = −A·V(pin6−). No separate `PolarityInverterT` is used
(the same way Stage 1's non-inverting VCVS uses none). The output is read off the **passive**
R10 feedback port (`+voltage(r10)`, since pin6− is virtual ground), which already carries the
inverted sign. (A non-inverting VCVS assignment would be positive feedback → unstable/NaN, so
a passing −22 gain proves the inversion is physical, not a forced sign flip.)

> The older guidance "a `PolarityInverterT` is required between the R-type and an ideal voltage
> source" assumed a different op-amp WDF structure. With the VCVS-in-root-R-type structure used
> here, inversion lives in the VCVS netlist. **Either is valid; the gate is the measured −22
> inverting gain, not the presence of a specific element.** Stage 1 (non-inverting) needs no
> inversion.

## chowdsp_wdf API Reference (key types)

All in `chowdsp::wdft` namespace. Include: `#include <chowdsp_wdf/chowdsp_wdf.h>`

### Passive Elements
```cpp
wdft::ResistorT<double> r { 1.0e3 };
wdft::CapacitorT<double> c { 1.0e-6 };          // call c.prepare(sampleRate)
wdft::ResistorCapacitorSeriesT<double> rc { R, C };
wdft::ResistorCapacitorParallelT<double> rc { R, C };
```

### Adaptors
```cpp
wdft::WDFSeriesT<double, decltype(a), decltype(b)> s { a, b };
wdft::WDFParallelT<double, decltype(a), decltype(b)> p { a, b };
wdft::PolarityInverterT<double, decltype(s)> inv { s };  // required for Stage 2 (inverting)
```

### R-type Adaptor
```cpp
wdft::RtypeAdaptor<double, upPortIndex, ImpedanceCalculator, Port0Type, Port1Type, ...> rtype { port0, port1, ... };
// Switch clipping topologies without tree reconstruction:
rtype.setSMatrixData({{ ... }});
```

### Nonlinear Elements — Symmetric Diode Pairs
Both clipping stages use **symmetric** antiparallel diode pairs. Use `DiodePairT` for both.

```cpp
// MA856 soft-clip parameters — validated from Panasonic datasheet (Vf=0.82V@1mA, Vf≤1.0V@100mA):
constexpr double Is_MA856 = 7.74e-13;  // derived; NOT a placeholder
constexpr double n_MA856  = 1.512;     // derived; lower than 1N4148's 1.752
constexpr double Vt       = 25.85e-3;
// Rs_MA856 = 0.85 Ω (datasheet rf) — model as ResistorT in series if desired; negligible at guitar levels

// 1S1588 hard-clip parameters (= 1N914 = 1N4148 electrically):
constexpr double Is_1S1588 = 2.52e-9;
constexpr double n_1S1588  = 1.752;

// Antiparallel diode pair (identical diodes, symmetric clipping):
wdft::DiodePairT<double, decltype(next), wdft::DiodeQuality::Best> dp { next, Is, Vt, n };
// Use DiodeQuality::Best — Wright Omega approximation, correct for audio
```

**Do NOT use separate `DiodeT` instances for each polarity.** Both stages use symmetric `DiodePairT` pairs.

### SW-1 Soft-Clip Configuration (CORRECTED 2026-06-16; IMPLEMENTED 2026-06-17)
The SW-1 feedback branch is `[D4 series D5] ∥ [D2 series D3]` (back-to-back, opposite
polarity MA856 pairs), in series with R11 (6.8k), this combination in parallel with R10
(220k). Two identical diodes in series are electrically equivalent to ONE diode with the
same `Is` and `n` doubled (`n_eff = 2×n_MA856 ≈ 3.024`). Model the entire diode network as
**a single** `DiodePairT` with `n_eff`. Do NOT instantiate two `DiodePairT`s — that models
two independent antiparallel pairs, which is not the actual topology.

> **IMPLEMENTED via the current-source / diode-root formulation** (`src/dsp/SW1SoftClip.h`),
> NOT a linear R-type-with-diode-port. The ideal op-amp pins pin6(−) at virtual ground, so
> the input forces a *known* current `i_in = Vin/Z_in` (Z_in = R9 + 1/sC7) into the feedback,
> independent of the nonlinear feedback. That current drives `R10 ∥ [R11 + diode]` with the
> `DiodePairT` as the nonlinear ROOT — far simpler than embedding a nonlinearity in an R-type
> scatter, and equivalent. SW-1 OFF reduces to V(pin7) = −i_in·R10 = −22·Vin (stock Stage 2).
> Validated: symmetric soft clip, onset ≈ 1.64 V (the ~1.6 V threshold vs 0.82 V for a single
> diode confirms `n_eff≈3.024`). **`nDiodes` (4th DiodePairT arg) scales Vt — pass `n_eff` there.**

```cpp
constexpr double n_eff_MA856 = 2.0 * n_MA856; // ≈ 3.024 (passed as DiodePairT's nDiodes arg)

// i_in‖R10 as a current source; R11 in series; DiodePair as the nonlinear root:
wdft::ResistiveCurrentSourceT<double> iSrc { 220.0e3 };  // i_in ‖ R10
wdft::ResistorT<double> r11 { 6.8e3 };
wdft::WDFSeriesT<double, decltype(r11), decltype(iSrc)> fbSeries { r11, iSrc };
wdft::DiodePairT<double, decltype(fbSeries), wdft::DiodeQuality::Best> dp { fbSeries, Is_MA856, Vt, n_eff_MA856 };
// per sample: iSrc.setCurrent(i_in); dp.incident(fbSeries.reflected()); fbSeries.incident(dp.reflected());
//             output V(pin7) = voltage(iSrc);   // passive read
```

> Mode switch (SW-1 ON/OFF, later step): with this diode-root structure the switch is
> structural (include / bypass the diode-root path), not a `setSMatrixData()` swap. The
> `setSMatrixData` plan in architecture.md applies to the linear R-type stages, not the
> nonlinear clip — reconcile when wiring the 8 modes.

### Voltage Readout
```cpp
double output = wdft::voltage<double>(element);
```

### Deferred Impedance Propagation
```cpp
{
    wdft::ScopedDeferImpedancePropagation deferrer { port0, port1 };
    r_drive.setResistanceValue(newDriveR);
} // propagation fires once
```

## Dual-Channel Implementation

Both channels are identical circuits. Implement a single `MonarchChannel` class and instantiate it twice:

```cpp
class MonarchChannel {
    explicit MonarchChannel (bool hiGain);  // hiGain bakes the fixed Stage-1 Hi-Gain voicing
    Stage1          stage1;       // IC_A non-inverting; includes input network (C3/R4/R5)
    Stage2          stage2;       // IC_B inverting + clipping
    ToneStage       tone;         // passive RC
    VolumePot       volume;
    juce::dsp::Oversampling<double> oversampler;
};

MonarchChannel channelYellow { false }; // first channel, stock Stage 1
MonarchChannel channelRed    { true };  // second channel, fixed Hi-Gain Stage 1
```

Channel routing:
```
inputSample → channelYellow.process() → yellow_output → channelRed.process() → output
```

Each channel has independent APVTS parameters (e.g., `drive_yellow`, `drive_red`,
`tone_yellow`, `tone_red` etc.). There is no `hi_gain_*` parameter — Hi Gain is fixed on Red.

## Oversampling

- Apply to SW-1 (soft-clip feedback diodes) and SW-2 (hard-clip shunt diodes) in both channels only — linear stages never oversampled
- Use `juce::dsp::Oversampling` — one oversampler per channel (wraps both clipping stages together)
- Two independent oversampling settings exposed in APVTS:
  - `oversampling_realtime` — applied during live playback; default **4x**
  - `oversampling_render` — applied when DAW is rendering/bouncing (detected via `AudioProcessor::isNonRealtime()`); default **8x**
  - Both are `AudioParameterChoice`: "1x" / "2x" / "4x" / "8x"
- The audio thread checks `isNonRealtime()` each processBlock and selects the appropriate factor. If the active factor differs from the currently initialised oversampler state, reinitialise (same `pendingOversamplingFactor` atomic mechanism, checked at block start).
- **Bypassed channels skip the oversampler entirely** — no upsample/downsample, raw pass-through. Do not run the oversampler on a bypassed channel even if the factor is >1x. This halves the cost when only one channel is in use.
- 4x + first-order ADAA is sufficient for inaudible aliasing on this circuit (gentle feedback clipping, not hard rail clipping). 8x render provides additional headroom for the final bounce with no CPU penalty in a non-realtime context.

## ADAA

- Apply to both soft-clip and hard-clip diode stages
- ADAA in addition to oversampling, not instead
- Reference: DAFx2020 "Antiderivative antialiasing in nonlinear wave digital filters"

## Hi Gain Mod — ✅ IMPLEMENTED, FIXED on the Red channel (no runtime switch)

> **RESOLVED & IMPLEMENTED 2026-06-18 (Step 4b, dsp-validator PASS).** Hi Gain is a **fixed
> mod on the Red channel only**, chosen at construction — not a runtime swap. Topology pinned
> from the Theseus page-28 trace: **SW1B switches R3(1k) ∥ R2(100k) in the Stage-1 feedback
> floor**, i.e. it **raises the Z_upper floor resistor** (NodeF → floor + DRIVE → output),
> shifting the whole DRIVE range up ("9 o'clock acts like noon").
>
> **It is NOT a scattering-matrix swap.** Because Stage1's R-type matrix recomputes live from
> port impedances (`ImpedanceCalc` reads them on every impedance propagation), the mod is just
> a different floor *resistance* — no precomputed second matrix, no `setSMatrixData()`, no
> solver re-run. Implementation: `Stage1(bool hiGain)`; `floorR = hiGain ? HiGain_floor : R6_floor`;
> `setDrive` uses `floorR + rDrive`. `MonarchChannel(bool hiGain)` forwards the flag (Yellow=false,
> Red=true).
> - **No** `hi_gain_*` APVTS parameter, **no** `pendingHiGain` atomic, **no** per-block Hi-Gain
>   check. (The SW-1/SW-2 *clipping-mode* swaps remain runtime.)
> - `HiGain_floor = 39k` is a **behavioural tuning** on the matsumin 10k base (the literal
>   Theseus 100k floor is ~+13 dB, far hotter than the documented subtle mod). Raise toward
>   100k for a hotter Red — single constant in `Stage1.h`.
> - Validated `tests/Stage1_HiGain.cpp`: hotter at every DRIVE point (+6.6→+1.7 dB), monotonic,
>   Red@9:00 = 13.79 dB ≈ Yellow@noon = 13.90 dB (−0.12 dB).
>
> The superseded matrix-derivation / `setSMatrixData()` notes below are retained only as the
> record of the earlier (wrong) R29∥R8 approach — they do **not** describe the implementation.

> **⚠️ UNDER REVISION 2026-06-16 — DO NOT IMPLEMENT THE CODE BELOW YET.** A hi-res trace of
> the Theseus schematic (page 28) shows the previous "R29(22k) ∥ R8(27k) in Z_lower Branch2"
> model is **wrong**. In Theseus, R29/R30(22k) are bypass-LED current-limit resistors and
> R27/R28(47R) are VCC supply-filter resistors — both in the **power supply**, not the gain
> stage. The actual Hi-Gain element is **DIP switch SW1B switching R3(1k)** in the Stage-1
> **DRIVE/feedback (Z_upper)** network (Theseus manual: Hi Gain "shifts the gain range of the
> drive knob"). Exact wiring is not yet pinned, and matsumin (primary source) lacks the mod
> entirely. See circuit.md Section 6 for the full discrepancy note. **Confirm the corrected
> topology before writing Stage-1 Hi-Gain code.** The mechanism (precomputed scattering
> matrices switched via `setSMatrixData()`, no tree reconstruction, Stage 1 linear) is
> unchanged regardless of which element ends up being correct.

**Superseded prior code (DO NOT USE — based on the wrong R29∥R8 model):**
```cpp
// WRONG per Theseus trace — kept only to show what was corrected.
// constexpr double R8_standard = 27.0e3;
// constexpr double R8_hi_gain = (27.0e3 * 22.0e3) / (27.0e3 + 22.0e3) + 47.0; // ≈ 12168 Ω
```

Select the matrix **once at construction** via `setSMatrixData()` from the `hiGain` ctor flag
(Red = Hi-Gain, Yellow = stock). No runtime switch, no tree reconstruction, no atomic.

**Effect on Stage 1 gain range:**
A smaller Branch2 resistor reduces Z_lower in the mid/high band, increasing
Av(s) = 1 + Z_upper(s)/Z_lower(s) — targeting the documented +4 dB shift. Measure the
actual shift from the implemented model via the frequency-response test; do not assume
specific Av values until validated.

Hi Gain is fixed per channel (Red on, Yellow off); only the clipping mode (SW-1/SW-2) changes
at runtime. They no longer share a block-start check — clipping mode is the only per-block
topology update.

## Pot Tapers

- **DRIVE (100kB):** Linear. `R = R_max * x`
- **TONE (25kB):** Linear. `R = R_max * x`
- **VOL (100kA):** Audio. Wiper fraction `pow(10.0, 1.8 * (x - 1.0))` — noon = −18 dB. (Exponent
  fitted to the real-pedal captures, 2026-06-21; the textbook "ideal log" 2.0/−20 dB was ~2 dB too
  quiet at noon vs the captures. See `src/dsp/VolumePot.h`.)
- **PRESENCE (50kB):** Linear. `R = R_max * x`

Never apply audio taper to DRIVE, TONE, or PRESENCE. Never apply linear taper to VOL.

## Component Values

> **CORRECTED 2026-06-15** — Stage 1 / input network values updated per circuit.md
> Sections 5–7.

See `circuit.md` for full table with schematic reference designators. Key values per channel
(using matsumin schematic refs as primary):
- C3 = 10nF (input coupling cap; series from input to pin3+, DC block w/ R4)
- R4 = 1M (pin3+ to BIAS; DC bias / return for C3)
- R5 = 1M (input pulldown to GND)
- R7 = 33k, C5 = 10nF (Stage 1 Z_lower Branch1: NodeF→GND, series)
- R8 = 27k, C6 = 10nF (Stage 1 Z_lower Branch2: NodeF→GND, series; Hi Gain: R8→R8_eff≈12.17k)
- C4 = 100pF (Stage 1 Z_upper: NodeF↔NodeG, parallel with R6+DRIVE)
- R6 = 10k (matsumin label), DRIVE = 100kB (Stage 1 Z_upper: NodeF↔NodeG, series, ∥ C4).
  **Implemented floor: Yellow = 1k (Theseus stock R2∥R3, nearly-clean min), Red = 39k (Hi-Gain).**
  See `Stage1.h` `R6_floor` / `HiGain_floor` (voicing decision 2026-06-19).
- R9 = 10k (Stage 2 input resistor; Av = –R10/R9 = –22)
- R10 = 220k (Stage 2 feedback resistor; always present)
- R11 = 6.8k (SW-1 feedback branch series R, with diode network; branch ∥ R10, gated by SW-1)
- R12 = 1k (Stage 2 output series R; always present; feeds node_HC / TONE pot top terminal)
- R13 = 6.8k (TONE pot wiper → node_T_out)
- R14 = 1M (output bleed)
- C7 = **100nF** (Stage 2 input coupling; confirmed from BOTH schematics)
- C8 = 10nF (TONE pot bottom terminal → BIAS); C9 = 10nF (Trim → C9 → BIAS, presence path)
- C11 = 1µF (output coupling)
- TONE = 25kB (3-terminal pot tap, R-type adaptor), VOL = 100kA, Trim = 50kB (2-terminal rheostat)

## Op-amp Rail Saturation (9V supply — verified, no charge pump)

> **Added 2026-06-16.** The KOT runs on a single **9V** supply (no doubler/charge pump;
> verified from both schematics + Theseus measured pin voltages: V+ ≈ 9.15V, V− = 0V, bias
> ≈ 4.5V). See circuit.md Section 4. The op-amps are NOT rail-to-rail.

- After each op-amp stage's linear WDF solve, **clamp the op-amp output to its supply rails**:
  realistic JRC4580 swing saturates ~1.3–1.5V from each rail → usable swing ≈ **±3.3V around
  bias**. In the bipolar (BIAS = 0V) model this is an **output ceiling of ≈ ±3.3V**.
- Use a **soft/gradual saturation** (e.g. a tanh-like or diode-to-rail soft knee centred so
  the linear region is untouched and the knee begins near ±3.0–3.3V), **not** a hard clip —
  the JRC4580 saturates gently.
- **Why it is required and why it is tone-safe:**
  - **Boost mode** (SW-1 OFF, SW-2 OFF) has no diodes; the rails are the *only* nonlinearity.
    Without this, Boost is an unphysical infinite-headroom clean boost. The Theseus manual
    confirms the hardware clips at high gain.
  - **Overdrive / Distortion / Both:** the diodes clamp at ≈ ±1.64V (SW-1) / ≈ ±0.584V
    (SW-2), far below ±3.3V, so the rail saturation **never engages** → **no tone change** in
    any diode mode. This is the guarantee that adding rail saturation does not affect tone.
- Apply per stage if Stage 1 can also rail at extreme DRIVE; in practice Stage 1's gain
  (≤ ~8×) rarely rails on its own, but Stage 2 (×22) reaches the rails in Boost. Validate the
  onset in the Boost-mode test (Step 6); measure, don't assume the exact knee.

## Linear-stage bilinear warping (OPEN DECISION — affects Stage 1/2/Tone peak frequencies)

> **Found 2026-06-16 during Stage 1 validation.** The WDF (bilinear/trapezoidal cap model)
> compresses high frequencies near Nyquist, shifting frequency-shaping *peaks* downward at
> low sample rates. Measured Stage 1 peak (drive=0.5): analog **3803 Hz**, WDF **2626 Hz @
> 48k**, **3142 Hz @ 96k**, **3476 Hz @ 192k**, **3728 Hz @ 768k** → analog as fs→∞. The
> peak *gain* (+13.9 dB) and DC/DRIVE behaviour are correct at every rate — only the peak
> *frequency* warps. At a 48k base rate the −1.2 kHz shift is audible. CCRMA hit the same
> wall and **prewarped the bilinear transform to the peak (4194 Hz)**.

This matters because the linear stages (Stage 1 gain shaping, Stage 2 HPF, Tone) are
currently spec'd to run at the **base sample rate** (oversampling wraps only the clipping
stages). Options to resolve (pick per the CPU-vs-accuracy tradeoff):

1. **Oversample the linear stages too** (run the whole channel, or at least the
   frequency-shaping WDF, at the oversampled rate). Most faithful; raises CPU. At 4×(48→192k)
   the Stage 1 peak is 3476 Hz (−327 Hz from analog); at 8× it is ~3646 Hz.
2. **Prewarp the WDF capacitors** to the peak region (CCRMA's approach) — cheap (no extra
   CPU), corrects the dominant peak; needs a custom cap discretization (chowdsp `CapacitorT`
   uses `R=1/(2·C·fs)` with no prewarp hook) and may slightly mis-place secondary features.
3. **Accept** the warping; document it and recommend a ≥96k project rate.

Until decided, the Stage 1 validation gate (Step 4a) checks the peak **gain** and DC/DRIVE
behaviour (all correct), and records the warped peak frequency rather than failing on it.

## Linear-stage accuracy at base rate — RESOLVED 2026-06-17 (was a bug, not warping)

> **A large peak-frequency error first looked like bilinear warping, but it was an
> implementation bug** in Stage 1's output reconstruction: it read the op-amp non-inverting
> input via the *source* port (`voltage(vPlusPort)`), whose node voltage averages `Vs[n]` and
> `Vs[n−1]` (the root scatter schedules the source's incident/reflected one sample apart).
> That 2-point average is a spurious low-pass that drooped the HF response ~0.2 dB by 5 kHz —
> enough to drag the very flat gain peak down ~880 Hz. **Fix: reconstruct V(NodeG) from two
> *passive* port voltages so V(NodeF) cancels exactly — `voltage(branch1) − voltage(driveR)`.**

**Lesson for every R-type stage:** when reading an output by combining port voltages, use
**passive** ports (resistors, caps, R+C series) so shared node terms cancel in the same time
frame. **Do not read the `ResistiveVoltageSourceT` (input) port voltage** — its `a`/`b` are a
sample apart, giving a 2-point-average low-pass. Reconstruct node voltages from passive ports.

**Result:** with the fix, the WDF matches the true bilinear transform of the analog circuit
to within the small, expected bilinear warp — Stage 1 peak vs analog 3803 Hz: **−74 Hz @ 48k**
(drive=0.5; worst-case −158 Hz at the broad low-DRIVE peak), **−23 Hz @ 96k**, **<12 Hz @
192k**. So **the linear stages are accurate at the base sample rate — no oversampling or
prewarping of the linear stages is required** for correct voicing. (Oversampling remains for
the *clipping* stages' anti-aliasing only, per the Oversampling section.) Prewarping was
explicitly rejected: a fixed prewarp freezes the gain peak in place across the DRIVE range
(the analog peak sweeps ~2.8–5.0 kHz with DRIVE), so it only matches at one knob setting.

## Signal Calibration

- Internal nominal: **-12 dBu**
- **Internal WDF voltages are REAL circuit volts**, not normalised. This is essential: the
  diode thresholds (≈0.584V / ≈1.64V) and op-amp rails (≈±3.3V) must be hit at the correct
  *absolute* signal levels, or the clipping onset/feel will be wrong. Do not normalise to
  ±1.0 internally.
- The plugin input maps the host signal to absolute volts at the circuit input. A real guitar
  runs ~0.1–0.3 Vpk (single-coil) up to ~1 Vpk (hot humbucker, hard transients); -12 dBu ≈
  0.275 Vpk sits in this range. Calibrate so -12 dBu lands at a realistic instrument level
  relative to the gain stages.
- **Input trim (±12 dB)** absorbs hotter/quieter pickups — mirroring how a player sets guitar
  volume / pedal placement to position the clipping. **Output trim** rematches level after.
  No tone-shaping in the trims; they only set where the (fixed-threshold) nonlinearities sit.
- Input trim → VU meter → Yellow channel → Red channel → VU meter → Output trim

## processBlock Structure (per channel)

```
1. Determine active oversampling factor:
   - isNonRealtime() ? read oversampling_render : read oversampling_realtime
   - If active factor != current factor: set pendingOversamplingFactor, reinit oversampler
2. Check pendingClippingMode (per channel) — update SW-1/SW-2 scattering matrices if changed
   (Stage 1 Hi-Gain is fixed at construction — Red on, Yellow off — so no Stage 1 swap here)
3. Read APVTS parameter values (smoothed) for this channel
4. Apply taper conversion (audio taper to VOL only)
5. Update WDF node values
6. If channel bypassed:
   - Copy input → output directly (no DSP, no oversampler)
   - Return early
7. Else:
   a. Upsample clipping stage block (active factor may be 1x = no-op)
   b. Process full WDF chain sample by sample
   c. Downsample clipping stage block
```

Global processBlock chains: inputTrim → channelYellow.process() → channelRed.process() → outputTrim
