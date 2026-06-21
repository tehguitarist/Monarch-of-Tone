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
    pOversampleLive = get ("oversampling_realtime");
    pOversampleRender = get ("oversampling_render");
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

    const juce::StringArray clippingModeChoices { "Boost", "Overdrive", "Distortion" };
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
    baseSampleRate = sampleRate;
    maxBlock = samplesPerBlock;

    const int numCh = (int) strips.size();
    scratchDry.setSize (numCh, samplesPerBlock);
    scratchNodeG.setSize (numCh, samplesPerBlock);
    scratchNodeHC.setSize (numCh, samplesPerBlock);

    for (auto& s : strips)
    {
        s.yellow.prepareLinear (sampleRate);
        s.red.prepareLinear (sampleRate);
        s.wetYellow.reset (sampleRate, bypassRampSeconds);
        s.wetRed.reset (sampleRate, bypassRampSeconds);
        s.wetYellow.setCurrentAndTargetValue (pYellow.bypass->load() > 0.5f ? 0.0f : 1.0f);
        s.wetRed.setCurrentAndTargetValue (pRed.bypass->load() > 0.5f ? 0.0f : 1.0f);
    }

    tilt.prepare (sampleRate);
    shelfMix.reset (sampleRate, bypassRampSeconds);
    shelfMix.setCurrentAndTargetValue (1.0f);

    constexpr double trimRampSeconds = 0.005; // de-zipper input/output trim automation steps
    inTrimGain.reset (sampleRate, trimRampSeconds);
    outTrimGain.reset (sampleRate, trimRampSeconds);
    inTrimGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pInputTrim->load()) * circuitVoltsPerFS);
    outTrimGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutputTrim->load()) / circuitVoltsPerFS);

    // Force a (re)build of the clip-span oversamplers + prepareClip on the first block.
    activeLog2 = -1;
    activeNumChannels = 0;
    updateOversampling (getTotalNumInputChannels());
}

int MonarchAudioProcessor::currentFactorLog2() const
{
    // 0/1/2/3 → 1x/2x/4x/8x. Render path (offline bounce) uses its own choice.
    const auto* p = isNonRealtime() ? pOversampleRender : pOversampleLive;
    return juce::jlimit (0, 3, (int) p->load());
}

void MonarchAudioProcessor::updateOversampling (int numCh)
{
    const int wantLog2 = currentFactorLog2();
    const bool wantRender = isNonRealtime();
    numCh = juce::jmax (1, numCh);

    if (wantLog2 == activeLog2 && wantRender == activeIsRender && numCh == activeNumChannels)
        return; // nothing changed — stay on the per-sample hot path

    activeLog2 = wantLog2;
    activeIsRender = wantRender;
    activeNumChannels = numCh;

    if (wantLog2 == 0)
    {
        osYellow.reset(); // 1x — no oversampler, clip span runs at base rate
        osRed.reset();
    }
    else
    {
        // Live: low-latency minimum-phase IIR. Render: max-quality linear-phase FIR.
        const auto filter = wantRender ? juce::dsp::Oversampling<double>::filterHalfBandFIREquiripple
                                       : juce::dsp::Oversampling<double>::filterHalfBandPolyphaseIIR;
        auto make = [&] {
            auto os = std::make_unique<juce::dsp::Oversampling<double>> (
                (size_t) numCh, (size_t) wantLog2, filter, /*maxQuality*/ true);
            os->initProcessing ((size_t) maxBlock);
            return os;
        };
        osYellow = make();
        osRed = make();
    }

    // Re-prepare just the clip-span stages at the oversampled rate (base × 2^log2).
    const double clipRate = baseSampleRate * (double) (1 << wantLog2);
    for (auto& s : strips)
    {
        s.yellow.prepareClip (clipRate);
        s.red.prepareClip (clipRate);
    }

    // Report the added latency (two clip-span oversamplers in series; 0 at 1x).
    double latency = 0.0;
    if (osYellow != nullptr)
        latency += osYellow->getLatencyInSamples() + osRed->getLatencyInSamples();
    setLatencySamples ((int) std::lround (latency));
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

// Process one pedal channel (Yellow or Red) across the whole buffer: base-rate Stage 1 →
// oversampled clip span → base-rate Tone/Volume, then a click-free wet/dry bypass crossfade.
// Operates in place on `buf` (already in circuit volts on entry, still in circuit volts on exit).
void MonarchAudioProcessor::processPedalChannel (juce::AudioBuffer<float>& buf, int numCh,
                                                 int numSamples, bool isYellow,
                                                 juce::dsp::Oversampling<double>* os)
{
    auto smoother = [&] (size_t ch) -> juce::SmoothedValue<float>& {
        return isYellow ? strips[ch].wetYellow : strips[ch].wetRed;
    };
    auto chan = [&] (size_t ch) -> monarch::MonarchChannel& {
        return isYellow ? strips[ch].yellow : strips[ch].red;
    };

    // Fully bypassed (all channels settled at dry) → skip DSP and the oversampler entirely.
    bool anyActive = false;
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto& sm = smoother ((size_t) ch);
        if (sm.isSmoothing() || sm.getTargetValue() > 0.0f)
            anyActive = true;
    }
    if (! anyActive)
        return;

    // 1. Base-rate front: capture dry + Stage 1 → NodeG.
    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* in = buf.getReadPointer (ch);
        double* dry = scratchDry.getWritePointer (ch);
        double* ng = scratchNodeG.getWritePointer (ch);
        auto& c = chan ((size_t) ch);
        for (int n = 0; n < numSamples; ++n)
        {
            dry[n] = (double) in[n];
            ng[n] = c.processPre (dry[n]);
        }
    }

    // 2. Nonlinear clip span — oversampled (factor > 1) or direct (1x).
    if (os != nullptr)
    {
        juce::dsp::AudioBlock<double> ngBlock (scratchNodeG.getArrayOfWritePointers(),
                                               (size_t) numCh, (size_t) numSamples);
        auto up = os->processSamplesUp (ngBlock);
        const int osN = (int) up.getNumSamples();
        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& c = chan ((size_t) ch);
            for (int i = 0; i < osN; ++i)
                up.setSample (ch, i, c.processClip (up.getSample (ch, i)));
        }
        juce::dsp::AudioBlock<double> hcBlock (scratchNodeHC.getArrayOfWritePointers(),
                                               (size_t) numCh, (size_t) numSamples);
        os->processSamplesDown (hcBlock);
    }
    else
    {
        for (int ch = 0; ch < numCh; ++ch)
        {
            const double* ng = scratchNodeG.getReadPointer (ch);
            double* hc = scratchNodeHC.getWritePointer (ch);
            auto& c = chan ((size_t) ch);
            for (int n = 0; n < numSamples; ++n)
                hc[n] = c.processClip (ng[n]);
        }
    }

    // 3. Base-rate back: Tone → Volume, then wet/dry bypass crossfade → buf.
    for (int ch = 0; ch < numCh; ++ch)
    {
        const double* dry = scratchDry.getReadPointer (ch);
        const double* hc = scratchNodeHC.getReadPointer (ch);
        float* out = buf.getWritePointer (ch);
        auto& c = chan ((size_t) ch);
        auto& sm = smoother ((size_t) ch);
        for (int n = 0; n < numSamples; ++n)
        {
            const double wet = c.processPost (hc[n]);
            const double g = (double) sm.getNextValue();
            out[n] = (float) (dry[n] + g * (wet - dry[n]));
        }
    }
}

void MonarchAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) strips.size());

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    pushParams();
    updateOversampling (numChannels); // pick live/render factor; rebuild only on change

    inTrimGain.setTargetValue (juce::Decibels::decibelsToGain (pInputTrim->load()) * circuitVoltsPerFS);
    outTrimGain.setTargetValue (juce::Decibels::decibelsToGain (pOutputTrim->load()) / circuitVoltsPerFS);

    // Input trim → circuit volts, in place; capture input meter (post-trim). Ramp the gain per
    // sample (shared across channels so L/R track) so trim automation steps don't zipper.
    {
        std::array<float, 2> inPeak { 0.0f, 0.0f };
        for (int n = 0; n < numSamples; ++n)
        {
            const float g = inTrimGain.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* data = buffer.getWritePointer (ch);
                data[n] *= g;
                inPeak[(size_t) ch] = juce::jmax (inPeak[(size_t) ch], std::abs (data[n]));
            }
        }
        inputLevelL.store (inPeak[0]);
        if (numChannels > 1) inputLevelR.store (inPeak[1]);
    }

    // Series: Yellow → Red (each with its own clip-span oversampler).
    processPedalChannel (buffer, numChannels, numSamples, /*isYellow*/ true, osYellow.get());
    processPedalChannel (buffer, numChannels, numSamples, /*isYellow*/ false, osRed.get());

    // Capture-match calibration shelf (see PluginProcessor.h::TiltShelf). Applied once, post-
    // pedal, in circuit volts. Crossfaded out when BOTH channels are fully bypassed so true
    // bypass stays dry (the shelf models the capture chain, not the pedal). The filter runs
    // every sample regardless of the gate so its state stays warm (click-free re-engage).
    if (TiltShelf::kEnabled)
    {
        bool pedalActive = false;
        for (auto& s : strips)
        {
            auto live = [] (juce::SmoothedValue<float>& sm) {
                return sm.isSmoothing() || sm.getTargetValue() > 0.0f;
            };
            if (live (s.wetYellow) || live (s.wetRed))
                pedalActive = true;
        }
        shelfMix.setTargetValue (pedalActive ? 1.0f : 0.0f);

        for (int n = 0; n < numSamples; ++n)
        {
            const float g = shelfMix.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* data = buffer.getWritePointer (ch);
                const float dry = data[n];
                const float wet = tilt.process (ch, dry);
                data[n] = dry + g * (wet - dry);
            }
        }
    }

    // Output trim; capture output meter (post-trim). Ramp per sample (shared L/R) to de-zipper.
    {
        std::array<float, 2> outPeak { 0.0f, 0.0f };
        for (int n = 0; n < numSamples; ++n)
        {
            const float g = outTrimGain.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* data = buffer.getWritePointer (ch);
                data[n] *= g;
                outPeak[(size_t) ch] = juce::jmax (outPeak[(size_t) ch], std::abs (data[n]));
            }
        }
        outputLevelL.store (outPeak[0]);
        if (numChannels > 1) outputLevelR.store (outPeak[1]);
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
