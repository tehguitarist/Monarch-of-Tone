# UI Rules

## General

- Custom `LookAndFeel` (`MonarchLookAndFeel`) — no default JUCE styling. All drawing lives in
  LookAndFeel overrides / dedicated components; zero drawing in DSP. UI fully decoupled from DSP
  (visual design replaceable without touching DSP). No foleys_gui_magic / XML UI builders. Gin may
  be used for supplementary components.
- Call sites use `juce::Colour(MonarchLookAndFeel::cFoo)` — never hardcode hex in component code.

## Structure

Two layers:
- **Peripheral shell** (`PluginEditor`, shared-look palette in `MonarchLookAndFeel`): Input/Output
  side panels (halo trim knob + TRIM label + always-visible dB readout + VU meter) and the
  oversampling strip (LIVE/RENDER combos bound to `oversampling_realtime`/`oversampling_render` +
  a UI-size scale button). Resizable, aspect-locked window: base **612×354**, 0.5–2.5×, with
  per-session (APVTS `uiScale`) + cross-session (`ApplicationProperties` `defaultScale`)
  persistence. `refreshFonts(sc)` rescales label fonts in `resized()`.
- **Pedal face** (`PedalFace`): royal-purple body (`cPedalPurple`) with gold (`cPedalGold`)
  Papyrus lettering + gold compass rose, over a graded background texture (`MonarchAssets`).

LookAndFeel overrides: halo trim knob (`drawRotarySlider`, ID "trim"), image knobs (main +
"presence"), OS combo boxes (`drawComboBox` + fonts), bypass footswitch (`drawButtonBackground`,
ID "bypass"), the UI-size scale button (ID "scale", drawn to match the OS combo boxes), and the
lit-on/dim-off toggle style (ID "ostoggle", drawn to match the OS combo boxes but keyed on
`getToggleState()` — currently used by the Trim Lock button).

## Layout

```
┌──────────────────────────────────────────────────────────┐
│ [INPUT: trim/VU]          (+) 9V (-)        [OUTPUT: trim/VU] │  ← side panels + supply selector
│            YELLOW (left)          RED (right)               │
│   [VOL] [DRIVE]                    [VOL] [DRIVE]            │  ← main knobs (V/D top row, Tone below)
│        [TONE]                          [TONE]              │
│  [CLIP Y]   B ●LED-Y      ●LED-R A   [CLIP R]              │  ← 3-way clip switches + LEDs + A/B badges
│  [PRES Y]      ◆ MONARCH OF TONE ◆      [PRES R]           │  ← presence trims + logo + compass
│       [BYPASS Y ⬤]              [BYPASS R ⬤]               │  ← footswitches
│  [OS LIVE] [OS RENDER]                      [UI SIZE]      │  ← oversampling strip (peripheral)
└──────────────────────────────────────────────────────────┘
```

Yellow left / Red right (hardware position unchanged); signal flow is Red→Yellow, surfaced only by
the A/B badges. No Hi-Gain control (fixed Red-channel mod). Reference look: royal purple + gold,
black knurled knobs, chrome footswitches.

## Controls

- **Main knobs** (Volume/Drive/Tone per channel + small Presence trim per channel): rotary
  `Slider` + image-based LookAndFeel. Taper applied in DSP, not UI. All 8 show a 2-decimal popup
  tooltip while dragging (`setPopupDisplayEnabled` + a `textFromValueFunction` override, since the
  attachment's default formatting falls back to 7 decimals — these params have no range interval).
  Presence default = fully CCW (hardware default).
- **Supply Voltage selector** (`VoltageSelector`, top centre): "(+) 9V (-)" bound to
  `supply_voltage` (9/12/18V). (+)/(-) step the value; each arrow is bright only when a step that
  direction exists. Gold Papyrus lettering; `setFontScale(sc)` rescales with UI size.
- **Clip mode selector** (`ClipSwitch`, ×2): 3-position hardware-style toggle Boost/Overdrive/
  Distortion (`clipping_mode_*` 0/1/2), default Overdrive. Toward the channel edges, below knobs,
  above footswitches; Red mirrored.
- **Bypass footswitches** (×2): footswitch-style toggle per channel, bottom of each column.
- **LEDs** (×2, `LEDIndicator`): Yellow LED left, Red LED right; ON = active, dim = bypassed (from
  `bypassedYellow/Red`). **A/B badges** — small gold "B" outside the Yellow LED, "A" outside the
  Red LED — identify the real signal order (Red = A first, Yellow = B second); non-interactive.
- **Input/Output trim** (peripheral side panels): ±18 dB halo knobs with a fixed, always-visible dB
  readout label (`Slider::onValueChange`, not a hover tooltip).
- **Trim Lock** (`trimLockButton`, peripheral strip, between the RENDER combo and the version
  string): a "LOCK" toggle bound to `trim_lock`, lit-on/dim-off via the `"ostoggle"` L&F component
  ID (`MonarchLookAndFeel::drawButtonBackground`). While on, moving either trim knob mirrors the
  equal-and-opposite **change** onto the other (delta-linked — the pair's existing offset is
  preserved, so enabling the lock never snaps a knob); off, the knobs are independent. Defaults ON.
  Sized ~1.5× a 2-glyph OS combo box so "LOCK" doesn't truncate at the 0.5× minimum UI scale (the
  L&F floors button text at 7 px, so below ~0.75× the font stops shrinking with the box). Logic in
  `MonarchAudioProcessorEditor::mirrorTrim`.
- **Oversampling** (peripheral strip): two combo boxes, "Live" and "Render" (1x/2x/4x/8x). Render
  engages automatically when the DAW bounces. Plus the UI-size button (scale popup menu, with "set
  current scale as default").

## Colours

The shared-look palette and the pedal-face palette are `static constexpr juce::uint32` constants in
`MonarchLookAndFeel.h` (`cBackground`, `cTrim*`, `cMeter*`, `cOS*`, `cKnob*`, `cBypassLabel`,
`cPedalPurple*`, `cPedalGold*`). LED colours: Yellow channel yellow, Red channel red.

## VU Meters

Bar-style (`VUMeter`), input and output, calibrated to −12 dBu nominal (~60% deflection). ~300 ms
release, driven by the editor `juce::Timer` (33 Hz) reading `std::atomic<float>` levels. Input =
post input-trim / pre-DSP; output = post-DSP / post output-trim. Mono display.

## Threading in UI

UI timer reads `std::atomic` meter values + bypass states — no direct DSP access. Parameter changes
go through APVTS attachments — no direct DSP calls from the UI.

## Visual check

Build/run the `UISnapshot` console app (headless editor → PNG, no display needed) or the Standalone
plugin.

## Asset conventions

Source art lives in `ui/` (originals); `tools/process_ui_assets.sh` resizes/compresses into
`assets/ui/` for `BinaryData` embedding — re-run it after editing any source art, never hand-edit
`assets/ui/`. Conventions: art drawn at "noon" (rotated in code), 2x resolution at 100% UI scale (stays
sharp to ~200%), alpha-channel + rotation-safe except the background texture, crop-to-fit rather than
stretch.
