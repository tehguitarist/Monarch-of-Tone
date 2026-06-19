#include "PedalFace.h"

#include <cmath>

using namespace juce;

namespace
{
const float kPi = MathConstants<float>::pi;

Font papyrus (float px, bool bold = false)
{
    return Font (FontOptions ("Papyrus", px, bold ? Font::bold : Font::plain));
}
} // namespace

PedalFace::PedalFace (AudioProcessorValueTreeState& apvts)
    : state (apvts),
      clipY (*apvts.getParameter ("clipping_mode_yellow")),
      clipR (*apvts.getParameter ("clipping_mode_red"))
{
    auto setupKnob = [this] (Slider& s, Label& lab, const String& text, const char* paramId) {
        s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters (kPi * 1.25f, kPi * 2.75f, true);
        s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible (s);
        sliderAttachments.push_back (std::make_unique<SliderParameterAttachment> (*state.getParameter (paramId), s));
        lab.setText (text, dontSendNotification);
        lab.setJustificationType (Justification::centred);
        lab.setColour (Label::textColourId, Colour (MonarchLookAndFeel::cPedalGold));
        addAndMakeVisible (lab);
    };

    setupKnob (volY, volYL, "V", "volume_yellow");
    setupKnob (driveY, driveYL, "D", "drive_yellow");
    setupKnob (toneY, toneYL, "Tone", "tone_yellow");
    setupKnob (presY, presYL, "Pres", "presence_yellow");
    setupKnob (volR, volRL, "V", "volume_red");
    setupKnob (driveR, driveRL, "D", "drive_red");
    setupKnob (toneR, toneRL, "Tone", "tone_red");
    setupKnob (presR, presRL, "Pres", "presence_red");

    clipR.setMirrored (true); // right channel: labels point inward, switch at the edge
    addAndMakeVisible (clipY);
    addAndMakeVisible (clipR);
    addAndMakeVisible (ledY);
    addAndMakeVisible (ledR);

    auto setupBypass = [this] (TextButton& b, Label& lab, const char* paramId,
                               std::unique_ptr<ButtonParameterAttachment>& attach) {
        b.setComponentID ("bypass");
        b.setClickingTogglesState (true);
        addAndMakeVisible (b);
        attach = std::make_unique<ButtonParameterAttachment> (*state.getParameter (paramId), b);
        lab.setText ("BYPASS", dontSendNotification);
        lab.setJustificationType (Justification::centred);
        lab.setColour (Label::textColourId, Colour (MonarchLookAndFeel::cPedalGold));
        addAndMakeVisible (lab);
    };
    setupBypass (bypassY, bypassYL, "bypass_yellow", bypassYA);
    setupBypass (bypassR, bypassRL, "bypass_red", bypassRA);

    logoL.setText ("MONARCH OF TONE", dontSendNotification);
    logoL.setJustificationType (Justification::centred);
    logoL.setColour (Label::textColourId, Colour (MonarchLookAndFeel::cPedalGoldBright));
    addAndMakeVisible (logoL);

    updateLEDs();
}

void PedalFace::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float corner = jmin (b.getWidth(), b.getHeight()) * 0.045f;

    // Royal-purple body: radial gradient, lit upper-centre → dark edges (matches the photo).
    ColourGradient body (Colour (MonarchLookAndFeel::cPedalPurpleLit), b.getCentreX(), b.getHeight() * 0.30f,
                         Colour (MonarchLookAndFeel::cPedalPurpleDark), b.getX(), b.getBottom(), true);
    body.addColour (0.55, Colour (MonarchLookAndFeel::cPedalPurple));
    g.setGradientFill (body);
    g.fillRoundedRectangle (b, corner);

    // Enclosure edge.
    g.setColour (Colour (0xFF1C0516u));
    g.drawRoundedRectangle (b.reduced (0.6f), corner, jmax (1.2f, b.getWidth() * 0.004f));

    drawCompassRose (g, compassArea.toFloat());
}

void PedalFace::drawCompassRose (Graphics& g, Rectangle<float> area) const
{
    const float cx = area.getCentreX(), cy = area.getCentreY();
    const float R = jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const Colour gold (MonarchLookAndFeel::cPedalGold);
    const Colour goldB (MonarchLookAndFeel::cPedalGoldBright);
    const float lw = jmax (1.0f, R * 0.025f);

    g.setColour (gold.withAlpha (0.85f));
    g.drawEllipse (cx - R, cy - R, R * 2.0f, R * 2.0f, lw);
    g.drawEllipse (cx - R * 0.88f, cy - R * 0.88f, R * 1.76f, R * 1.76f, lw * 0.7f);

    // 8-point star (alternating long/short radii).
    Path star;
    for (int i = 0; i < 16; ++i)
    {
        const float ang = (float) i / 16.0f * MathConstants<float>::twoPi;
        const float rad = (i % 2 == 0) ? R * 0.82f : R * 0.30f;
        const float px = cx + rad * std::sin (ang);
        const float py = cy - rad * std::cos (ang);
        if (i == 0)
            star.startNewSubPath (px, py);
        else
            star.lineTo (px, py);
    }
    star.closeSubPath();
    g.setColour (gold.withAlpha (0.45f));
    g.fillPath (star);
    g.setColour (goldB);
    g.strokePath (star, PathStrokeType (lw));

    // Centre medallion.
    g.setColour (goldB);
    g.fillEllipse (cx - R * 0.15f, cy - R * 0.15f, R * 0.30f, R * 0.30f);
    g.setColour (Colour (MonarchLookAndFeel::cPedalPurpleDark));
    g.fillEllipse (cx - R * 0.09f, cy - R * 0.09f, R * 0.18f, R * 0.18f);

    // Cardinal letters.
    g.setColour (goldB);
    g.setFont (papyrus (R * 0.20f));
    const float lr = R * 0.16f;
    g.drawText ("N", Rectangle<float> (cx - lr, cy - R * 0.74f, lr * 2.0f, lr * 2.0f), Justification::centred);
    g.drawText ("S", Rectangle<float> (cx - lr, cy + R * 0.42f, lr * 2.0f, lr * 2.0f), Justification::centred);
    g.drawText ("E", Rectangle<float> (cx + R * 0.42f, cy - lr, lr * 2.0f, lr * 2.0f), Justification::centred);
    g.drawText ("W", Rectangle<float> (cx - R * 0.74f, cy - lr, lr * 2.0f, lr * 2.0f), Justification::centred);
}

