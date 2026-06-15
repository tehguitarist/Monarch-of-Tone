#include "PluginEditor.h"

MonarchAudioProcessorEditor::MonarchAudioProcessorEditor (MonarchAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    juce::ignoreUnused (audioProcessor);
    setSize (700, 480);
}

MonarchAudioProcessorEditor::~MonarchAudioProcessorEditor() = default;

void MonarchAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour { 0xFF1A1A1A });
}

void MonarchAudioProcessorEditor::resized()
{
}
