#include "PluginEditor.h"

// Build version for the UI, sourced from CMake PROJECT_VERSION (see CMakeLists target defs).
// Fallback keeps the non-plugin tools (which compile this file) building without the define.
#ifndef MONARCH_VERSION_STRING
 #define MONARCH_VERSION_STRING "dev"
#endif

using namespace juce;

namespace
{
const StringArray kOsChoices { "1x", "2x", "4x", "8x" };

constexpr float kScales[] = { 0.50f, 0.75f, 1.00f, 1.25f, 1.50f, 1.75f, 2.00f, 2.25f, 2.50f };
constexpr const char* kScaleLabels[] = { "50%", "75%", "100%", "125%", "150%",
                                         "175%", "200%", "225%", "250%" };
} // namespace

MonarchAudioProcessorEditor::MonarchAudioProcessorEditor (MonarchAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lnf);

    // Cross-session default scale via ApplicationProperties; per-session via APVTS state.
    PropertiesFile::Options opts;
    opts.applicationName = "MonarchOfTone";
    opts.filenameSuffix = ".settings";
    opts.folderName = "LeighPierce";
    opts.osxLibrarySubFolder = "Application Support";
    appProps.setStorageParameters (opts);

    if (const auto v = audioProcessor.apvts.state.getProperty ("uiScale"); ! v.isVoid())
        currentScale = (float) (double) v;
    else
        currentScale = (float) appProps.getUserSettings()->getDoubleValue ("defaultScale", 1.0);
    currentScale = jlimit (0.5f, 2.5f, currentScale);

    auto setupSectionLabel = [this] (Label& l, const String& text) {
        l.setText (text, dontSendNotification);
        l.setJustificationType (Justification::centred);
        l.setColour (Label::textColourId, Colour (MonarchLookAndFeel::cTrimLabel));
        addAndMakeVisible (l);
    };
    setupSectionLabel (inputSectionLabel, "INPUT");
    setupSectionLabel (outputSectionLabel, "OUTPUT");

    auto setupTrimSub = [this] (Label& l) {
        l.setText ("TRIM", dontSendNotification);
        l.setJustificationType (Justification::centred);
        l.setColour (Label::textColourId, Colour (MonarchLookAndFeel::cTrimLabel).darker (0.25f));
        addAndMakeVisible (l);
    };
    setupTrimSub (inputTrimSub);
    setupTrimSub (outputTrimSub);

    auto setupTrimValue = [this] (Label& l) {
        l.setJustificationType (Justification::centred);
        l.setColour (Label::textColourId, Colour (MonarchLookAndFeel::cTrimLabel));
        addAndMakeVisible (l);
    };
    setupTrimValue (inputTrimValue);
    setupTrimValue (outputTrimValue);

    const float pi = MathConstants<float>::pi;
    auto setupTrim = [this, pi] (Slider& s) {
        s.setComponentID ("trim");
        s.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters (pi * 1.25f, pi * 2.75f, true); // 270° sweep, gap at bottom
        s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        s.setDoubleClickReturnValue (true, 0.0); // 0 dB
        addAndMakeVisible (s);
    };
    setupTrim (inputTrim);
    setupTrim (outputTrim);
    inputTrimAttach = std::make_unique<SliderParameterAttachment> (
        *audioProcessor.apvts.getParameter ("input_trim"), inputTrim);
    outputTrimAttach = std::make_unique<SliderParameterAttachment> (
        *audioProcessor.apvts.getParameter ("output_trim"), outputTrim);

    auto formatTrim = [] (double dB) { return String (dB, 1) + " dB"; };
    inputTrimValue.setText (formatTrim (inputTrim.getValue()), dontSendNotification);
    outputTrimValue.setText (formatTrim (outputTrim.getValue()), dontSendNotification);
    inputTrim.onValueChange = [this, formatTrim] {
        inputTrimValue.setText (formatTrim (inputTrim.getValue()), dontSendNotification);
        mirrorTrim (true);
    };
    outputTrim.onValueChange = [this, formatTrim] {
        outputTrimValue.setText (formatTrim (outputTrim.getValue()), dontSendNotification);
        mirrorTrim (false);
    };

    // Seed the delta caches AFTER the attachments have pushed the restored parameter values into
    // the sliders, so a session recalled with non-zero trims doesn't read as a jump on the first move.
    lastInputTrim = inputTrim.getValue();
    lastOutputTrim = outputTrim.getValue();

    addAndMakeVisible (inputVU);
    addAndMakeVisible (outputVU);

    // ---- Oversampling strip ----
    auto setupOSLabel = [this] (Label& l, const String& text, Justification j) {
        l.setText (text, dontSendNotification);
        l.setJustificationType (j);
        l.setColour (Label::textColourId, Colour (MonarchLookAndFeel::cOSLabel));
        addAndMakeVisible (l);
    };
    setupOSLabel (osLabel, "OS", Justification::centredLeft);
    setupOSLabel (osLiveLabel, "LIVE", Justification::centredRight);
    setupOSLabel (osRenderLabel, "RENDER", Justification::centredRight);
    setupOSLabel (osSizeLabel, "UI SIZE", Justification::centredRight);
    setupOSLabel (trimLockLabel, "TRIM", Justification::centredRight);
    // Version, centred in the strip. From the build (JucePlugin_VersionString == CMake PROJECT_VERSION),
    // never hardcoded, so a version bump flows through automatically. Dimmed — informational, not a control.
    setupOSLabel (osVersionLabel, "v" MONARCH_VERSION_STRING, Justification::centred);
    osVersionLabel.setColour (Label::textColourId, Colour (MonarchLookAndFeel::cOSLabel).withAlpha (0.55f));

    auto setupOSBox = [this] (ComboBox& box) {
        box.addItemList (kOsChoices, 1);
        box.setJustificationType (Justification::centred);
        box.setColour (ComboBox::textColourId, Colour (MonarchLookAndFeel::cOSBtnActive));
        addAndMakeVisible (box);
    };
    setupOSBox (osRealtimeBox);
    setupOSBox (osRenderBox);
    // Bind to the EXISTING processor params (live = oversampling_realtime, render = oversampling_render).
    osRealtimeAttach = std::make_unique<ComboBoxParameterAttachment> (
        *audioProcessor.apvts.getParameter ("oversampling_realtime"), osRealtimeBox);
    osRenderAttach = std::make_unique<ComboBoxParameterAttachment> (
        *audioProcessor.apvts.getParameter ("oversampling_render"), osRenderBox);

    // Trim lock — ties the two trims together (delta-linked; see mirrorTrim). "ostoggle" gives it
    // the lit-on/dim-off treatment in the L&F; the text colour follows the toggle state via the
    // on/off colour IDs (LookAndFeel_V4::drawButtonText picks between them).
    trimLockButton.setComponentID ("ostoggle");
    trimLockButton.setClickingTogglesState (true);
    trimLockButton.setColour (TextButton::textColourOnId, Colour (MonarchLookAndFeel::cOSBtnActive));
    trimLockButton.setColour (TextButton::textColourOffId, Colour (MonarchLookAndFeel::cOSLabel));
    addAndMakeVisible (trimLockButton);
    trimLockAttach = std::make_unique<ButtonParameterAttachment> (
        *audioProcessor.apvts.getParameter ("trim_lock"), trimLockButton);

    scaleBtn.setComponentID ("scale");
    scaleBtn.setColour (TextButton::buttonColourId, Colour (MonarchLookAndFeel::cOSBtnActiveBg));
    scaleBtn.setColour (TextButton::textColourOffId, Colour (MonarchLookAndFeel::cOSBtnActive));
    scaleBtn.onClick = [this] { showScaleMenu(); };
    addAndMakeVisible (scaleBtn);

    pedalFace = std::make_unique<PedalFace> (audioProcessor.apvts);
    addAndMakeVisible (*pedalFace);

    setResizable (true, true);
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio ((double) kBaseW / (double) kBaseH);
        c->setSizeLimits (roundToInt (kBaseW * 0.5f), roundToInt (kBaseH * 0.5f),
                          roundToInt (kBaseW * 2.5f), roundToInt (kBaseH * 2.5f));
    }
    setSize (roundToInt (kBaseW * currentScale), roundToInt (kBaseH * currentScale));

    startTimerHz (33);
}

