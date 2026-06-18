#pragma once

#include <array>

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

    // Meter levels (post-trim), written by the audio thread, read by the UI timer.
    std::atomic<float> inputLevelL { 0.0f };
    std::atomic<float> inputLevelR { 0.0f };
    std::atomic<float> outputLevelL { 0.0f };
    std::atomic<float> outputLevelR { 0.0f };

private:
    // Signal calibration: host full-scale (±1.0 float) maps to ±1.0 absolute circuit volt — a
    // hot-humbucker transient level. Typical recorded guitar then sits at realistic instrument
    // volts; the diode/rail thresholds (real volts) are hit correctly. Input/output trim (±12 dB)
    // and the Drive knob position the clipping from there. See dsp.md "Signal Calibration".
    static constexpr float circuitVoltsPerFS = 1.0f;

    // One dual-mono pedal per audio channel (index 0 = L, 1 = R). Each strip is the full
    // Yellow → Red series chain; both strips share the same knob settings. Hi Gain is fixed
    // per channel via the MonarchChannel ctor flag (Yellow stock, Red Hi-Gain).
    struct ChannelStrip
    {
        monarch::MonarchChannel yellow { false };
        monarch::MonarchChannel red { true };
        juce::SmoothedValue<float> wetYellow; // bypass crossfade: 1 = active, 0 = bypassed (dry)
        juce::SmoothedValue<float> wetRed;
    };
    std::array<ChannelStrip, 2> strips;

    // Cached APVTS raw parameter pointers (atomic<float>*), fetched once in the constructor.
    struct ParamPtrs
    {
        std::atomic<float>*drive {}, *tone {}, *volume {}, *presence {}, *clip {}, *bypass {};
    };
    ParamPtrs pYellow, pRed;
    std::atomic<float>* pInputTrim {};
    std::atomic<float>* pOutputTrim {};

    void cacheParamPointers();
    void pushParams(); // read APVTS → set per-block knob values on both strips

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonarchAudioProcessor)
};
