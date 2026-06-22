# Architecture Rules

## Threading

- All DSP runs on the **audio thread**
- All UI runs on the **message thread**
- Cross-thread communication: `std::atomic` only — no locks, mutexes, or blocking calls on the audio thread
- Meter levels: `std::atomic<float>` written by audio thread, read by UI timer
- The two series channels are named by LED colour: **Yellow** (first) and **Red** (second).
- Bypass state per channel: `std::atomic<bool> bypassedYellow`, `std::atomic<bool> bypassedRed`
- Clipping mode per channel: `std::atomic<int> pendingClippingModeYellow`, `std::atomic<int> pendingClippingModeRed`
- **Hi Gain is NOT a runtime parameter.** It is a *fixed* Theseus mod baked into the Red
  channel's Stage 1 only (passed to `MonarchChannel` at construction); Yellow is always
  stock. There is no `pendingHiGain` atomic, no UI toggle, and no runtime scattering-matrix
  swap for Hi Gain — the Red channel simply uses the Hi-Gain Stage 1 scattering matrix it was
  built with. (The clipping-mode SW-1/SW-2 swaps are still runtime per channel.)
- Oversampling: `std::atomic<int> pendingOversamplingFactor` (derived each block from `isNonRealtime()` + APVTS; single global setting applies to both channels)
- Parameter changes: JUCE `AudioProcessorValueTreeState` with smoothed parameter values

## Plugin Structure

```
MonarchAudioProcessor          ← AudioProcessor subclass
  AudioProcessorValueTreeState apvts
  InputTrimStage
  MonarchChannel channelYellow { false }  ← full single-channel DSP; stock Stage 1
    InputFilter   (linear WDF)
    Stage1        (linear WDF, IC_A non-inverting ideal op-amp — stock voicing)
    Stage2        (linear WDF, IC_B inverting ideal op-amp + R-type at feedback node)
    SW1SoftClip   (nonlinear WDF — MA856×4 in feedback, 2 precomputed topologies)
    SW2HardClip   (nonlinear WDF — 1S1588×2 shunt, 2 precomputed topologies)
    ToneStage     (linear WDF passive)
    VolumePot     (linear)
    juce::dsp::Oversampling oversampler
  MonarchChannel channelRed { true }      ← identical chain, independent parameter set,
    (same structure as channelYellow)        EXCEPT Stage 1 uses the fixed Hi-Gain voicing
  OutputTrimStage
  std::atomic<float> inputLevelL, inputLevelR
  std::atomic<float> outputLevelL, outputLevelR
  std::atomic<bool>  bypassedYellow, bypassedRed
  std::atomic<int>   pendingClippingModeYellow, pendingClippingModeRed
  std::atomic<int>   pendingOversamplingFactor
  // No pendingHiGain — Hi Gain is fixed-on for Red, fixed-off for Yellow (construction time)
```

## Parameters (APVTS IDs)

The two channels (**Yellow** = first, **Red** = second) have mirrored parameter sets — with
one exception: there is **no Hi Gain parameter**. Hi Gain is a fixed mod on the Red channel
only (baked in at construction), so it is not exposed or automatable. See the Hi Gain note
below the table.

