#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/MonarchChannel.h"

class MonarchAudioProcessor : public juce::AudioProcessor
{
public:
    MonarchAudioProcessor();
    ~MonarchAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

private:
    monarch::MonarchChannel channelA;
    monarch::MonarchChannel channelB;

    std::atomic<float> inputLevelL { 0.0f };
    std::atomic<float> inputLevelR { 0.0f };
    std::atomic<float> outputLevelL { 0.0f };
    std::atomic<float> outputLevelR { 0.0f };

    std::atomic<bool> bypassedA { false };
    std::atomic<bool> bypassedB { false };

    std::atomic<int> pendingClippingModeA { 1 };
    std::atomic<int> pendingClippingModeB { 1 };

    std::atomic<bool> pendingHiGainA { false };
    std::atomic<bool> pendingHiGainB { false };

    std::atomic<int> pendingOversamplingFactor { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonarchAudioProcessor)
};
