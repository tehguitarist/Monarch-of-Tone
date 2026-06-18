#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "MonarchLookAndFeel.h"

/**
 * 22-segment vertical VU bar (top = loudest). Zones from the top: 3 red, 5 yellow, 14 green.
 * Calibration: 0 VU = −12 dBu nominal (≈ −11.2 dBFS peak at the 1 V/FS calibration) → ~60% lit.
 * Ballistics (peak-hold off, ~300 ms release) are driven by the editor's timer via setLevel().
 */
class VUMeter : public juce::Component
{
public:
    VUMeter() = default;

    /** level: linear peak (post-trim, ~circuit volts ≈ FS). Mapped to a 0..1 lit fraction. */
    void setLevel (float level)
    {
        const float db = juce::Decibels::gainToDecibels (level, floorDB);
        const float frac = juce::jlimit (0.0f, 1.0f, (db - floorDB) / (ceilDB - floorDB));
        if (std::abs (frac - litFraction) > 1.0e-4f)
        {
            litFraction = frac;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        const float gap = juce::jmax (1.0f, b.getHeight() * 0.007f);
        const float segH = (b.getHeight() - gap * (float) (kSegments - 1)) / (float) kSegments;
        const int lit = juce::roundToInt (litFraction * (float) kSegments);

        for (int i = 0; i < kSegments; ++i) // i = 0 at the bottom
        {
            const bool isLit = i < lit;
            const float top = (float) (kSegments - 1 - i) * (segH + gap);
            g.setColour (juce::Colour (zoneColour (i, isLit)));
            g.fillRect (juce::Rectangle<float> (0.0f, top, b.getWidth(), segH));
        }
    }

private:
    static constexpr int kSegments = 22;
    // dB window chosen so nominal −12 dBu (≈ −11.2 dBFS peak) lands at ~60% lit.
    static constexpr float floorDB = -33.0f;
    static constexpr float ceilDB = 3.0f;

    static juce::uint32 zoneColour (int i, bool lit)
    {
        if (i >= 19) // top 3 segments
            return lit ? MonarchLookAndFeel::cMeterHigh : MonarchLookAndFeel::cMeterHighDim;
        if (i >= 14) // next 5 segments
            return lit ? MonarchLookAndFeel::cMeterMid : MonarchLookAndFeel::cMeterMidDim;
        return lit ? MonarchLookAndFeel::cMeterLow : MonarchLookAndFeel::cMeterLowDim; // bottom 14
    }

    float litFraction { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VUMeter)
};
