#include "PluginProcessor.h"
#include "PluginEditor.h"

MonarchAudioProcessor::MonarchAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    cacheParamPointers();
}

void MonarchAudioProcessor::cacheParamPointers()
{
    auto get = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };

    pYellow = { get ("drive_yellow"), get ("tone_yellow"), get ("volume_yellow"),
                get ("presence_yellow"), get ("clipping_mode_yellow"), get ("bypass_yellow") };
    pRed = { get ("drive_red"), get ("tone_red"), get ("volume_red"),
             get ("presence_red"), get ("clipping_mode_red"), get ("bypass_red") };
    pInputTrim = get ("input_trim");
    pOutputTrim = get ("output_trim");
}

void MonarchAudioProcessor::pushParams()
{
    // Read APVTS (atomic) once per block and set the WDF knob values on both stereo strips.
    // Block-rate updates: each setter recomputes its stage's impedance, so this stays off the
    // per-sample path. Both strips (L/R) share the same knob settings (dual-mono pedal).
    for (auto& s : strips)
    {
        s.yellow.setDrive (pYellow.drive->load());
        s.yellow.setTone (pYellow.tone->load());
        s.yellow.setVolume (pYellow.volume->load());
        s.yellow.setPresence (pYellow.presence->load());
        s.yellow.setClippingMode ((int) pYellow.clip->load());

        s.red.setDrive (pRed.drive->load());
        s.red.setTone (pRed.tone->load());
        s.red.setVolume (pRed.volume->load());
        s.red.setPresence (pRed.presence->load());
        s.red.setClippingMode ((int) pRed.clip->load());

        s.wetYellow.setTargetValue (pYellow.bypass->load() > 0.5f ? 0.0f : 1.0f);
        s.wetRed.setTargetValue (pRed.bypass->load() > 0.5f ? 0.0f : 1.0f);
    }
}

MonarchAudioProcessor::~MonarchAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout MonarchAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    const juce::StringArray clippingModeChoices { "Boost", "Overdrive", "Distortion", "Both" };
    const juce::StringArray oversamplingChoices { "1x", "2x", "4x", "8x" };

    // The two series channels are identified externally by their LED colour: the first
    // channel is "Yellow", the second is "Red". The Theseus Hi-Gain mod is a FIXED part of
    // the Red channel's Stage 1 (not a runtime parameter) — see circuit.md Section 6.
    struct ChannelSpec
    {
        const char* id;
        const char* label;
    };

    for (const auto& channel : { ChannelSpec { "yellow", "Yellow" }, ChannelSpec { "red", "Red" } })
    {
        const juce::String id = channel.id;
        const juce::String label = channel.label;

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "drive_" + id, 1 },
            "Drive " + label,
            juce::NormalisableRange<float> (0.0f, 1.0f),
            0.5f));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "tone_" + id, 1 },
            "Tone " + label,
            juce::NormalisableRange<float> (0.0f, 1.0f),
            0.5f));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "volume_" + id, 1 },
            "Volume " + label,
            juce::NormalisableRange<float> (0.0f, 1.0f),
            0.5f));

        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "presence_" + id, 1 },
            "Presence " + label,
            juce::NormalisableRange<float> (0.0f, 1.0f),
            0.0f));

        params.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { "clipping_mode_" + id, 1 },
            "Clipping " + label,
            clippingModeChoices,
            1));

        params.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { "bypass_" + id, 1 },
            "Bypass " + label,
            false));
    }

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "input_trim", 1 },
        "Input Trim",
        juce::NormalisableRange<float> (-12.0f, 12.0f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "output_trim", 1 },
        "Output Trim",
        juce::NormalisableRange<float> (-12.0f, 12.0f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "oversampling_realtime", 1 },
        "Oversampling (Live)",
        oversamplingChoices,
        2));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "oversampling_render", 1 },
        "Oversampling (Render)",
        oversamplingChoices,
        3));

    return { params.begin(), params.end() };
}

void MonarchAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    constexpr double bypassRampSeconds = 0.005; // ~5 ms click-free bypass crossfade
    for (auto& s : strips)
    {
        s.yellow.prepare (sampleRate, samplesPerBlock);
        s.red.prepare (sampleRate, samplesPerBlock);
        s.wetYellow.reset (sampleRate, bypassRampSeconds);
        s.wetRed.reset (sampleRate, bypassRampSeconds);
        s.wetYellow.setCurrentAndTargetValue (pYellow.bypass->load() > 0.5f ? 0.0f : 1.0f);
        s.wetRed.setCurrentAndTargetValue (pRed.bypass->load() > 0.5f ? 0.0f : 1.0f);
    }
}

void MonarchAudioProcessor::releaseResources()
{
}

bool MonarchAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    // Guitar-pedal layout: mono or stereo only, with the input matching the output
    // (no up-/down-mixing). This lets the plugin load on mono tracks as well as stereo.
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    return mainIn == mainOut;
}

void MonarchAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) strips.size());

    // Clear any output channels with no matching input.
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    pushParams();

    const float inGain = juce::Decibels::decibelsToGain (pInputTrim->load()) * circuitVoltsPerFS;
    const float outGain = juce::Decibels::decibelsToGain (pOutputTrim->load()) / circuitVoltsPerFS;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& strip = strips[(size_t) ch];
        float* data = buffer.getWritePointer (ch);
        float inPeak = 0.0f, outPeak = 0.0f;

        for (int n = 0; n < numSamples; ++n)
        {
            // Host float → absolute circuit volts (post input trim).
            const double dryY = (double) (data[n] * inGain);
            inPeak = juce::jmax (inPeak, std::abs ((float) dryY));

            // Yellow channel, with click-free bypass crossfade (wet=1 active, 0 = dry pass).
            const double wetY = strip.yellow.processSample (dryY);
            const double gY = (double) strip.wetYellow.getNextValue();
            const double afterY = dryY + gY * (wetY - dryY);

            // Red channel (series after Yellow), same crossfade.
            const double wetR = strip.red.processSample (afterY);
            const double gR = (double) strip.wetRed.getNextValue();
            const double afterR = afterY + gR * (wetR - afterY);

            const float out = (float) afterR * outGain;
            data[n] = out;
            outPeak = juce::jmax (outPeak, std::abs (out));
        }

        (ch == 0 ? inputLevelL : inputLevelR).store (inPeak);
        (ch == 0 ? outputLevelL : outputLevelR).store (outPeak);
    }
}

juce::AudioProcessorEditor* MonarchAudioProcessor::createEditor()
{
    return new MonarchAudioProcessorEditor (*this);
}

bool MonarchAudioProcessor::hasEditor() const
{
    return true;
}

const juce::String MonarchAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool MonarchAudioProcessor::acceptsMidi() const
{
    return false;
}

bool MonarchAudioProcessor::producesMidi() const
{
    return false;
}

double MonarchAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int MonarchAudioProcessor::getNumPrograms()
{
    return 1;
}

int MonarchAudioProcessor::getCurrentProgram()
{
    return 0;
}

void MonarchAudioProcessor::setCurrentProgram (int)
{
}

const juce::String MonarchAudioProcessor::getProgramName (int)
{
    return {};
}

void MonarchAudioProcessor::changeProgramName (int, const juce::String&)
{
}

void MonarchAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        if (auto xml = state.createXml())
            copyXmlToBinary (*xml, destData);
    }
}

void MonarchAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MonarchAudioProcessor();
}
