#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/MonarchLookAndFeel.h"
#include "ui/PedalFace.h"
#include "ui/VUMeter.h"

class MonarchAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MonarchAudioProcessorEditor (MonarchAudioProcessor&);
    ~MonarchAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshFonts (float sc);
    void showScaleMenu();

    // Base (1x) window size. The pedal face is the placeholder centre (~1.7:1); the side
    // panels (trim + VU) and the oversampling strip are the "peripheral" shared-look elements.
    static constexpr int kBaseW = 694;
    static constexpr int kBaseH = 354;

    MonarchAudioProcessor& audioProcessor;
    MonarchLookAndFeel lnf;
    juce::ApplicationProperties appProps;
    float currentScale { 1.0f };

    // ---- Side panels: Input (left) / Output (right) ----
    juce::Label inputSectionLabel, outputSectionLabel;
    juce::Slider inputTrim, outputTrim;
    juce::Label inputTrimSub, outputTrimSub;
    VUMeter inputVU, outputVU;
    std::unique_ptr<juce::SliderParameterAttachment> inputTrimAttach, outputTrimAttach;
    float vuInDecay { 0.0f }, vuOutDecay { 0.0f };

    // ---- Oversampling strip ----
    juce::Label osLabel, osLiveLabel, osRenderLabel, osSizeLabel;
    juce::ComboBox osRealtimeBox, osRenderBox;
    juce::TextButton scaleBtn;
    std::unique_ptr<juce::ComboBoxParameterAttachment> osRealtimeAttach, osRenderAttach;

    std::unique_ptr<PedalFace> pedalFace; // the unique purple/gold centre
    juce::Rectangle<int> pedalFaceArea;
    juce::Rectangle<int> osStripArea; // OS strip background rect (laid out in resized, painted in paint)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonarchAudioProcessorEditor)
};