MonarchAudioProcessorEditor::~MonarchAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void MonarchAudioProcessorEditor::paint (Graphics& g)
{
    g.fillAll (Colour (MonarchLookAndFeel::cBackground));

    // (The pedal face paints itself — `pedalFace` covers pedalFaceArea.)

    // Oversampling strip background.
    g.setColour (Colour (MonarchLookAndFeel::cOSBackground));
    g.fillRoundedRectangle (osStripArea.toFloat(), 6.0f);
    g.setColour (Colour (MonarchLookAndFeel::cOSBorder));
    g.drawRoundedRectangle (osStripArea.toFloat().reduced (0.5f), 6.0f, 1.0f);
}

void MonarchAudioProcessorEditor::refreshFonts (float sc)
{
    auto bold = [] (float sz) { return Font (FontOptions (sz, Font::bold)); };
    inputSectionLabel.setFont (bold (8.0f * sc).withExtraKerningFactor (0.20f));
    outputSectionLabel.setFont (bold (8.0f * sc).withExtraKerningFactor (0.20f));
    inputTrimSub.setFont (bold (7.5f * sc).withExtraKerningFactor (0.15f));
    outputTrimSub.setFont (bold (7.5f * sc).withExtraKerningFactor (0.15f));
    inputTrimValue.setFont (bold (8.5f * sc));
    outputTrimValue.setFont (bold (8.5f * sc));
    osLabel.setFont (bold (8.0f * sc));
    osLiveLabel.setFont (bold (7.0f * sc).withExtraKerningFactor (0.10f));
    osRenderLabel.setFont (bold (7.0f * sc).withExtraKerningFactor (0.10f));
    osSizeLabel.setFont (bold (7.0f * sc).withExtraKerningFactor (0.10f));
    trimLockLabel.setFont (bold (7.0f * sc).withExtraKerningFactor (0.10f));
    osVersionLabel.setFont (bold (7.0f * sc).withExtraKerningFactor (0.10f));
}