| ID | Label | Range | Default | Notes |
|----|-------|-------|---------|-------|
| `drive_yellow` | Drive Yellow | 0.0–1.0 | 0.5 | Linear taper (B-pot) applied in DSP |
| `tone_yellow` | Tone Yellow | 0.0–1.0 | 0.5 | Linear taper (B-pot) applied in DSP |
| `volume_yellow` | Volume Yellow | 0.0–1.0 | 0.5 | Audio taper (A-pot) applied in DSP |
| `presence_yellow` | Presence Yellow | 0.0–1.0 | 0.0 | Linear taper; default fully CCW (no boost) |
| `clipping_mode_yellow` | Clipping Yellow | 0/1/2 | 1 | `AudioParameterChoice`: "Boost"/"Overdrive"/"Distortion" (3-way) |
| `bypass_yellow` | Bypass Yellow | true/false | false | `AudioParameterBool` |
| `drive_red` | Drive Red | 0.0–1.0 | 0.5 | As above |
| `tone_red` | Tone Red | 0.0–1.0 | 0.5 | As above |
| `volume_red` | Volume Red | 0.0–1.0 | 0.5 | As above |
| `presence_red` | Presence Red | 0.0–1.0 | 0.0 | As above |
| `clipping_mode_red` | Clipping Red | 0/1/2 | 1 | As above |
| `bypass_red` | Bypass Red | true/false | false | `AudioParameterBool` |
| `input_trim` | Input Trim | -12.0 to +12.0 dB | 0.0 | Plugin-only; `AudioParameterFloat` |
| `output_trim` | Output Trim | -12.0 to +12.0 dB | 0.0 | Plugin-only; `AudioParameterFloat` |
| `supply_voltage` | Supply Voltage | 0/1/2 | 0 (9V) | `AudioParameterChoice`: "9V"/"12V"/"18V" — voltage mod (scales op-amp rails only; diode thresholds fixed). UI: `VoltageSelector` "(+) 9V (-)" at the pedal-face top centre. See dsp.md "Supply Voltage". |
| `oversampling_realtime` | Oversampling (Live) | 0/1/2/3 | 2 (4x) | `AudioParameterChoice`: "1x"/"2x"/"4x"/"8x" — active during live playback |
| `oversampling_render` | Oversampling (Render) | 0/1/2/3 | 3 (8x) | `AudioParameterChoice`: "1x"/"2x"/"4x"/"8x" — active when `isNonRealtime()` |

Default `clipping_mode` = 1 (Overdrive = SW-1 ON, SW-2 OFF). This is the factory default per Analog Man.
Default `presence` = 0.0 (fully CCW = no boost). This is the factory default per Analog Man.

Note: Use `std::make_unique<AudioParameterBool>(...)` for bool params; APVTS does not accept raw bool.

**Hi Gain (fixed mod, Red only):** The Theseus Hi-Gain mod is *not* a parameter. The Red
channel's `MonarchChannel` is constructed with `hiGain=true` and the Yellow channel with
`hiGain=false`; Stage 1 selects its fixed scattering matrix accordingly. There is nothing to
save/restore, automate, or toggle. (Topology of the mod itself is still an open item — see
circuit.md Section 6 — but it no longer affects the parameter/threading model either way.)

## Clipping Mode Mapping

3-way per channel (decision 2026-06-19 — the "Both" stacked mode was dropped to suit the
3-position hardware toggle; `MonarchChannel::processClip` still handles any SW-1/SW-2 combo, so
re-adding it is a 1-line change).

| Value | Label | SW-1 | SW-2 |
|-------|-------|------|------|
| 0 | "Boost" | OFF | OFF |
| 1 | "Overdrive" | ON | OFF |
| 2 | "Distortion" | OFF | ON |

## Channel Routing

```
guitar input → [input trim] → channelYellow.process() → channelRed.process() → [output trim] → amp output
```

When a channel is bypassed, its `process()` returns the input unchanged (no DSP). Both bypass states are independent. When both are bypassed the signal passes through both switches dry, matching the hardware's true-bypass behaviour.

## Bypass

- True bypass per channel: input routed directly to output, zero DSP
- Crossfade on bypass transition (~5ms ramp) to prevent clicks
- `std::atomic<bool> bypassedYellow / bypassedRed` — audio thread polls, applies ramp
- On DAW recall, bypass visual state reflects the restored APVTS parameter value

## Oversampling

- Single `juce::dsp::Oversampling` instance per channel (two total)
- Wraps only the nonlinear clipping stages (SW-1 and SW-2) per channel
- Changing factor: `pendingOversamplingFactor` atomic → audio thread reinits both oversamplers at start of next processBlock. Brief gap (one block) acceptable.
- Call `oversampler.initProcessing(samplesPerBlock)` in `prepareToPlay` for both channels

