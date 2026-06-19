# UI Rules

> **IMPLEMENTED 2026-06-18 — peripheral elements (shared-look, match a sibling plugin).** The
> authoritative spec for these is the project brief; they live in `src/ui/`:
> - `MonarchLookAndFeel.{h,cpp}` — the **shared-look colour palette** (`cBackground`, `cTrim*`,
>   `cMeter*`, `cOS*`, `cKnob*`, `cBypassLabel`, as `static constexpr juce::uint32`; use
>   `juce::Colour(MonarchLookAndFeel::cFoo)` at call sites). Overrides: halo trim knob
>   (`drawRotarySlider`, componentID "trim"), OS combo boxes (`drawComboBox` + fonts), bypass
>   footswitch (`drawButtonBackground`, componentID "bypass" — octagon nut + socket + dome),
>   scaling text-button font.
> - `VUMeter.h` (22-segment, 3 red/5 yellow/14 green, ~60% at −12 dBu, 33 fps ~300 ms release
>   driven by the editor timer) and `LEDIndicator.h` (green core + glow, read bypass param).
> - `PluginEditor.{h,cpp}` — Input/Output side panels (trim halo + TRIM label + VU), the
>   oversampling strip (LIVE/RENDER combos bound to `oversampling_realtime`/`oversampling_render`
>   + UI-size scale button), and the **resizable, aspect-locked** window (base 694×354, 0.5–2.5×)
>   with per-session (APVTS `uiScale`) + cross-session (`ApplicationProperties` `defaultScale`)
>   scale persistence. `refreshFonts(sc)` rescales every label font in `resized()`.
>
> The colour-scheme block further down (`colourBackground`, etc.) is the ORIGINAL plan; the
> shared-look palette above is what the peripheral elements actually use. The **pedal face**
> (below) is still to build and reuses the bypass-footswitch L&F + `LEDIndicator` per channel.
> Visual check: build/run `UISnapshot` (headless editor → /tmp/monarch_ui.png) or the Standalone.

## General

- Custom `LookAndFeel` subclass — no default JUCE styling anywhere
- All drawing code in LookAndFeel overrides only — zero drawing in component or DSP logic
- No foleys_gui_magic or XML-driven UI builders
- Gin library (github.com/FigBug/Gin) may be used for supplementary components
- UI fully decoupled from DSP — visual design replaceable without touching DSP

## Layout — Two-Channel Pedal Face

Reference: `pedal_picture.png` (project root) — **royal purple** enclosure (`#6A0956`), **gold**
lettering in **Papyrus** font (`#C6A15B`), gold compass rose centre, black knurled knobs
(`#161019`) with white indicators, chrome footswitches with off-white rings (`#DBDCD5`). The
"ANALOG.MAN" wording is omitted. (Supersedes the earlier white-enclosure reference.)
**Clipping switches (3-way): toward the left & right edges, below the knobs, above the footswitches.**
The King of Tone has two identical channels side by side. We name them by their LED colour:
the first channel is **Yellow**, the second is **Red**. Signal runs Yellow → Red in series.
Layout mirrors the hardware directly. Note there is **no Hi Gain control** — the Hi-Gain mod
is a fixed part of the Red channel only and is not user-switchable (see architecture.md).

```
┌──────────────────────────────────────────────────────────┐
│  [INPUT TRIM]                             [OUTPUT TRIM]  │  ← plugin-only, visually distinct
├──────────────────────────────────────────────────────────┤
│           YELLOW CHANNEL          RED CHANNEL            │
│  [VOL Y]  [DRIVE Y]  [TONE Y]   [VOL R] [DRIVE R] [TONE R] │  ← main knobs
│                                                          │
│  [CLIP Y]      [●LED Y(yellow)] [●LED R(red)]    [CLIP R] │  ← mode selectors + LEDs
│  Boost/OD/Dist/Both                      Boost/OD/Dist/Both     │
│                                                          │
│  [PRESENCE Y]  ◆ MONARCH OF TONE ◆  [PRESENCE R]       │  ← internal trim knobs + logo
│                                                          │
│    [BYPASS Y ⬤]                   [BYPASS R ⬤]          │  ← footswitches, bottom
│  [OVERSAMPLING LIVE]  [OVERSAMPLING RENDER]              │  ← plugin-only controls
│  [IN  METER] ▐▐▐▐▐▐▐▐▐                                 │
│  [OUT METER] ▐▐▐▐▐▐▐▐▐                                 │
└──────────────────────────────────────────────────────────┘
```

The Red channel's panel may carry a small fixed "Hi Gain" badge/label (non-interactive) to
communicate that it is the higher-gain voice — purely cosmetic, not a control.

## Controls

### Knobs (×6 main, ×2 presence)
- Three main knobs per channel: Volume, Drive, Tone
- Labels: "Volume", "Drive", "Tone" (same on both channels; distinguished by Yellow/Red column)
- Drive and Tone: linear taper — applied in DSP, not UI
- Volume: audio taper — applied in DSP, not UI
- Style: large black knurled knobs with white indicator line (reference KOT photo)
- JUCE `Slider` (rotary) + custom LookAndFeel paint
- Presence knobs: smaller, same style, labelled "Presence Yellow" / "Presence Red"
- Default Presence = fully CCW (matches hardware default = no treble boost)