void MonarchAudioProcessorEditor::resized()
{
    currentScale = (float) getWidth() / (float) kBaseW;
    const float sc = currentScale;
    const auto i = [sc] (int v) { return roundToInt ((float) v * sc); };
    refreshFonts (sc);

    const int W = getWidth(), H = getHeight();
    const int margin = i (10);
    const int panelW = i (74);
    const int osH = i (24);
    const int faceGap = i (10);
    const int colGap = i (8);

    const int topY = margin;
    const int topH = H - margin - osH - faceGap - margin;

    osStripArea = Rectangle<int> (margin, H - margin - osH, W - 2 * margin, osH);

    const Rectangle<int> inPanel (margin, topY, panelW, topH);
    const Rectangle<int> outPanel (W - margin - panelW, topY, panelW, topH);
    pedalFaceArea = Rectangle<int> (margin + panelW + colGap, topY,
                                    W - 2 * (margin + panelW + colGap), topH);
    if (pedalFace != nullptr)
    {
        pedalFace->setBounds (pedalFaceArea);
        pedalFace->refresh (sc);
    }

    auto layoutPanel = [&] (Rectangle<int> panel, Label& sec, Slider& knob, Label& sub, Label& val, VUMeter& vu) {
        auto r = panel;
        sec.setBounds (r.removeFromTop (i (14)));
        r.removeFromTop (i (2));
        const int knobD = jmin (i (70), r.getWidth());
        auto knobRow = r.removeFromTop (knobD);
        knob.setBounds (knobRow.withSizeKeepingCentre (knobD, knobD));
        sub.setBounds (r.removeFromTop (i (12)));
        val.setBounds (r.removeFromTop (i (12)));
        r.removeFromTop (i (2));
        const int vuW = jmin (i (34), r.getWidth());
        vu.setBounds (r.withSizeKeepingCentre (vuW, r.getHeight()));
    };
    layoutPanel (inPanel, inputSectionLabel, inputTrim, inputTrimSub, inputTrimValue, inputVU);
    layoutPanel (outPanel, outputSectionLabel, outputTrim, outputTrimSub, outputTrimValue, outputVU);

    // ---- Oversampling strip content (inset from the bg) ----
    auto os = osStripArea.reduced (i (6), 0);
    const int boxVPad = i (2);
    osLabel.setBounds (os.removeFromLeft (i (20)));
    os.removeFromLeft (i (8));
    osLiveLabel.setBounds (os.removeFromLeft (i (26)));
    os.removeFromLeft (i (5));
    osRealtimeBox.setBounds (os.removeFromLeft (i (36)).reduced (0, boxVPad));
    os.removeFromLeft (i (12));
    osRenderLabel.setBounds (os.removeFromLeft (i (40)));
    os.removeFromLeft (i (5));
    osRenderBox.setBounds (os.removeFromLeft (i (36)).reduced (0, boxVPad));
    os.removeFromLeft (i (12));
    trimLockLabel.setBounds (os.removeFromLeft (i (24)));
    os.removeFromLeft (i (5));
    // LOCK is 4 glyphs against the 2-glyph OS boxes and getTextButtonFont floors the size at 7 px,
    // so below ~0.75× the text stops shrinking with the box; give it ~1.5× the 36 px OS-box width
    // so the word still fits at the 0.5× minimum. The centre version label absorbs the extra width.
    trimLockButton.setBounds (os.removeFromLeft (i (52)).reduced (0, boxVPad));

    scaleBtn.setBounds (os.removeFromRight (i (48)).reduced (0, boxVPad));
    os.removeFromRight (i (5));
    osSizeLabel.setBounds (os.removeFromRight (i (42)));

    // Version fills the centre gap between the RENDER box and the UI-SIZE controls (centred text).
    osVersionLabel.setBounds (os);

    scaleBtn.setButtonText (String (roundToInt (currentScale * 100.0f)) + "%");

    // Persist the scale per-session (survives DAW preset recall).
    audioProcessor.apvts.state.setProperty ("uiScale", (double) currentScale, nullptr);
}