## prepareToPlay Responsibilities

All of the following in `prepareToPlay(sampleRate, samplesPerBlock)`:
- `.prepare(sampleRate)` on every `CapacitorT` in both channels
- `oversampler.initProcessing(samplesPerBlock)` for both channels
- Reset all smoothed parameter values to current APVTS values
- Reset both bypass crossfade states
- Initialise all precomputed scattering matrices for all clipping modes (both channels)

## processBlock Structure

> **IMPLEMENTED 2026-06-18 (auval PASS) — what the shipped `processBlock` actually does, where
> it differs from the original plan below:**
> - **Params read directly from cached APVTS atomic pointers** (`getRawParameterValue`) once per
>   block, not via separate `pending*` atomics. Tapers are applied inside each stage (VOL audio
>   taper in `VolumePot`, DRIVE/TONE/PRESENCE linear in their stages), so there is no separate
>   taper step. Clipping mode is pushed per block via `MonarchChannel::setClippingMode` (no
>   scattering swap needed — the soft/hard clip stages are structural).
> - **Dual-mono stereo:** one `ChannelStrip {MonarchChannel yellow, red}` per audio channel
>   (array of 2), so L/R have independent WDF state but share knob settings.
> - **Calibration:** host float ×`circuitVoltsPerFS` (1 V/FS) → absolute circuit volts; input/
>   output trim in dB around that. Meters are peak, post-trim.
> - **Param smoothing (Step 10, 2026-06-22):** VOLUME and input/output TRIM are smoothed (~5 ms) so
>   DAW automation steps don't zipper — the pure level multipliers, tone-neutral (no WDF state). VOL
>   via a one-pole on the wiper gain in `VolumePot`; trims via `inTrimGain`/`outTrimGain`
>   `SmoothedValue` ramped per sample. DRIVE/TONE/PRESENCE are NOT smoothed (WDF circuit elements;
>   continuous knob turns are already click-free). So "smoothed parameter values" above is realized
>   for the level controls only, by design.
> - **Bypass:** click-free ~5 ms crossfade per channel (`juce::SmoothedValue` wet 1→0). Current
>   build crossfades wet/dry every sample (always processes); the "skip DSP when bypassed" CPU
>   optimisation lands with oversampling (Step 7/8), where it matters.
> - **Oversampling not yet wrapped** — the chain currently runs at base rate (linear stages are
>   accurate at base rate; clip-stage anti-aliasing is the Step 7 add).

The original planned structure (target once oversampling + smoothing land):

```
1. Determine active oversampling factor:
   isNonRealtime() ? oversampling_render : oversampling_realtime (read from APVTS)
   If active factor != current → set pendingOversamplingFactor, reinit both oversamplers
2. Check pendingClippingModeYellow/Red — update SW-1/SW-2 scattering matrices for changed channels
   (Hi Gain is fixed per channel — no per-block Stage 1 swap)
3. Read all APVTS parameter values (smoothed) for both channels
4. Apply taper conversion (audio taper to VOL Yellow/Red only; all others linear)
5. Update WDF node values in both channels
6. Apply input trim gain
7. Update input meter levels (std::atomic write)
8. Process channelYellow:
   - If bypassedYellow: copy input → output, skip DSP and oversampler
   - Else: upsample → WDF chain → downsample
9. Process channelRed (input = channelYellow output):
    - If bypassedRed: copy input → output, skip DSP and oversampler
    - Else: upsample → WDF chain → downsample
10. Apply output trim gain
11. Update output meter levels (std::atomic write)
```

## State Save/Restore

- Full state via `AudioProcessorValueTreeState::state` (JUCE XML serialise)
- All parameters for both channels saved/restored
- Bypass states per channel saved/restored via APVTS
- Oversampling setting saved/restored
