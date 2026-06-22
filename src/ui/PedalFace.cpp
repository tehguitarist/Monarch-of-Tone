#include "PedalFace.h"

#include "Assets.h"

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
      clipR (*apvts.getParameter ("clipping_mode_red")),
      voltage (*apvts.getParameter ("supply_voltage"))
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
    presY.setComponentID ("presence");
    presR.setComponentID ("presence");

    clipR.setMirrored (true); // right channel: labels point inward, switch at the edge
    addAndMakeVisible (clipY);
    addAndMakeVisible (clipR);
    ledY.setChannel (LEDIndicator::Channel::Yellow);
    ledR.setChannel (LEDIndicator::Channel::Red);
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
    logoL.setColour (Label::textColourId, Colour (MonarchLookAndFeel::cPedalGoldBright).withAlpha (0.8f));
    addAndMakeVisible (logoL);

    addAndMakeVisible (voltage);

    updateLEDs();
}

void PedalFace::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float corner = jmin (b.getWidth(), b.getHeight()) * 0.045f;

    const Image& tex = MonarchAssets::textureGraded();
    if (tex.isValid())
    {
        // Cover/crop-fill: scale to fill b without stretching, then clip to the rounded body.
        Path body;
        body.addRoundedRectangle (b, corner);
        Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (body);

        const float scale = jmax (b.getWidth() / (float) tex.getWidth(), b.getHeight() / (float) tex.getHeight());
        const float drawW = (float) tex.getWidth() * scale, drawH = (float) tex.getHeight() * scale;
        const Rectangle<float> dest (b.getCentreX() - drawW * 0.5f, b.getCentreY() - drawH * 0.5f, drawW, drawH);
        g.drawImage (tex, dest, RectanglePlacement::centred, false);
    }
    else
    {
        // Fallback: royal-purple radial gradient, lit upper-centre → dark edges.
        ColourGradient bodyGrad (Colour (MonarchLookAndFeel::cPedalPurpleLit), b.getCentreX(), b.getHeight() * 0.30f,
                                 Colour (MonarchLookAndFeel::cPedalPurpleDark), b.getX(), b.getBottom(), true);
        bodyGrad.addColour (0.55, Colour (MonarchLookAndFeel::cPedalPurple));
        g.setGradientFill (bodyGrad);
        g.fillRoundedRectangle (b, corner);
    }

    // Enclosure edge.
    g.setColour (Colour (0xFF1C0516u));
    g.drawRoundedRectangle (b.reduced (0.6f), corner, jmax (1.2f, b.getWidth() * 0.004f));

    g.beginTransparencyLayer (0.9f); // compass 10% less opaque (round 5)
    drawCompassRose (g, compassArea.toFloat());
    g.endTransparencyLayer();
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
    const float cx0 = W * 0.5f;

    auto place = [] (Component& c, float cx, float cy, float w, float h) {
        c.setBounds (roundToInt (cx - w * 0.5f), roundToInt (cy - h * 0.5f), roundToInt (w), roundToInt (h));
    };
    auto placeLabel = [&] (Label& l, float cx, float cyKnob, float knob, float gap = 0.70f) {
        place (l, cx, cyKnob + knob * gap, knob * 1.6f, labH);
    };

    // Top row: Volume / Drive per channel, spread wider with a bigger mutual gap (2026-06-22
    // round 2). Tone sits below, between the clip switch's OD label and the Presence knob, and
    // is nudged down a little from its previous spot.
    const float topY = H * 0.155f;
    const float toneRowY = H * 0.40f;
    const float halfGapVD = W * 0.095f; // round 5: a bit more gap between V and D
    const float wideStep = knobD * 0.25f; // round 4: nudge the whole V/D/Tone group out again
    const float toneYx = W * 0.26f - wideStep, toneRx = W * 0.74f + wideStep;
    const float volYx = toneYx - halfGapVD, driveYx = toneYx + halfGapVD;
    const float volRx = toneRx - halfGapVD, driveRx = toneRx + halfGapVD; // V then D, left-to-right (match Yellow)
    place (volY, volYx, topY, knobD, knobD);
    place (driveY, driveYx, topY, knobD, knobD);
    place (volR, volRx, topY, knobD, knobD);
    place (driveR, driveRx, topY, knobD, knobD);
    placeLabel (volYL, volYx, topY, knobD);
    placeLabel (driveYL, driveYx, topY, knobD);
    placeLabel (volRL, volRx, topY, knobD);
    placeLabel (driveRL, driveRx, topY, knobD);

    place (toneY, toneYx, toneRowY, knobD, knobD);
    place (toneR, toneRx, toneRowY, knobD, knobD);
    placeLabel (toneYL, toneYx, toneRowY, knobD);
    placeLabel (toneRL, toneRx, toneRowY, knobD);

    // Compass centre — footprint used for layout anchoring (Presence position) stays at the
    // ORIGINAL size; the DRAWN compass is 20% larger (compD) and is allowed to overlap
    // neighbours, per the earlier tweak request — painted in PedalFace::paint(), before any
    // child component paints, so it is already behind everything except the texture.
    const float compDOrig = jmin (W * 0.215f, H * 0.30f);
    const float compD = compDOrig * 1.2f * 1.1f * 0.95f * 0.93f; // round 7: another -7% drawn size
    const float compCy = H * 0.44f;
    compassArea = Rectangle<float> (cx0 - compD * 0.5f, compCy - compD * 0.5f, compD, compD).toNearestInt();

    // Presence trims: moved a little further out from the (original) compass footprint, then
    // back in a touch (round 5).
    const float presX = cx0 - compDOrig * 0.5f - presD * 1.1f;
    place (presY, presX, compCy, presD, presD);
    place (presR, W - presX, compCy, presD, presD);
    placeLabel (presYL, presX, compCy, presD, 0.85f);
    placeLabel (presRL, W - presX, compCy, presD, 0.85f);

    // Footswitches: 15% smaller, scaled about their existing centre (position unchanged).
    const float fsD = jmin (W * 0.12f, H * 0.17f) * 0.85f;
    const float fsY = H * 0.875f;
    const float fsYx = W * 0.27f, fsRx = W * 0.73f;
    place (bypassY, fsYx, fsY, fsD, fsD);
    place (bypassR, fsRx, fsY, fsD, fsD);
    place (bypassYL, fsYx, fsY + fsD * 0.68f, fsD * 1.7f, labH); // round 5: a touch lower
    place (bypassRL, fsRx, fsY + fsD * 0.68f, fsD * 1.7f, labH);

    // 3-way clip switches: the 2.5x size (2x + round-2's 1.25x) came out far too large — scaled
    // back down to 40% of that (round 3), which nets out to the original 1x size; then +5% (round 4).
    // Computed before the LEDs/logo below so the logo band can clear the switch's footprint.
    const float clipW = W * 0.12f * 2.0f * 1.25f * 0.4f * 1.05f, clipH = H * 0.20f * 2.0f * 1.25f * 0.4f * 1.05f;
    const float clipYx = clipW * 0.5f + W * 0.032f, clipRx = W - clipYx; // round 5: a little further in
    const float clipCy = H * 0.52f;
    place (clipY, clipYx, clipCy, clipW, clipH);
    place (clipR, clipRx, clipCy, clipW, clipH);
    const float clipBottomEdge = clipCy + clipH * 0.5f;

    // LEDs: vertical position unchanged; horizontal position now equidistant between the
    // (drawn, enlarged) compass edge and the nearer edge of the respective footswitch.
    const float ledBox = jmin (W, H) * 0.075f;
    const float ledCy = H * 0.62f;
    const float compLeftEdge = cx0 - compD * 0.5f, compRightEdge = cx0 + compD * 0.5f;
    const float fsYRightEdge = fsYx + fsD * 0.5f, fsRLeftEdge = fsRx - fsD * 0.5f;
    const float ledYx = (compLeftEdge + fsYRightEdge) * 0.5f;
    const float ledRx = (compRightEdge + fsRLeftEdge) * 0.5f;
    place (ledY, ledYx, ledCy, ledBox, ledBox);
    place (ledR, ledRx, ledCy, ledBox, ledBox);

    // Logo: 90% opacity is set once in the constructor. Band starts below whichever is lower —
    // the LED row or the (now much taller) clip switches' DIST labels — so the enlarged switches
    // never collide with the logo text; the label itself is 10% smaller (font size in refresh()).
    const float fsTopEdge = fsY - fsD * 0.5f;
    const float ledBottomEdge = ledCy + ledBox * 0.5f;
    const float bandTop = jmax (ledBottomEdge, clipBottomEdge);
    // Centred at the midpoint of (bandTop, fsTopEdge) so the gap above the letters equals the gap
    // below them; nudged down a touch further (round 5) per visual check.
    const float logoCy = bandTop + (fsTopEdge - bandTop) * 0.5f + (fsTopEdge - bandTop) * 0.08f;
    const float logoH = jmin (H * 0.12f, (fsTopEdge - bandTop) * 0.85f);
    place (logoL, cx0, logoCy, W * 0.94f, logoH);

    // Supply-voltage selector "(+) 9V (-)" — top centre, in the empty band above the top knob row.
    // Box 30% larger than the original (the tight-grouped text centres within it).
    place (voltage, cx0, H * 0.065f, W * 0.34f * 1.3f, jmax (12.0f, H * 0.07f) * 1.3f);
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

    logoL.setFont (papyrus (jmax (27.0f, 49.5f * sc))); // 2.5× larger, then 10% smaller (round 2)

    clipY.setLabelScale (sc);
    clipR.setLabelScale (sc);
    voltage.setFontScale (sc);
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
