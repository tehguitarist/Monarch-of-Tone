# Architecture Rules

## Threading

- All DSP on the **audio thread**; all UI on the **message thread**.
- Cross-thread: `std::atomic` only — no locks/mutexes/blocking on the audio thread.
- Meter levels: `std::atomic<float>` written by audio thread, read by a UI timer.
- The two series channels are named by LED colour: **Yellow** (left, stock) and **Red** (right,
  fixed Hi-Gain). **Signal flow is Red first, Yellow second.** Parameter IDs / member order still
  list Yellow first (save-state back-compat) but `processBlock` runs Red before Yellow. External
  LED badges: **A = Red**, **B = Yellow**.
- Per-channel atomics: `bypassedYellow/Red` (bool), `pendingClippingModeYellow/Red` (int).
- Oversampling: `pendingOversamplingFactor` (int), derived each block from `isNonRealtime()` +
  APVTS; one global setting for both channels.
- **Hi-Gain is NOT a runtime parameter** — a fixed Theseus mod baked into Red's Stage 1 at
  construction (`MonarchChannel{true}`); Yellow is always stock. No `pendingHiGain`, no UI toggle,
  no runtime swap. (Clipping-mode SW-1/SW-2 changes are still runtime.)
- Parameters via `AudioProcessorValueTreeState` with smoothed values (see dsp.md: VOL + trims
  smoothed; DRIVE/TONE/PRESENCE not).

## Plugin Structure

```
MonarchAudioProcessor : AudioProcessor
  AudioProcessorValueTreeState apvts
  ChannelStrip strips[2]           ← dual-mono: one strip per audio channel (L/R)
    MonarchChannel yellow {false}  ← InputFilter, Stage1 (stock), Stage2, SW1SoftClip,
    MonarchChannel red    {true}      SW2HardClip, ToneStage, VolumePot, Oversampling
                                      (red differs only in Stage-1 Hi-Gain floor)
  inTrimGain / outTrimGain (SmoothedValue, includes circuitVoltsPerFS cal)
  std::atomic<float> inputLevelL/R, outputLevelL/R
  std::atomic<bool>  bypassedYellow/Red
  std::atomic<int>   pendingClippingModeYellow/Red, pendingOversamplingFactor
  // No pendingHiGain — fixed-on Red / fixed-off Yellow at construction.
```

## Parameters (APVTS IDs)

Mirrored Yellow/Red sets, **no Hi-Gain param**. IDs list Yellow before Red for back-compat only —
not signal order (see Threading).

| ID | Label | Range | Default | Notes |
|----|-------|-------|---------|-------|
| `drive_{yellow,red}` | Drive | 0–1 | 0.5 | Linear taper in DSP |
| `tone_{yellow,red}` | Tone | 0–1 | 0.5 | Linear taper |
| `volume_{yellow,red}` | Volume | 0–1 | 0.5 | Audio taper |
| `presence_{yellow,red}` | Presence | 0–1 | 0.0 | Linear; default fully CCW (no boost) |
| `clipping_mode_{yellow,red}` | Clipping | 0/1/2 | 1 | Choice: Boost / Overdrive / Distortion |
| `bypass_{yellow,red}` | Bypass | bool | false | `AudioParameterBool` |
| `input_trim` | Input Trim | −12…+12 dB | 0.0 | Plugin-only; `AudioParameterFloat` |
| `output_trim` | Output Trim | −12…+12 dB | 0.0 | Plugin-only |
| `supply_voltage` | Supply Voltage | 0/1/2 | 0 (9V) | Choice 9V/12V/18V — scales op-amp rails only |
| `oversampling_realtime` | Oversampling (Live) | 0/1/2/3 | **1 (2x)** | Choice 1x/2x/4x/8x — live playback |
| `oversampling_render` | Oversampling (Render) | 0/1/2/3 | **2 (4x)** | Choice — when `isNonRealtime()` |

Defaults per Analog Man factory: clipping = Overdrive (SW-1 ON, SW-2 OFF), presence = fully CCW.
Use `std::make_unique<AudioParameterBool>(...)` for bools (APVTS won't take raw bool).

**Clipping mode mapping:** 0 Boost (SW-1 OFF, SW-2 OFF) / 1 Overdrive (ON, OFF) / 2 Distortion
(OFF, ON). The "Both" stacked mode was dropped for the 3-position toggle; `processClip` still
handles any combo, so re-adding is a 1-line change.

## Channel Routing

```
input → [input trim] → channelRed.process() → channelYellow.process() → [output trim] → output
```

Red first (real pedal flow). A bypassed channel returns its input unchanged (no DSP); both bypass
states independent. Both bypassed = dry passthrough (matches hardware true-bypass).

## Bypass

True-bypass per channel; ~5 ms crossfade on transition to prevent clicks (`juce::SmoothedValue`
wet 1↔0). On DAW recall the LED reflects the restored APVTS value. (Current build crossfades every
sample; the "skip DSP when bypassed" CPU optimisation is in the oversampling path.)

## Oversampling

One `juce::dsp::Oversampling` per channel, wrapping the **whole channel** (linear stages + clip
span) so the linear WDF's near-Nyquist bilinear warp shrinks with the OS factor (see dsp.md
"Linear stages run oversampled"). Factor change → reinit both oversamplers + re-`prepareLinear`/
`prepareClip` at the OS rate, at next block start (one-block gap acceptable).
`oversampler.initProcessing(samplesPerBlock)` in prepareToPlay.

## prepareToPlay Responsibilities

`.prepare(sampleRate)` on every `CapacitorT` in both channels; `initProcessing` both oversamplers;
reset smoothed values to current APVTS, bypass crossfades; init all precomputed scattering matrices.

## State Save/Restore

Full state via `apvts.state` (XML). All params for both channels, bypass states, oversampling,
and the per-session `uiScale` property saved/restored.

## Presets

5 factory presets (`src/Presets.h`: `getFactoryPresets()`, `applyFactoryPreset()`) exposed via
JUCE's native programs API (`getNumPrograms`/`getCurrentProgram`/`setCurrentProgram`/
`getProgramName`) → they appear in the host's own preset browser (AU + VST3; no in-plugin preset
UI). Data is plain APVTS-ID → normalized-value pairs applied via `setValueNotifyingHost`.
