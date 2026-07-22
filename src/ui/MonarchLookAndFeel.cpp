#include "MonarchLookAndFeel.h"

#include "Assets.h"

#include <cmath>

using namespace juce;

MonarchLookAndFeel::MonarchLookAndFeel()
{
    setColour (PopupMenu::backgroundColourId, Colour (0xFF0A1628u));
    setColour (PopupMenu::textColourId, Colour (cOSBtnActive));
    setColour (PopupMenu::highlightedBackgroundColourId, Colour (cOSBtnActiveBg));
    setColour (PopupMenu::highlightedTextColourId, Colour (0xFFF5F5F5u));
}

// ============================ Halo trim knob ============================

void MonarchLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float rotaryStartAngle,
                                           float rotaryEndAngle, Slider& s)
{
    const auto bounds = Rectangle<int> (x, y, width, height).toFloat();
    const auto id = s.getComponentID();
    if (id == "trim")
        drawTrimHalo (g, bounds, sliderPos, rotaryStartAngle, rotaryEndAngle);
    else if (id == "presence")
        drawKnobImage (g, MonarchAssets::presenceKnobGraded(), bounds, sliderPos, rotaryStartAngle, rotaryEndAngle);
    else
        drawKnobImage (g, MonarchAssets::bakeliteKnobGraded(), bounds, sliderPos, rotaryStartAngle, rotaryEndAngle);
}

void MonarchLookAndFeel::drawKnobImage (Graphics& g, const Image& img, Rectangle<float> bounds,
                                        float sliderPos, float startAngle, float endAngle)
{
    if (! img.isValid())
        return;

    const float d = jmin (bounds.getWidth(), bounds.getHeight());
    const float toAngle = startAngle + sliderPos * (endAngle - startAngle);
    const float noonAngle = 0.5f * (startAngle + endAngle); // image art is drawn at "noon"

    const Rectangle<float> dest (bounds.getCentreX() - d * 0.5f, bounds.getCentreY() - d * 0.5f, d, d);

    Graphics::ScopedSaveState save (g);
    g.addTransform (AffineTransform::rotation (toAngle - noonAngle, bounds.getCentreX(), bounds.getCentreY()));
    g.drawImage (img, dest, RectanglePlacement::centred, false);
}

void MonarchLookAndFeel::drawTrimHalo (Graphics& g, Rectangle<float> bounds, float sliderPos,
                                       float startAngle, float endAngle)
{
    // All metrics are proportional to the knob diameter (1x reference: 70 px knob, 36 px cap,
    // 5 px arc, 2.5 px indicator) so the halo scales cleanly with the window.
    const float diameter = jmin (bounds.getWidth(), bounds.getHeight());
    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY();
    const float arcW = diameter * (5.0f / 70.0f);
    const float arcR = diameter * 0.5f - arcW * 0.5f - 1.0f;
    const float capR = diameter * (18.0f / 70.0f);
    const float toAngle = startAngle + sliderPos * (endAngle - startAngle);

    // Outer arc track (full 270° sweep) then the value arc on top.
    Path track;
    track.addCentredArc (cx, cy, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (Colour (cTrimArcTrack));
    g.strokePath (track, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));

    Path value;
    value.addCentredArc (cx, cy, arcR, arcR, 0.0f, startAngle, toAngle, true);
    g.setColour (Colour (cTrimArc));
    g.strokePath (value, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));

    // Cap — image art (rotated to sliderPos), sized to the same footprint as the old vector cap.
    const Image& img = MonarchAssets::trimKnob();
    if (img.isValid())
    {
        const float noonAngle = 0.5f * (startAngle + endAngle);
        const Rectangle<float> dest (cx - capR, cy - capR, capR * 2.0f, capR * 2.0f);
        Graphics::ScopedSaveState save (g);
        g.addTransform (AffineTransform::rotation (toAngle - noonAngle, cx, cy));
        g.drawImage (img, dest, RectanglePlacement::centred, false);
    }
}

// ============================ Oversampling combo box ============================

