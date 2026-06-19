#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

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

    // Meter accessors for the UI timer (message thread). ch 0 = L, 1 = R.
    float getInputLevel (int ch) const noexcept { return (ch == 0 ? inputLevelL : inputLevelR).load(); }
    float getOutputLevel (int ch) const noexcept { return (ch == 0 ? outputLevelL : outputLevelR).load(); }

    // Meter levels (post-trim), written by the audio thread, read by the UI timer.
    std::atomic<float> inputLevelL { 0.0f };
    std::atomic<float> inputLevelR { 0.0f };
    std::atomic<float> outputLevelL { 0.0f };
    std::atomic<float> outputLevelR { 0.0f };

private:
    // Signal calibration: 0 dBFS ↔ ~0.66 absolute circuit volts. Pinned to MATCH the real-pedal
    // (NAM) captures in analysis/ — the captured King of Tone hits its clean/Boost rail-clip
    // onset at a −18 dBFS 1 kHz input, i.e. an effective 0.66 V/FS (a 1 V-pk humbucker transient
    // measures −13.5 dBFS ≈ 3.31 V/FS, but the capture was reamped ~14 dB quieter; we match the
    // capture per decision 2026-06-20 and may revisit the absolute level once the curves match).
    // This sets the diode/rail thresholds at the input levels the capture used; verified against
    // the clean onset + OD/Dist THD. Input/output trim (±12 dB) trims around it.
    static constexpr float circuitVoltsPerFS = 0.66f;

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
    std::atomic<float>* pOversampleLive {};
    std::atomic<float>* pOversampleRender {};

    // ---- Oversampling: wraps ONLY each channel's nonlinear clip span (Stage2/SW1 + rail + SW2).
    // One oversampler per pedal channel (Yellow, Red), each multi-channel (L/R). Factor is chosen
    // per block from isNonRealtime() (render) vs live; the filter is low-latency IIR for live and
    // max-quality FIR for render. Rebuilt on the audio thread only when the factor/quality/channel
    // count changes (rare, user-driven) — one-block gap accepted per architecture.md.
    std::unique_ptr<juce::dsp::Oversampling<double>> osYellow, osRed;
    juce::AudioBuffer<double> scratchDry, scratchNodeG, scratchNodeHC;
    double baseSampleRate { 0.0 };
    int maxBlock { 0 };
    int activeLog2 { -1 };          // current OS exponent: 0 = 1x, 1 = 2x, 2 = 4x, 3 = 8x
    bool activeIsRender { false };
    int activeNumChannels { 0 };

    void cacheParamPointers();
    void pushParams();                               // APVTS → per-block knob values on both strips
    int currentFactorLog2() const;                   // from isNonRealtime() + APVTS
    void updateOversampling (int numCh);             // rebuild oversamplers + prepareClip on change
    void processPedalChannel (juce::AudioBuffer<float>& buf, int numCh, int numSamples,
                              bool isYellow, juce::dsp::Oversampling<double>* os);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonarchAudioProcessor)
};
