#pragma once

#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/MonarchLookAndFeel.h"
#include "ui/PedalFace.h"
#include "ui/VUMeter.h"

// Subclass so textWasEdited() — which fires AFTER hideEditor has copied the raw
// user text into the label — is the hook for parsing, clamping, and applying the
// typed value through the APVTS parameter (never the slider directly).
struct EditableTrimLabel : public juce::Label
{
    void textWasEdited() override
    {
        juce::Label::textWasEdited();
        if (onTrimEdit)
            onTrimEdit();
    }
    std::function<void()> onTrimEdit;
};

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

    // Applies the equal-and-opposite CHANGE to the other trim, preserving the pair's existing
    // offset (delta-linked, so enabling the lock never snaps a knob). No-op when the lock is off.
    // `trimLinkBusy` breaks the A→B→A feedback loop the two parameter attachments would otherwise
    // bounce through.
    void mirrorTrim (bool sourceIsInput);
    bool trimLinkBusy { false };
    double lastInputTrim { 0.0 };
    double lastOutputTrim { 0.0 };

    // Base (1x) window size. The pedal face is the centre; the side panels (trim + VU) and the
    // oversampling strip are the "peripheral" shared-look elements. kBaseW dropped 694→592 so the
    // pedal face is 20% narrower (510→408) with the fixed-width side panels unchanged; then
    // 592→612 (2026-06-22 round 2) so the pedal face is 5% wider (408→428) — side panels/margins
    // are fixed-width in px, so all the extra window width goes to the pedal face.
    static constexpr int kBaseW = 612;
    static constexpr int kBaseH = 354;

    // Trim knob range, ± dB. Must match the input_trim/output_trim NormalisableRange in
    // MonarchAudioProcessor::createParameterLayout().
    static constexpr double kTrimRange = 18.0;

    MonarchAudioProcessor& audioProcessor;
    MonarchLookAndFeel lnf;
    juce::ApplicationProperties appProps;
    float currentScale { 1.0f };

    // ---- Side panels: Input (left) / Output (right) ----
    juce::Label inputSectionLabel, outputSectionLabel;
    juce::Slider inputTrim, outputTrim;
    juce::Label inputTrimSub, outputTrimSub;
    EditableTrimLabel inputTrimValue, outputTrimValue; // dB readout, double-click to edit
    VUMeter inputVU, outputVU;
    std::unique_ptr<juce::SliderParameterAttachment> inputTrimAttach, outputTrimAttach;
    float vuInDecay { 0.0f }, vuOutDecay { 0.0f };

    // ---- Oversampling strip ----
    juce::Label osLabel, osLiveLabel, osRenderLabel, osSizeLabel, osVersionLabel, trimLockLabel;
    juce::ComboBox osRealtimeBox, osRenderBox;
    juce::TextButton scaleBtn;
    juce::TextButton trimLockButton { "LOCK" };
    std::unique_ptr<juce::ComboBoxParameterAttachment> osRealtimeAttach, osRenderAttach;
    std::unique_ptr<juce::ButtonParameterAttachment> trimLockAttach;

    std::unique_ptr<PedalFace> pedalFace; // the unique purple/gold centre
    juce::Rectangle<int> pedalFaceArea;
    juce::Rectangle<int> osStripArea; // OS strip background rect (laid out in resized, painted in paint)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonarchAudioProcessorEditor)
};