void MonarchLookAndFeel::drawComboBox (Graphics& g, int width, int height, bool isButtonDown, int,
                                       int, int, int, ComboBox&)
{
    const float corner = 4.0f;
    auto bounds = Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
    g.setColour (isButtonDown ? Colour (cOSBtnActiveBdr).withAlpha (0.4f) : Colour (cOSBtnActiveBg));
    g.fillRoundedRectangle (bounds, corner);
    g.setColour (Colour (cOSBtnActiveBdr));
    g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);

    const float arrowCX = bounds.getRight() - 8.0f, arrowCY = bounds.getCentreY();
    Path arrow;
    arrow.startNewSubPath (arrowCX - 2.5f, arrowCY - 1.5f);
    arrow.lineTo (arrowCX, arrowCY + 1.5f);
    arrow.lineTo (arrowCX + 2.5f, arrowCY - 1.5f);
    g.setColour (Colour (cOSLabel));
    g.strokePath (arrow, PathStrokeType (1.2f, PathStrokeType::curved, PathStrokeType::rounded));
}

Font MonarchLookAndFeel::getComboBoxFont (ComboBox& box)
{
    return Font (FontOptions (jmax (7.0f, (float) box.getHeight() * 0.38f), Font::bold));
}

void MonarchLookAndFeel::positionComboBoxText (ComboBox& box, Label& label)
{
    // Full-width label so centred justification truly centres the text (arrow painted on top).
    label.setBounds (0, 0, box.getWidth(), box.getHeight());
    label.setFont (getComboBoxFont (box));
}

// ============================ Bypass footswitch ============================

void MonarchLookAndFeel::drawButtonBackground (Graphics& g, Button& button, const Colour& bg,
                                               bool highlighted, bool isDown)
{
    if (button.getComponentID() == "bypass")
    {
        drawBypassFootswitch (g, button.getLocalBounds().toFloat(), isDown);
        return;
    }
    if (button.getComponentID() == "scale")
    {
        // Match the OS selector boxes' fill/outline/corner radius exactly.
        const auto bounds = button.getLocalBounds().toFloat();
        const float corner = 4.0f;
        g.setColour (isDown ? Colour (cOSBtnActiveBdr).withAlpha (0.4f) : bg);
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (Colour (cOSBtnActiveBdr));
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);
        return;
    }
    if (button.getComponentID() == "ostoggle")
    {
        // Lit-on / dim-off toggle in the OS strip. Same geometry as the "scale" button above, but
        // the fill/outline follow the TOGGLE state (not the momentary press) so the on/off state is
        // readable at a glance; off dims down to the strip background so it recedes.
        const auto bounds = button.getLocalBounds().toFloat();
        const float corner = 4.0f;
        const bool on = button.getToggleState();
        g.setColour (Colour (on ? cOSBtnActiveBg : cOSBackground));
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (Colour (on ? cOSBtnActiveBdr : cOSBorder));
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);
        return;
    }
    LookAndFeel_V4::drawButtonBackground (g, button, bg, highlighted, isDown);
}

Font MonarchLookAndFeel::getTextButtonFont (TextButton& btn, int buttonHeight)
{
    const float factor = (btn.getComponentID() == "ostoggle" || btn.getComponentID() == "scale") ? 0.38f : 0.5f;
    return Font (FontOptions (jmax (7.0f, (float) buttonHeight * factor), Font::bold));
}

void MonarchLookAndFeel::drawBypassFootswitch (Graphics& g, Rectangle<float> b, bool isButtonDown)
{
    // isButtonDown is the momentary mouse-down state (not the bypass toggle state) — the
    // down-image is a press animation only — footswitch position doesn't indicate bypass state.
    const Image img = isButtonDown ? MonarchAssets::footswitchDownGraded() : MonarchAssets::footswitchUpGraded();
    if (! img.isValid())
        return;

    const float d = jmin (b.getWidth(), b.getHeight());
    const Rectangle<float> dest (b.getCentreX() - d * 0.5f, b.getCentreY() - d * 0.5f, d, d);
    g.drawImage (img, dest, RectanglePlacement::centred, false);
}
