#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

class MonarchAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit MonarchAudioProcessorEditor (MonarchAudioProcessor&);
    ~MonarchAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    MonarchAudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonarchAudioProcessorEditor)
};
