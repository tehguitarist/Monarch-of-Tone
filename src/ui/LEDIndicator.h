#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
 * Small status LED with a soft radial glow when on. Active = green (0xFF00DD55) with glow;
 * inactive = dim (0xFF091A09). Driven from the editor timer by reading the bypass parameter
 * (LED on = channel active). The component should be sized a little larger than the lit core
 * so the glow has room to fall off.
 */
class LEDIndicator : public juce::Component
{
public:
    LEDIndicator() = default;

    void setOn (bool shouldBeOn)
    {
        if (on != shouldBeOn)
        {
            on = shouldBeOn;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float cx = b.getCentreX(), cy = b.getCentreY();
        const float coreR = juce::jmin (b.getWidth(), b.getHeight()) * 0.32f;
        const juce::Colour core = on ? juce::Colour (0xFF00DD55u) : juce::Colour (0xFF091A09u);

        // Soft radial glow (only when lit).
        if (on)
        {
            const float glowR = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
            juce::ColourGradient glow (core.withAlpha (0.55f), cx, cy,
                                       core.withAlpha (0.0f), cx + glowR, cy, true);
            g.setGradientFill (glow);
            g.fillEllipse (cx - glowR, cy - glowR, glowR * 2.0f, glowR * 2.0f);
        }

        // Lit core, slightly domed.
        juce::ColourGradient body (core.brighter (0.35f), cx - coreR * 0.3f, cy - coreR * 0.3f,
                                   core.darker (0.45f), cx + coreR * 0.35f, cy + coreR * 0.35f, true);
        g.setGradientFill (body);
        g.fillEllipse (cx - coreR, cy - coreR, coreR * 2.0f, coreR * 2.0f);

        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.drawEllipse (cx - coreR, cy - coreR, coreR * 2.0f, coreR * 2.0f, 1.0f);
    }

private:
    bool on { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LEDIndicator)
};
