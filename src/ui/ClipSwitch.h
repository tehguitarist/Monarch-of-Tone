#pragma once

#include <cmath>

#include <juce_audio_processors/juce_audio_processors.h>

#include "MonarchLookAndFeel.h"

/**
 * 3-position clipping switch (Boost / Overdrive / Distortion) bound to a `clipping_mode_*`
 * AudioParameterChoice. A vertical toggle (top = Boost, middle = Overdrive, bottom = Distortion)
 * with gold mode labels beside it; click a third to select. Matches the pedal-face look.
 */
class ClipSwitch : public juce::Component
{
public:
    explicit ClipSwitch (juce::RangedAudioParameter& param)
        : attachment (param, [this] (float v) {
              index = juce::jlimit (0, 2, (int) std::lround (v));
              repaint();
          })
    {
        attachment.sendInitialUpdate();
    }

    void setLabelScale (float sc)
    {
        labelFontPx = juce::jmax (6.0f, 7.0f * sc);
        repaint();
    }

    /** Mirror = switch on the right, labels on the left (so labels point inward on the right channel). */
    void setMirrored (bool m)
    {
        mirrored = m;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        // Switch body is ~42% of the width; the rest holds the three stacked mode labels.
        const float fullW = b.getWidth();
        auto sw = (mirrored ? b.removeFromRight (fullW * 0.42f) : b.removeFromLeft (fullW * 0.42f))
                      .reduced (fullW * 0.04f, 0.0f);
        auto labels = b;
        const auto labelJustify = mirrored ? juce::Justification::centredRight : juce::Justification::centredLeft;

        // Switch housing (dark rounded pill).
        const float corner = sw.getWidth() * 0.5f;
        g.setColour (juce::Colour (0xFF0A0A0Cu));
        g.fillRoundedRectangle (sw, corner);
        g.setColour (juce::Colour (0xFF000000u));
        g.drawRoundedRectangle (sw.reduced (0.5f), corner, 1.0f);

        // Three detent centres (top→bottom = 0,1,2) and the metallic thumb at the active one.
        const float thumbR = sw.getWidth() * 0.40f;
        for (int i = 0; i < 3; ++i)
        {
            const float cy = sw.getY() + sw.getHeight() * (0.18f + 0.32f * (float) i);
            const float cx = sw.getCentreX();
            if (i == index)
            {
                juce::ColourGradient grad (juce::Colour (0xFFE6E6EAu), cx - thumbR * 0.3f, cy - thumbR * 0.4f,
                                           juce::Colour (0xFF6E7078u), cx + thumbR * 0.3f, cy + thumbR * 0.4f, true);
                g.setGradientFill (grad);
                g.fillEllipse (cx - thumbR, cy - thumbR, thumbR * 2.0f, thumbR * 2.0f);
                g.setColour (juce::Colour (MonarchLookAndFeel::cPedalGold));
                g.drawEllipse (cx - thumbR, cy - thumbR, thumbR * 2.0f, thumbR * 2.0f, juce::jmax (1.0f, thumbR * 0.22f));
            }
            else
            {
                g.setColour (juce::Colour (0xFF1C1C20u));
                g.fillEllipse (cx - thumbR * 0.45f, cy - thumbR * 0.45f, thumbR * 0.9f, thumbR * 0.9f);
            }
        }

        // Mode labels (active = bright gold, others dimmed).
        static const char* names[] = { "BOOST", "OD", "DIST" };
        g.setFont (juce::Font (juce::FontOptions (labelFontPx, juce::Font::bold)));
        for (int i = 0; i < 3; ++i)
        {
            const float rowY = labels.getY() + labels.getHeight() * (0.18f + 0.32f * (float) i) - labelFontPx;
            const auto row = juce::Rectangle<float> (labels.getX(), rowY, labels.getWidth(), labelFontPx * 2.0f);
            g.setColour (i == index ? juce::Colour (MonarchLookAndFeel::cPedalGoldBright)
                                    : juce::Colour (MonarchLookAndFeel::cPedalGold).withAlpha (0.4f));
            g.drawText (names[i], row, labelJustify);
        }
    }

    // Click to select a position, or drag the thumb between positions.
    void mouseDown (const juce::MouseEvent& e) override
    {
        attachment.beginGesture();
        setFromY (e.position.y);
    }
    void mouseDrag (const juce::MouseEvent& e) override { setFromY (e.position.y); }
    void mouseUp (const juce::MouseEvent&) override { attachment.endGesture(); }

private:
    void setFromY (float y)
    {
        const int idx = juce::jlimit (0, 2, (int) (y / (float) juce::jmax (1, getHeight()) * 3.0f));
        if (idx != index)
            attachment.setValueAsPartOfGesture ((float) idx);
    }

    juce::ParameterAttachment attachment;
    int index { 1 };
    float labelFontPx { 7.0f };
    bool mirrored { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipSwitch)
};
