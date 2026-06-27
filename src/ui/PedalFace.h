#pragma once

#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "ClipSwitch.h"
#include "LEDIndicator.h"
#include "MonarchLookAndFeel.h"
#include "VoltageSelector.h"

/**
 * The unique pedal face — royal-purple body with gold Papyrus lettering and a gold compass rose
 * (ref: pedal_picture.png). Hosts per channel: Volume / Drive / Tone knobs (black knurled), a
 * small Presence trim flanking the compass, a 3-way clip switch at the edge, an LED, and a
 * bypass footswitch. Yellow (left) and Red (right) are unchanged in physical position, but
 * signal flow is Red → Yellow (Red is first in the real pedal's chain) — labelled A (Red) and
 * B (Yellow) on the outside of each LED. Takes the APVTS for parameter binding.
 */
class PedalFace : public juce::Component
{
public:
    explicit PedalFace (juce::AudioProcessorValueTreeState& apvts);

    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh (float scale);  // rescale all fonts/labels (called from the editor's resized)
    void updateLEDs();           // read bypass params → LED on/off (called from the editor timer)

private:
    void drawCompassRose (juce::Graphics&, juce::Rectangle<float> area) const;

    juce::AudioProcessorValueTreeState& state;
    float scale { 1.0f };

    // Per-channel knobs (Yellow left, Red right): Volume, Drive, Tone, Presence.
    juce::Slider volY, driveY, toneY, presY;
    juce::Slider volR, driveR, toneR, presR;
    juce::Label volYL, driveYL, toneYL, presYL;
    juce::Label volRL, driveRL, toneRL, presRL;

    ClipSwitch clipY, clipR;
    LEDIndicator ledY, ledR;
    juce::Label ledBadgeY, ledBadgeR; // "B" / "A" outside the LEDs — Red is A (first in signal), Yellow is B (second)
    juce::TextButton bypassY, bypassR;
    juce::Label bypassYL, bypassRL;
    juce::Label logoL;
    VoltageSelector voltage; // supply-voltage sim "(+) 9V (-)" at the top centre

    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttachments;
    std::unique_ptr<juce::ButtonParameterAttachment> bypassYA, bypassRA;

    juce::Rectangle<int> compassArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalFace)
};
