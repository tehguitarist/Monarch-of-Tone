#include "MonarchLookAndFeel.h"

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
    if (s.getComponentID() == "trim")
        drawTrimHalo (g, bounds, sliderPos, rotaryStartAngle, rotaryEndAngle);
    else
        drawMainKnob (g, bounds, sliderPos, rotaryStartAngle, rotaryEndAngle); // black knurled (pedal face)
}

void MonarchLookAndFeel::drawMainKnob (Graphics& g, Rectangle<float> bounds, float sliderPos,
                                       float startAngle, float endAngle)
{
    const float d = jmin (bounds.getWidth(), bounds.getHeight());
    const float cx = bounds.getCentreX(), cy = bounds.getCentreY();
    const float r = d * 0.5f * 0.92f; // knurled black knob (ref: pedal photo)
    const float toAngle = startAngle + sliderPos * (endAngle - startAngle);

    // Drop shadow.
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillEllipse (cx - r + 1.0f, cy - r + 2.0f, r * 2.0f, r * 2.0f);

    // Knurled rim: short radial ticks just outside the cap, alternating light/dark for grip.
    const int ticks = 36;
    for (int i = 0; i < ticks; ++i)
    {
        const float a = (float) i / (float) ticks * MathConstants<float>::twoPi;
        const float s0 = std::sin (a), c0 = std::cos (a);
        g.setColour ((i % 2 == 0) ? Colour (0xFF2A2A30u) : Colour (0xFF050507u));
        g.drawLine (cx + r * 0.86f * s0, cy - r * 0.86f * c0, cx + r * s0, cy - r * c0,
                    jmax (1.0f, d * 0.02f));
    }

    // Cap body — black radial gradient, lit upper-left.
    ColourGradient body (Colour (0xFF3C3C42u), cx - r * 0.4f, cy - r * 0.5f,
                         Colour (0xFF09090Bu), cx + r * 0.45f, cy + r * 0.6f, true);
    body.addColour (0.6, Colour (cKnobBlack));
    g.setGradientFill (body);
    g.fillEllipse (cx - r * 0.86f, cy - r * 0.86f, r * 1.72f, r * 1.72f);

    // Rim ring + a soft top sheen.
    g.setColour (Colours::black);
    g.drawEllipse (cx - r * 0.86f, cy - r * 0.86f, r * 1.72f, r * 1.72f, jmax (1.0f, d * 0.015f));
    g.setColour (Colours::white.withAlpha (0.10f));
    g.drawEllipse (cx - r * 0.7f, cy - r * 0.78f, r * 1.4f, r * 1.0f, jmax (1.0f, d * 0.01f));

    // White indicator line.
    const float sinA = std::sin (toAngle), cosA = std::cos (toAngle);
    const Point<float> p1 (cx + 0.12f * r * sinA, cy - 0.12f * r * cosA);
    const Point<float> p2 (cx + 0.80f * r * sinA, cy - 0.80f * r * cosA);
    g.setColour (Colour (0xFFF2F2F2u));
    g.drawLine (Line<float> (p1, p2), jmax (1.5f, d * 0.05f));
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
    const float indW = jmax (1.0f, diameter * (2.5f / 70.0f));
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

    // Cap — radial gradient highlight → mid → shadow, lit from upper-left.
    ColourGradient cap (Colour (cKnobHighlight), cx - capR * 0.4f, cy - capR * 0.45f,
                        Colour (cKnobShadow), cx + capR * 0.5f, cy + capR * 0.55f, true);
    cap.addColour (0.55, Colour (cKnobMid));
    g.setGradientFill (cap);
    g.fillEllipse (cx - capR, cy - capR, capR * 2.0f, capR * 2.0f);

    // Indicator line (angle measured clockwise from 12 o'clock).
    const float sinA = std::sin (toAngle), cosA = std::cos (toAngle);
    const Point<float> p1 (cx + 0.30f * capR * sinA, cy - 0.30f * capR * cosA);
    const Point<float> p2 (cx + 0.92f * capR * sinA, cy - 0.92f * capR * cosA);
    g.setColour (Colour (cKnobIndicator));
    g.drawLine (Line<float> (p1, p2), indW);
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
    LookAndFeel_V4::drawButtonBackground (g, button, bg, highlighted, isDown);
}