void PedalFace::resized()
{
    const float W = (float) getWidth(), H = (float) getHeight();
    const float knobD = jmin (W * 0.155f, H * 0.21f);
    const float presD = knobD * 0.60f;
    const float labH = jmax (10.0f, H * 0.055f);

    auto place = [] (Component& c, float cx, float cy, float w, float h) {
        c.setBounds (roundToInt (cx - w * 0.5f), roundToInt (cy - h * 0.5f), roundToInt (w), roundToInt (h));
    };
    auto placeLabel = [&] (Label& l, float cx, float cyKnob, float knob) {
        place (l, cx, cyKnob + knob * 0.60f, knob * 1.4f, labH);
    };

    // Top row: Volume / Drive per channel (Yellow left, Red right; volume outer, drive inner).
    const float topY = H * 0.205f;
    place (volY, W * 0.135f, topY, knobD, knobD);
    place (driveY, W * 0.320f, topY, knobD, knobD);
    place (driveR, W * 0.680f, topY, knobD, knobD);
    place (volR, W * 0.865f, topY, knobD, knobD);
    placeLabel (volYL, W * 0.135f, topY, knobD);
    placeLabel (driveYL, W * 0.320f, topY, knobD);
    placeLabel (driveRL, W * 0.680f, topY, knobD);
    placeLabel (volRL, W * 0.865f, topY, knobD);

    // Compass centre + Tone knobs flanking it.
    const float compD = jmin (W * 0.26f, H * 0.34f);
    compassArea = Rectangle<float> (W * 0.5f - compD * 0.5f, H * 0.42f - compD * 0.5f, compD, compD).toNearestInt();
    const float toneRowY = H * 0.50f;
    place (toneY, W * 0.135f, toneRowY, knobD, knobD);
    place (toneR, W * 0.865f, toneRowY, knobD, knobD);
    placeLabel (toneYL, W * 0.135f, toneRowY, knobD);
    placeLabel (toneRL, W * 0.865f, toneRowY, knobD);

    // Small Presence trims flanking the compass.
    const float presY_y = H * 0.52f;
    place (presY, W * 0.375f, presY_y, presD, presD);
    place (presR, W * 0.625f, presY_y, presD, presD);
    placeLabel (presYL, W * 0.375f, presY_y, presD);
    placeLabel (presRL, W * 0.625f, presY_y, presD);

    // 3-way clip switches at the left/right edges, below the knobs / above the footswitches.
    const float clipW = W * 0.115f, clipH = H * 0.21f;
    place (clipY, W * 0.085f, H * 0.64f, clipW, clipH);
    place (clipR, W * 0.915f, H * 0.64f, clipW, clipH);

    // LEDs above the footswitches (flanking centre), then the logo, then the footswitches.
    const float ledBox = jmin (W, H) * 0.06f;
    place (ledY, W * 0.40f, H * 0.675f, ledBox, ledBox);
    place (ledR, W * 0.60f, H * 0.675f, ledBox, ledBox);

    place (logoL, W * 0.5f, H * 0.755f, W * 0.46f, H * 0.10f);

    const float fsD = jmin (W * 0.12f, H * 0.17f);
    const float fsY = H * 0.855f;
    place (bypassY, W * 0.25f, fsY, fsD, fsD);
    place (bypassR, W * 0.75f, fsY, fsD, fsD);
    place (bypassYL, W * 0.25f, fsY + fsD * 0.60f, fsD * 1.7f, labH);
    place (bypassRL, W * 0.75f, fsY + fsD * 0.60f, fsD * 1.7f, labH);
}

void PedalFace::refresh (float sc)
{
    scale = sc;
    auto knobLabelFont = papyrus (jmax (8.0f, 11.0f * sc));
    for (auto* l : { &volYL, &driveYL, &toneYL, &presYL, &volRL, &driveRL, &toneRL, &presRL })
        l->setFont (knobLabelFont);

    auto bypassFont = papyrus (jmax (7.0f, 8.0f * sc));
    bypassYL.setFont (bypassFont);
    bypassRL.setFont (bypassFont);

    logoL.setFont (papyrus (jmax (12.0f, 22.0f * sc)));

    clipY.setLabelScale (sc);
    clipR.setLabelScale (sc);
}

void PedalFace::updateLEDs()
{
    auto active = [this] (const char* id) {
        const auto* p = state.getRawParameterValue (id);
        return p == nullptr || p->load() < 0.5f; // bypass == true → LED off
    };
    ledY.setOn (active ("bypass_yellow"));
    ledR.setOn (active ("bypass_red"));
}
