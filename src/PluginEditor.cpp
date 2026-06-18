#include "PluginEditor.h"

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

    auto setupOSBox = [this] (ComboBox& box) {
        box.addItemList (kOsChoices, 1);
        box.setJustificationType (Justification::centred);
        addAndMakeVisible (box);
    };
    setupOSBox (osRealtimeBox);
    setupOSBox (osRenderBox);
    // Bind to the EXISTING processor params (live = oversampling_realtime, render = oversampling_render).
    osRealtimeAttach = std::make_unique<ComboBoxParameterAttachment> (
        *audioProcessor.apvts.getParameter ("oversampling_realtime"), osRealtimeBox);
    osRenderAttach = std::make_unique<ComboBoxParameterAttachment> (
        *audioProcessor.apvts.getParameter ("oversampling_render"), osRenderBox);

    scaleBtn.setComponentID ("scale");
    scaleBtn.setColour (TextButton::buttonColourId, Colour (MonarchLookAndFeel::cOSBtnActiveBg));
    scaleBtn.setColour (TextButton::textColourOffId, Colour (MonarchLookAndFeel::cOSBtnActive));
    scaleBtn.onClick = [this] { showScaleMenu(); };
    addAndMakeVisible (scaleBtn);

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

    // Pedal-face placeholder (the real face lands next phase).
    g.setColour (Colour (0xFF0C1424u));
    g.fillRoundedRectangle (pedalFaceArea.toFloat(), 8.0f);
    g.setColour (Colour (0xFF1A2A40u));
    g.drawRoundedRectangle (pedalFaceArea.toFloat().reduced (0.5f), 8.0f, 1.0f);
    g.setColour (Colour (MonarchLookAndFeel::cOSLabel));
    g.setFont (Font (FontOptions (12.0f * currentScale, Font::bold)));
    g.drawText ("PEDAL FACE", pedalFaceArea, Justification::centred);

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
    osLabel.setFont (bold (8.0f * sc));
    osLiveLabel.setFont (bold (7.0f * sc).withExtraKerningFactor (0.10f));
    osRenderLabel.setFont (bold (7.0f * sc).withExtraKerningFactor (0.10f));
    osSizeLabel.setFont (bold (7.0f * sc).withExtraKerningFactor (0.10f));
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

    auto layoutPanel = [&] (Rectangle<int> panel, Label& sec, Slider& knob, Label& sub, VUMeter& vu) {
        auto r = panel;
        sec.setBounds (r.removeFromTop (i (14)));
        r.removeFromTop (i (2));
        const int knobD = jmin (i (70), r.getWidth());
        auto knobRow = r.removeFromTop (knobD);
        knob.setBounds (knobRow.withSizeKeepingCentre (knobD, knobD));
        sub.setBounds (r.removeFromTop (i (12)));
        r.removeFromTop (i (4));
        const int vuW = jmin (i (34), r.getWidth());
        vu.setBounds (r.withSizeKeepingCentre (vuW, r.getHeight()));
    };
    layoutPanel (inPanel, inputSectionLabel, inputTrim, inputTrimSub, inputVU);
    layoutPanel (outPanel, outputSectionLabel, outputTrim, outputTrimSub, outputVU);

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

    scaleBtn.setBounds (os.removeFromRight (i (48)).reduced (0, boxVPad));
    os.removeFromRight (i (5));
    osSizeLabel.setBounds (os.removeFromRight (i (42)));

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
