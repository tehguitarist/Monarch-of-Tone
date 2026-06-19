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
    ledY.setOnColour (Colour (0xFFFFC21Au)); // Yellow channel
    ledR.setOnColour (Colour (0xFFFF3300u)); // Red channel
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
}

void PedalFace::resized()
{
    const float W = (float) getWidth(), H = (float) getHeight();
    const float knobD = jmin (W * 0.16f, H * 0.20f);
    const float presD = knobD * 0.264f; // small presence trims (0.6× the previous size)
    const float labH = jmax (10.0f, H * 0.052f);

    auto place = [] (Component& c, float cx, float cy, float w, float h) {
        c.setBounds (roundToInt (cx - w * 0.5f), roundToInt (cy - h * 0.5f), roundToInt (w), roundToInt (h));
    };
    auto placeLabel = [&] (Label& l, float cx, float cyKnob, float knob, float gap = 0.70f) {
        place (l, cx, cyKnob + knob * gap, knob * 1.6f, labH);
    };

    // Top row: Volume / Drive per channel — SAME order on both channels (Volume left, Drive right).
    const float topY = H * 0.155f;
    const float volYx = W * 0.135f, driveYx = W * 0.330f;
    const float volRx = W * 0.670f, driveRx = W * 0.865f;
    place (volY, volYx, topY, knobD, knobD);
    place (driveY, driveYx, topY, knobD, knobD);
    place (volR, volRx, topY, knobD, knobD);
    place (driveR, driveRx, topY, knobD, knobD);
    placeLabel (volYL, volYx, topY, knobD);
    placeLabel (driveYL, driveYx, topY, knobD);
    placeLabel (volRL, volRx, topY, knobD);
    placeLabel (driveRL, driveRx, topY, knobD);

    // Tone knob centred between Volume and Drive, pulled UP closer to that pair.
    const float toneRowY = H * 0.37f;
    const float toneYx = (volYx + driveYx) * 0.5f;
    const float toneRx = (volRx + driveRx) * 0.5f;
    place (toneY, toneYx, toneRowY, knobD, knobD);
    place (toneR, toneRx, toneRowY, knobD, knobD);
    placeLabel (toneYL, toneYx, toneRowY, knobD);
    placeLabel (toneRL, toneRx, toneRowY, knobD);

    // Compass centre.
    const float compD = jmin (W * 0.215f, H * 0.30f);
    const float compCy = H * 0.44f;
    compassArea = Rectangle<float> (W * 0.5f - compD * 0.5f, compCy - compD * 0.5f, compD, compD).toNearestInt();

    // Small Presence trims: vertical centre = compass centre, on either side, clear of the graphic.
    const float presX = W * 0.5f - compD * 0.5f - presD * 0.85f; // just outside the compass
    place (presY, presX, compCy, presD, presD);
    place (presR, W - presX, compCy, presD, presD);
    placeLabel (presYL, presX, compCy, presD, 0.85f);
    placeLabel (presRL, W - presX, compCy, presD, 0.85f);

    // 3-way clip switches at the left/right edges (raised up).
    const float clipW = W * 0.12f, clipH = H * 0.20f;
    place (clipY, W * 0.085f, H * 0.55f, clipW, clipH);
    place (clipR, W * 0.915f, H * 0.55f, clipW, clipH);

    // LEDs, then the (large) logo, then the footswitches — all raised up.
    const float ledBox = jmin (W, H) * 0.055f;
    place (ledY, W * 0.40f, H * 0.61f, ledBox, ledBox);
    place (ledR, W * 0.60f, H * 0.61f, ledBox, ledBox);

    place (logoL, W * 0.5f, H * 0.70f, W * 0.94f, H * 0.16f);

    const float fsD = jmin (W * 0.12f, H * 0.17f);
    const float fsY = H * 0.83f;
    place (bypassY, W * 0.27f, fsY, fsD, fsD);
    place (bypassR, W * 0.73f, fsY, fsD, fsD);
    place (bypassYL, W * 0.27f, fsY + fsD * 0.60f, fsD * 1.7f, labH);
    place (bypassRL, W * 0.73f, fsY + fsD * 0.60f, fsD * 1.7f, labH);
}

void PedalFace::refresh (float sc)
{
    scale = sc;
    auto mainLabelFont = papyrus (jmax (12.0f, 16.0f * sc)); // V / D / Tone — larger
    for (auto* l : { &volYL, &driveYL, &toneYL, &volRL, &driveRL, &toneRL })
        l->setFont (mainLabelFont);
    auto presLabelFont = papyrus (jmax (8.0f, 10.0f * sc)); // Pres — small
    presYL.setFont (presLabelFont);
    presRL.setFont (presLabelFont);

    auto bypassFont = papyrus (jmax (7.0f, 8.0f * sc));
    bypassYL.setFont (bypassFont);
    bypassRL.setFont (bypassFont);

    logoL.setFont (papyrus (jmax (30.0f, 55.0f * sc))); // 2.5× larger

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
