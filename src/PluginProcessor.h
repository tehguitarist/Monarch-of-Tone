#pragma once

#include <array>
#include <cmath>
#include <complex>
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
    // Signal calibration: 0 dBFS ↔ absolute circuit volts. Pinned to MATCH the real-pedal (NAM)
    // captures in analysis/ — the captured KOT hits its clean/Boost rail-clip onset at a −18 dBFS
    // 1 kHz input. Re-calibrated 0.66 → 0.87 (+2.4 dB) after the Stage-1 Z_lower/floor rebuild
    // (2026-06-20), which dropped Stage-1 gain by ~2.4 dB, to restore the same clipping drive.
    // PROVISIONAL — the driven path (compression depth, even-harmonic shaper) still needs a full
    // re-validation against the captures after this rebuild; expect this value to be refined.
    static constexpr float circuitVoltsPerFS = 0.87f;

    // ---- Capture-match calibration (first-order high-shelf / "tilt") ----------------------
    // A/B vs the NAM captures of the real King of Tone showed a clean, GAIN-INVARIANT tilt
    // pivoting at ~1 kHz: the captures are ~+2 dB brighter above 1 kHz and ~1.7 dB lower in the
    // low-mids than our (schematic-faithful) model. The tilt has the SAME shape at every gain
    // and input level (verified G2 ≈ G4 on the clean sweep) and is linear — i.e. it is a
    // post-pedal artifact of the capture chain (reamp + interface ADC), NOT a circuit feature,
    // which is why no R-C in the verified netlist produces it. Because it is fixed and linear,
    // one shelf matches it across every scenario (clean/OD/Dist, hot/cold input, any drive).
    // We keep all circuit component values at their verified schematic values and correct the
    // capture chain here instead — the EQ analog of the circuitVoltsPerFS level calibration.
    // Normalized to unity at 1 kHz so it leaves the level calibration untouched. Fit: −2.6 dB
    // LF / +1.4 dB HF asymptotes, pivot 1.4 kHz → matches the measured tilt to ~0.4 dB RMS.
    // Set kEnabled=false to A/B the pure-circuit model. (decision 2026-06-20)
    struct TiltShelf
    {
        static constexpr bool kEnabled = false;
        static constexpr double kGloDb = -2.6;    // LF asymptote (dB)
        static constexpr double kGhiDb = 1.4;     // HF asymptote (dB)
        static constexpr double kPivotHz = 1400.0; // geometric centre of the zero/pole

        double b0 { 1.0 }, b1 { 0.0 }, a1 { 0.0 };
        std::array<double, 2> x1 {}, y1 {};

        void prepare (double fs)
        {
            constexpr double pi = juce::MathConstants<double>::pi;
            const double Glo = std::pow (10.0, kGloDb / 20.0);
            const double Ghi = std::pow (10.0, kGhiDb / 20.0);
            const double fz = kPivotHz * std::sqrt (Glo / Ghi);
            const double fp = kPivotHz * std::sqrt (Ghi / Glo);
            const double K = 2.0 * fs;
            const double wz = K * std::tan (pi * fz / fs); // prewarped zero
            const double wp = K * std::tan (pi * fp / fs); // prewarped pole
            const double a0 = K + wp;
            a1 = (wp - K) / a0;
            double nb0 = Ghi * (K + wz) / a0;
            double nb1 = Ghi * (wz - K) / a0;
            // Normalize to unity at 1 kHz (preserve the overall level / circuitVoltsPerFS cal).
            const double w1 = 2.0 * pi * 1000.0 / fs;
            const std::complex<double> z = std::exp (std::complex<double> (0.0, -w1));
            const double scale = 1.0 / std::abs ((nb0 + nb1 * z) / (1.0 + a1 * z));
            b0 = nb0 * scale;
            b1 = nb1 * scale;
            reset();
        }

        void reset()
        {
            x1 = {};
            y1 = {};
        }

        inline float process (int ch, float in) noexcept
        {
            const double x = (double) in;
            const double y = b0 * x + b1 * x1[(size_t) ch] - a1 * y1[(size_t) ch];
            x1[(size_t) ch] = x;
            y1[(size_t) ch] = y;
            return (float) y;
        }
    };
    TiltShelf tilt;
    juce::SmoothedValue<float> shelfMix; // 1 = shelf engaged, 0 = true-bypass (dry, no shelf)

    // Input/output trim gains, smoothed (~5 ms) so DAW automation steps don't zipper. These are
    // pure pre/post level multipliers (no circuit state), so smoothing is tone-neutral. The factor
    // includes the circuitVoltsPerFS calibration (in: ×cal, out: ÷cal). 2026-06-22.
    juce::SmoothedValue<float> inTrimGain, outTrimGain;

    // One dual-mono pedal per audio channel (index 0 = L, 1 = R). Each strip is the full
    // Red → Yellow series chain (the real pedal's signal flow — Red is first); both strips
    // share the same knob settings. Hi Gain is fixed per channel via the MonarchChannel ctor
    // flag (Yellow stock, Red Hi-Gain).
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
    std::atomic<float>* pSupplyVoltage {};
    std::atomic<float>* pOversampleLive {};
    std::atomic<float>* pOversampleRender {};

    // ---- Oversampling: wraps each channel's WHOLE chain (Stage 1 + clip span + Tone/Volume), so
    // the linear WDF stages' near-Nyquist bilinear warp shrinks with the OS factor (see dsp.md).
    // One oversampler per pedal channel (Yellow, Red), each multi-channel (L/R). Factor is chosen
    // per block from isNonRealtime() (render) vs live; the filter is low-latency IIR for live and
    // max-quality FIR for render. Rebuilt on the audio thread only when the factor/quality/channel
    // count changes (rare, user-driven) — one-block gap accepted per architecture.md.
    std::unique_ptr<juce::dsp::Oversampling<double>> osYellow, osRed;
    juce::AudioBuffer<double> scratchDry, scratchNodeHC;
    double baseSampleRate { 0.0 };
    int maxBlock { 0 };
    int activeLog2 { -1 };          // current OS exponent: 0 = 1x, 1 = 2x, 2 = 4x, 3 = 8x
    bool activeIsRender { false };
    int activeNumChannels { 0 };
    int currentProgramIndex { 0 }; // index into monarch::getFactoryPresets(); host preset browser

    void cacheParamPointers();
    void pushParams();                               // APVTS → per-block knob values on both strips
    int currentFactorLog2() const;                   // from isNonRealtime() + APVTS
    void updateOversampling (int numCh);             // rebuild oversamplers + prepareClip on change
    void processPedalChannel (juce::AudioBuffer<float>& buf, int numCh, int numSamples,
                              bool isYellow, juce::dsp::Oversampling<double>* os);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MonarchAudioProcessor)
};
