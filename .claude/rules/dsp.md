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
Stage 2 (IC_B) is an **inverting** amplifier. `PolarityInverterT` IS required between the R-type adaptor and the ideal voltage source for Stage 2. Omitting it produces inverted polarity from Stage 2. Stage 1 (IC_A, non-inverting) does NOT need a polarity inverter.

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

### SW-1 Soft-Clip Configuration (CORRECTED 2026-06-16)
The SW-1 feedback branch is `[D4 series D5] ∥ [D2 series D3]` (back-to-back, opposite
polarity MA856 pairs), in series with R11 (6.8k), this combination in parallel with R10
(220k). Two identical diodes in series are electrically equivalent to ONE diode with the
same `Is` and `n` doubled (`n_eff = 2×n_MA856 ≈ 3.024`). Model the entire diode network as
**a single** `DiodePairT` with `n_eff`, in series with R11, in `WDFParallelT` with R10.
Do NOT instantiate two `DiodePairT`s — that models two independent antiparallel pairs,
which is not the actual topology.

```cpp
constexpr double n_eff_MA856 = 2.0 * n_MA856; // ≈ 3.024

wdft::DiodePairT<double, decltype(next), wdft::DiodeQuality::Best> dp { next, Is_MA856, Vt, n_eff_MA856 };
wdft::ResistorT<double> r11 { 6.8e3 };
wdft::WDFSeriesT<double, decltype(r11), decltype(dp)> sw1Branch { r11, dp };
// sw1Branch goes in WDFParallelT with R10 (220k) at IC_B's feedback R-type adaptor.
// SW-1 OFF: precomputed matrix with R10 only (sw1Branch removed from the matrix).
```

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
    Stage1          stage1;       // IC_A non-inverting; includes input network (C3/R4/R5)
    Stage2          stage2;       // IC_B inverting + clipping
    ToneStage       tone;         // passive RC
    VolumePot       volume;
    juce::dsp::Oversampling<double> oversampler;
};

MonarchChannel channelA;
MonarchChannel channelB;
```

Channel routing:
```
inputSample → channelA.process() → channelA_output → channelB.process() → output
```

Each channel has independent APVTS parameters (e.g., `drive_a`, `drive_b`, `tone_a`, `tone_b` etc.).

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

## Hi Gain Mod — Stage 1 Scattering Matrix Switch

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

Switch at block start via `setSMatrixData()` when `pendingHiGainA/B` changes. No tree reconstruction.

**Effect on Stage 1 gain range:**
A smaller Branch2 resistor reduces Z_lower in the mid/high band, increasing
Av(s) = 1 + Z_upper(s)/Z_lower(s) — targeting the documented +4 dB shift. Measure the
actual shift from the implemented model via the frequency-response test; do not assume
specific Av values until validated.

The Hi Gain and clipping mode changes are independent — both can change simultaneously
within the same block start check. Handle them as separate atomic flags.

## Pot Tapers

- **DRIVE (100kB):** Linear. `R = R_max * x`
- **TONE (25kB):** Linear. `R = R_max * x`
- **VOL (100kA):** Audio. `R = R_max * pow(10.0, 2.0 * x - 2.0)` or equivalent
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
- R6 = 10k, DRIVE = 100kB (Stage 1 Z_upper: NodeF↔NodeG, series, in parallel with C4)
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
- Input trim → VU meter → Channel A → Channel B → VU meter → Output trim

## processBlock Structure (per channel)

```
1. Determine active oversampling factor:
   - isNonRealtime() ? read oversampling_render : read oversampling_realtime
   - If active factor != current factor: set pendingOversamplingFactor, reinit oversampler
2. Check pendingClippingMode (per channel) — update SW-1/SW-2 scattering matrices if changed
3. Check pendingHiGain (per channel) — update Stage 1 scattering matrix if changed
4. Read APVTS parameter values (smoothed) for this channel
5. Apply taper conversion (audio taper to VOL only)
6. Update WDF node values
7. If channel bypassed:
   - Copy input → output directly (no DSP, no oversampler)
   - Return early
8. Else:
   a. Upsample clipping stage block (active factor may be 1x = no-op)
   b. Process full WDF chain sample by sample
   c. Downsample clipping stage block
```

Global processBlock chains: inputTrim → channelA.process() → channelB.process() → outputTrim