### Hi Gain — no control
There is **no Hi Gain toggle**. The Theseus Hi-Gain mod is a fixed part of the Red channel's
Stage 1 (baked in at construction, see architecture.md) and the Yellow channel is always
stock. At most, the Red panel shows a cosmetic, non-interactive "Hi Gain" badge. No
`AudioParameterBool`, no automation, no LED toggle.

### Clipping Mode Selector (×2, one per channel)
- **3-way switch** per channel: **Boost / Overdrive / Distortion** (the "Both" stacked mode was
  dropped 2026-06-19 to suit a 3-position hardware-style toggle; param `clipping_mode_*` = 0/1/2)
- Styled as a 3-position toggle (hardware aesthetic), not a ComboBox
- Default: "Overdrive" (matches hardware factory default: SW-1 ON, SW-2 OFF)
- **Placement: toward the left & right edges, below the knobs and above the footswitches**

### Bypass (×2 footswitches)
- Large footswitch-style toggle button per channel, at bottom of each channel column
- LED ON = channel active (processing); LED OFF = bypassed
- Both bypass states are independent

### LEDs (×2)
- Small circular indicator per channel, between knobs and bypass
- The channel names come from these LEDs: the first channel's LED is **yellow**
  (`colourLEDActiveYellow`), the second's is **red** (`colourLEDActiveRed`)
- ON = channel active; OFF (dim) = channel bypassed
- State from `std::atomic<bool> bypassedYellow / bypassedRed`

### Input Trim / Output Trim
- Visually distinct from the main knobs (different size, placement, colour tint)
- Range: -12 to +12 dB
- Placed at the top of the plugin window, outside the pedal face area

### Oversampling Controls
- Two controls, clearly grouped and labelled:
  - **"Oversampling (Live)"** — applied during real-time playback; default 4x
  - **"Oversampling (Render)"** — applied when DAW is bouncing/rendering; default 8x
- Each is a `AudioParameterChoice`: 1x / 2x / 4x / 8x
- ComboBox or segmented button for each — clearly readable
- Placed together at the bottom of the plugin window, outside the pedal face area
- A small info label or tooltip is helpful: e.g. "Render mode activates automatically when bouncing"

## Plugin Window

- Fixed size: **700 × 480 px** (wider than single-channel to accommodate two columns)
- Not resizable in v1
- Set via `setSize(700, 480)` in `PluginEditor` constructor

## Colour Scheme

Inspired by the KOT white/gold aesthetic. All colours as named constants in `MonarchLookAndFeel.h`.

```cpp
static constexpr juce::Colour colourBackground    { 0xFF1A1A1A }; // near-black body/frame
static constexpr juce::Colour colourPanelFace     { 0xFFF0EEE8 }; // off-white pedal face
static constexpr juce::Colour colourKnob          { 0xFF1A1A1A }; // black knob cap
static constexpr juce::Colour colourKnobIndicator { 0xFFFFFFFF }; // white indicator line
static constexpr juce::Colour colourAccent        { 0xFFCCA42A }; // gold (KOT compass rose)
static constexpr juce::Colour colourChannelDivider{ 0xFFCCA42A }; // gold divider between channels
static constexpr juce::Colour colourLEDActiveYellow { 0xFFFFC21A }; // Yellow channel LED on
static constexpr juce::Colour colourLEDInactiveYellow { 0xFF3A2E00 }; // dim Yellow LED off
static constexpr juce::Colour colourLEDActiveRed    { 0xFFFF3300 }; // Red channel LED on (KOT-style red)
static constexpr juce::Colour colourLEDInactiveRed  { 0xFF3A1000 }; // dim Red LED off
static constexpr juce::Colour colourLabelText     { 0xFF1A1A1A }; // dark text on white face
static constexpr juce::Colour colourMeterLow      { 0xFF44CC44 }; // meter green
static constexpr juce::Colour colourMeterMid      { 0xFFDDCC00 }; // meter yellow
static constexpr juce::Colour colourMeterHigh     { 0xFFDD2222 }; // meter red
static constexpr juce::Colour colourTrimControl   { 0xFF4488CC }; // blue tint for plugin-only controls
```

## VU Meters

- Bar-style, input and output
- Calibrated to -12 dBu nominal (0 VU = -12 dBu)
- VU ballistics: ~300ms release (attack fast)
- Input: post input-trim, pre DSP chain
- Output: post DSP chain (both channels), post output-trim
- Updated via `juce::Timer` on message thread reading `std::atomic<float>` levels
- Mono display acceptable for v1

## Threading in UI

- UI timer reads `std::atomic` meter values and bypass states — no direct DSP access
- Parameter changes go through APVTS — no direct DSP calls from UI
