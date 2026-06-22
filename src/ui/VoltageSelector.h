#pragma once

#include <cmath>

#include <juce_audio_processors/juce_audio_processors.h>

#include "MonarchLookAndFeel.h"

/**
 * Supply-voltage selector — a small "(+)  9V  (-)" label that simulates running the pedal at a
 * higher supply voltage (9 → 12 → 18 V). Bound to the `supply_voltage` AudioParameterChoice.
 *
 * Press (+) to step UP to the next voltage, (-) to step DOWN. An arrow is drawn BRIGHT when a
 * step in that direction exists and DIM when it doesn't (so at 9 V only (+) is live, at 18 V only
 * (-), at 12 V both). Gold Papyrus lettering to match the pedal face it sits on.
 */
class VoltageSelector : public juce::Component
{
public:
    explicit VoltageSelector (juce::RangedAudioParameter& param)
        : attachment (param, [this] (float v) {
              index = juce::jlimit (0, 2, (int) std::lround (v));
              repaint();
          })
    {
        attachment.sendInitialUpdate();
    }

    void setFontScale (float sc) { fontPx = juce::jmax (10.0f, 14.3f * sc); repaint(); } // 30% larger

    void paint (juce::Graphics& g) override
    {
        const auto L = computeLayout();
        g.setFont (juce::Font (juce::FontOptions ("Papyrus", fontPx, juce::Font::bold)));

        const auto bright = juce::Colour (MonarchLookAndFeel::cPedalGoldBright);
        const auto dim = juce::Colour (MonarchLookAndFeel::cPedalGold).withAlpha (0.30f);

        g.setColour (index < 2 ? bright : dim); // (+) bright when a higher voltage exists
        g.drawText ("(+)", L.plus, juce::Justification::centred);

        g.setColour (juce::Colour (MonarchLookAndFeel::cPedalGold));
        g.drawText (kLabels[index], L.value, juce::Justification::centred);

        g.setColour (index > 0 ? bright : dim); // (-) bright when a lower voltage exists
        g.drawText ("(-)", L.minus, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto L = computeLayout();
        int target = index;
        if (L.plus.contains (e.position) && index < 2)
            target = index + 1; // (+) up
        else if (L.minus.contains (e.position) && index > 0)
            target = index - 1; // (-) down

        if (target != index)
            attachment.setValueAsCompleteGesture ((float) target);
    }

private:
    static constexpr const char* kLabels[] = { "9V", "12V", "18V" };

    // Lay "(+) <value> (-)" out as a tight, horizontally-centred group (the arrows sit right next
    // to the voltage label rather than at the edges). Same layout drives paint and hit-testing.
    struct Layout { juce::Rectangle<float> plus, value, minus; };
    Layout computeLayout() const
    {
        const juce::Font f (juce::FontOptions ("Papyrus", fontPx, juce::Font::bold));
        const float gap = fontPx * 0.28f; // small gap → arrows hug the label
        const float wPlus = juce::GlyphArrangement::getStringWidth (f, "(+)");
        const float wMinus = juce::GlyphArrangement::getStringWidth (f, "(-)");
        const float wVal = juce::GlyphArrangement::getStringWidth (f, kLabels[index]);
        const float total = wPlus + gap + wVal + gap + wMinus;
        const float h = (float) getHeight();
        float x = ((float) getWidth() - total) * 0.5f;

        Layout L;
        L.plus = { x, 0.0f, wPlus, h };
        x += wPlus + gap;
        L.value = { x, 0.0f, wVal, h };
        x += wVal + gap;
        L.minus = { x, 0.0f, wMinus, h };
        return L;
    }

    juce::ParameterAttachment attachment;
    int index { 0 };
    float fontPx { 14.3f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoltageSelector)
};