Font MonarchLookAndFeel::getTextButtonFont (TextButton&, int buttonHeight)
{
    return Font (FontOptions (jmax (7.0f, (float) buttonHeight * 0.5f), Font::bold));
}

void MonarchLookAndFeel::drawBypassFootswitch (Graphics& g, Rectangle<float> b, bool isButtonDown)
{
    const float cx = b.getCentreX(), cy = b.getCentreY();
    const float nutR = jmin (b.getWidth(), b.getHeight()) * 0.5f - 1.0f;
    const float domeR = nutR * 0.60f;

    auto makeOctagon = [&] (float radius) -> Path {
        Path p;
        for (int i = 0; i < 8; ++i)
        {
            const float a = (float) i / 8.0f * MathConstants<float>::twoPi + MathConstants<float>::pi / 8.0f;
            const float px = cx + radius * std::cos (a);
            const float py = cy + radius * std::sin (a);
            if (i == 0)
                p.startNewSubPath (px, py);
            else
                p.lineTo (px, py);
        }
        p.closeSubPath();
        return p;
    };

    // ---- Layer 1: octagonal nut ----
    auto shadow = makeOctagon (nutR);
    shadow.applyTransform (AffineTransform::translation (1.5f, 3.0f));
    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillPath (shadow);

    ColourGradient nutGrad (Colour (0xFF8A8E94u), cx - nutR * 0.4f, cy - nutR * 0.5f,
                            Colour (0xFF2E3238u), cx + nutR * 0.4f, cy + nutR * 0.5f, false);
    nutGrad.addColour (0.30, Colour (0xFF606468u));
    nutGrad.addColour (0.65, Colour (0xFF404448u));
    g.setGradientFill (nutGrad);
    g.fillPath (makeOctagon (nutR));

    g.setColour (Colour (0xFF1A1C1Eu));
    g.strokePath (makeOctagon (nutR), PathStrokeType (1.0f));

    {
        auto facet = makeOctagon (nutR);
        g.saveState();
        g.reduceClipRegion (facet);
        ColourGradient sheen (Colour (0x30FFFFFFu), cx - nutR * 0.3f, cy - nutR * 0.55f,
                              Colours::transparentWhite, cx, cy - nutR * 0.1f, false);
        g.setGradientFill (sheen);
        g.fillPath (facet);
        g.restoreState();
    }

    // ---- Layer 2: recessed socket ring ----
    g.setColour (Colour (0xFF101214u));
    g.fillEllipse (cx - domeR - 4.0f, cy - domeR - 4.0f, (domeR + 4.0f) * 2.0f, (domeR + 4.0f) * 2.0f);

    // ---- Layer 3: circular rubber dome (the switch) ----
    const float pressOff = isButtonDown ? 1.5f : 0.0f;

    g.setColour (Colours::black.withAlpha (0.50f));
    g.fillEllipse (cx - domeR + 1.0f, cy - domeR + 2.5f + pressOff, domeR * 2.0f, domeR * 2.0f);

    ColourGradient domeGrad (Colour (0xFFDCE0E4u), cx - domeR * 0.28f, cy - domeR * 0.35f - pressOff,
                             Colour (0xFF7A8490u), cx + domeR * 0.25f, cy + domeR * 0.35f + pressOff, true);
    domeGrad.addColour (0.45, Colour (0xFFAEB8C0u));
    g.setGradientFill (domeGrad);
    g.fillEllipse (cx - domeR, cy - domeR + pressOff, domeR * 2.0f, domeR * 2.0f);

    g.setColour (Colour (0xFF303438u));
    g.drawEllipse (cx - domeR + 0.5f, cy - domeR + pressOff + 0.5f, domeR * 2.0f - 1.0f, domeR * 2.0f - 1.0f, 1.0f);

    if (! isButtonDown)
    {
        g.setColour (Colours::white.withAlpha (0.55f));
        g.fillEllipse (cx - domeR * 0.50f, cy - domeR * 0.52f, domeR * 0.52f, domeR * 0.28f);
    }
}
