#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * Shared LookAndFeel for the Monarch of Tone plugin. The peripheral elements (halo trim knob,
 * oversampling combo boxes, bypass footswitch) match a sibling plugin's look exactly — see the
 * colour palette and per-element specs in ui.md / the project brief.
 *
 * Call sites use juce::Colour(MonarchLookAndFeel::cFoo) — never hardcode hex in component code.
 */
class MonarchLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // ---- Colour palette (shared-look) ----
    static constexpr juce::uint32 cBackground = 0xFF050912u;
    static constexpr juce::uint32 cKnobHighlight = 0xFFF5F5F5u;
    static constexpr juce::uint32 cKnobMid = 0xFFD8D8D8u;
    static constexpr juce::uint32 cKnobShadow = 0xFF949494u;
    static constexpr juce::uint32 cKnobIndicator = 0xFF1A1A30u;
    static constexpr juce::uint32 cTrimLabel = 0xFF5588AAu;
    static constexpr juce::uint32 cTrimArc = 0xFF2A5898u;
    static constexpr juce::uint32 cTrimArcTrack = 0xFF101E30u;
    static constexpr juce::uint32 cMeterLow = 0xFF44CC44u;
    static constexpr juce::uint32 cMeterMid = 0xFFCCBA00u;
    static constexpr juce::uint32 cMeterHigh = 0xFFDD2222u;
    static constexpr juce::uint32 cMeterLowDim = 0xFF091A09u;
    static constexpr juce::uint32 cMeterMidDim = 0xFF1E1700u;
    static constexpr juce::uint32 cMeterHighDim = 0xFF220808u;
    static constexpr juce::uint32 cOSBackground = 0xFF080E1Au;
    static constexpr juce::uint32 cOSBorder = 0xFF101C2Eu;
    static constexpr juce::uint32 cOSLabel = 0xFF3A6080u;
    static constexpr juce::uint32 cOSBtnActive = 0xFF70A8D8u;
    static constexpr juce::uint32 cOSBtnActiveBg = 0xFF0C2040u;
    static constexpr juce::uint32 cOSBtnActiveBdr = 0xFF2A5890u;
    static constexpr juce::uint32 cBypassLabel = 0xFF2E4A60u;

    MonarchLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX,
                       int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

    // Pedal-face palette (royal purple body + gold, sampled from pedal_picture.png).
    static constexpr juce::uint32 cPedalPurple = 0xFF6A0956u;   // body base
    static constexpr juce::uint32 cPedalPurpleLit = 0xFF8C1A74u; // lighter centre (lighting)
    static constexpr juce::uint32 cPedalPurpleDark = 0xFF3E0431u; // edge vignette
    static constexpr juce::uint32 cPedalGold = 0xFFC6A15Bu;     // lettering / compass
    static constexpr juce::uint32 cPedalGoldBright = 0xFFE2C681u;
    static constexpr juce::uint32 cKnobBlack = 0xFF161019u;     // knob cap base

private:
    void drawTrimHalo (juce::Graphics&, juce::Rectangle<float> bounds, float sliderPos,
                       float startAngle, float endAngle);
    void drawMainKnob (juce::Graphics&, juce::Rectangle<float> bounds, float sliderPos,
                       float startAngle, float endAngle);
    void drawBypassFootswitch (juce::Graphics&, juce::Rectangle<float> bounds, bool isDown);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonarchLookAndFeel)
};