void MonarchAudioProcessorEditor::timerCallback()
{
    constexpr float kNoiseFl = 5.0e-4f; // -66 dBFS silence floor

    float in = jmax (audioProcessor.getInputLevel (0), vuInDecay * 0.90f);
    if (in < kNoiseFl)
        in = 0.0f;
    vuInDecay = in;
    inputVU.setLevel (in);

    float out = jmax (audioProcessor.getOutputLevel (0), vuOutDecay * 0.90f);
    if (out < kNoiseFl)
        out = 0.0f;
    vuOutDecay = out;
    outputVU.setLevel (out);

    if (pedalFace != nullptr)
        pedalFace->updateLEDs();
}

void MonarchAudioProcessorEditor::mirrorTrim (bool sourceIsInput)
{
    const Slider& src = sourceIsInput ? inputTrim : outputTrim;
    double& srcLast = sourceIsInput ? lastInputTrim : lastOutputTrim;
    const double dstLast = sourceIsInput ? lastOutputTrim : lastInputTrim;

    // Cache the new source value FIRST and unconditionally — the delta must be measured against the
    // previous position even when the lock is off, otherwise the first move after enabling it would
    // be computed from a stale reference and jump the other knob.
    const double delta = src.getValue() - srcLast;
    srcLast = src.getValue();

    // trimLinkBusy: this call is the echo of our own write to the other parameter — that side's
    // cache has just been refreshed above, which is all this pass needs to do.
    if (trimLinkBusy || ! trimLockButton.getToggleState() || delta == 0.0)
        return;

    // Equal and opposite CHANGE, relative to where the other knob already sits — so the pair's
    // existing offset is preserved and the starting values don't matter. Clamped at the rails.
    const auto target = (float) jlimit (-kTrimRange, kTrimRange, dstLast - delta);

    if (auto* param = audioProcessor.apvts.getParameter (sourceIsInput ? "output_trim" : "input_trim"))
    {
        const ScopedValueSetter<bool> guard (trimLinkBusy, true);
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (target));
        param->endChangeGesture();
    }
}

void MonarchAudioProcessorEditor::showScaleMenu()
{
    PopupMenu menu;
    for (int n = 0; n < 9; ++n)
        menu.addItem (n + 1, kScaleLabels[n], true, std::abs (currentScale - kScales[n]) < 0.01f);
    menu.addSeparator();
    menu.addItem (100, "Set current scale as default");

    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&scaleBtn), [this] (int r) {
        if (r >= 1 && r <= 9)
            setSize (roundToInt (kBaseW * kScales[r - 1]), roundToInt (kBaseH * kScales[r - 1]));
        else if (r == 100)
            appProps.getUserSettings()->setValue ("defaultScale", (double) currentScale);
    });
}
