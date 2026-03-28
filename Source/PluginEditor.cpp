#include "PluginProcessor.h"
#include "PluginEditor.h"

GravitasAudioProcessorEditor::GravitasAudioProcessorEditor (GravitasAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), planetField (p), waveformDisplay (p)
{
    using namespace UILayoutConst;

    setResizable (false, false);

    // Planet selector buttons
    static const char* symbols[] = { "\xe2\x98\xbf", "\xe2\x99\x80", "\xe2\x99\x81", "\xe2\x99\x82",
                                     "\xe2\x99\x83", "\xe2\x99\x84", "\xe2\x9b\xa2", "\xe2\x99\x86" };
    for (int i = 0; i < Planets::Count; ++i)
    {
        auto& btn = planetButtons[(size_t) i];
        btn.setButtonText (juce::String (juce::CharPointer_UTF8 (symbols[i])) + " " + Planets::All[i].name);
        btn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff1a1a2e));
        btn.setColour (juce::TextButton::buttonOnColourId, Planets::All[i].colour.darker (kBtnDarkerFactor));
        btn.setColour (juce::TextButton::textColourOffId,  juce::Colours::white.withAlpha (kBtnTextOffAlpha));
        btn.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        btn.setClickingTogglesState (false);
        btn.onClick = [this, i] { selectPlanet (i); };
        addAndMakeVisible (btn);
    }

    addAndMakeVisible (planetField);
    addAndMakeVisible (waveformDisplay);

    // Build param rows (right panel)
    auto& apvts = audioProcessor.apvts;
    auto addRow = [&] (const juce::String& label, const char* id)
    {
        paramRows.push_back (std::make_unique<ParamRow> (
            label, apvts.getParameter (id), apvts));
        addAndMakeVisible (*paramRows.back());
    };

    addRow ("Gravity",       "gravity");
    addRow ("Atmosphere",    "damping");
    addRow ("Wind",          "wind");
    addRow ("Ball Mass",     "ballMass");
    addRow ("Capture Bars",  "bufferBars");
    addRow ("Reset Bars",    "resetBars");

    syncWindowBtn.setColour (juce::ToggleButton::textColourId,
                             juce::Colours::white.withAlpha (kParamLabelAlpha));
    syncWindowAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, "syncWindow", syncWindowBtn);
    addAndMakeVisible (syncWindowBtn);

    addRow ("Cutoff",        "filterCutoff");
    addRow ("Resonance",     "filterRes");
    addRow ("Reverb Wet",    "reverbWet");
    addRow ("Reverb Decay",  "reverbDecay");
    addRow ("Saturation",    "saturation");
    addRow ("Tremolo Rate",  "tremoloRate");
    addRow ("Tremolo Depth", "tremoloDepth");
    addRow ("Echo Taps",     "echoTaps");
    addRow ("Echo Spacing",  "echoSpacing");
    addRow ("Echo Feedback", "echoFeedback");
    addRow ("Mix",           "mix");

    // Select current planet
    selectPlanet (audioProcessor.getCurrentPlanetIndex());

    setSize (kEditorWidth, kEditorHeight);
}

GravitasAudioProcessorEditor::~GravitasAudioProcessorEditor() {}

//==============================================================================
void GravitasAudioProcessorEditor::selectPlanet (int index)
{
    selectedPlanet = index;
    audioProcessor.setPlanet (index);
    planetField.setPlanet (index);
    waveformDisplay.setPlanet (index);

    for (int i = 0; i < Planets::Count; ++i)
        planetButtons[(size_t) i].setToggleState (i == index, juce::dontSendNotification);
}

//==============================================================================
void GravitasAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace UILayoutConst;

    // Overall background
    g.fillAll (juce::Colour (0xff0d0d1a));

    // Header background
    g.setColour (juce::Colour (0xff12122a));
    g.fillRect (0, 0, getWidth(), kHeaderHeight);

    // Title — right-aligned in the header
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (kTitleFontSize).withStyle ("Bold"));
    g.drawText ("GRAVITAS",
                getWidth() - kTitleWidth - kTitleRightPad, 0,
                kTitleWidth, kHeaderHeight,
                juce::Justification::centredRight);

    // Vertical divider between left field panel and right param panel
    g.setColour (juce::Colours::white.withAlpha (kDividerLineAlpha));
    g.fillRect (kDividerX, kHeaderHeight, 1, getHeight() - kHeaderHeight - kBottomBarHeight);

    // Bottom bar background
    g.setColour (juce::Colour (0xff12122a));
    g.fillRect (0, getHeight() - kBottomBarHeight, getWidth(), kBottomBarHeight);

    // Section headers in the right param panel
    auto drawSection = [&] (const juce::String& text, int y)
    {
        g.setColour (Planets::All[selectedPlanet].colour.withAlpha (kSectionLabelAlpha));
        g.setFont (juce::FontOptions (10.0f).withStyle ("Bold"));
        g.drawText (text, kParamPanelX, y, kParamPanelWidth, kSectionLabelHeight,
                    juce::Justification::centredLeft);
        g.setColour (juce::Colours::white.withAlpha (kSectionLineAlpha));
        g.drawHorizontalLine (y + kSectionLabelHeight - 2,
                              static_cast<float> (kParamPanelX), kSectionLineEndX);
    };

    drawSection ("PHYSICS",              sectionYPhysics);
    drawSection ("STUTTER",              sectionYStutter);
    drawSection ("FILTER",               sectionYFilter);
    drawSection ("REVERB",               sectionYReverb);
    drawSection ("SATURATION / TREMOLO", sectionYSatTrem);
    drawSection ("ECHO",                 sectionYEcho);
    drawSection ("OUTPUT",               sectionYOutput);
}

void GravitasAudioProcessorEditor::resized()
{
    using namespace UILayoutConst;

    // Planet selector row — buttons span the full header width equally
    int btnW = getWidth() / Planets::Count;
    for (int i = 0; i < Planets::Count; ++i)
        planetButtons[(size_t) i].setBounds (i * btnW, 0, btnW, kHeaderHeight);

    // Left panel
    planetField.setBounds    (0, kFieldY,    kFieldWidth, kFieldHeight);
    waveformDisplay.setBounds (0, kWaveformY, kFieldWidth, kWaveformHeight);

    // Param rows — right panel
    int px  = kParamPanelX;
    int py  = kParamPanelStartY;
    int pw  = kParamPanelWidth;
    int ph  = kParamRowHeight;
    int gap = kParamRowGap;

    // nextRow(extraTop) returns the bounds for the next row, advancing py.
    auto nextRow = [&] (int extraTop = 0) -> juce::Rectangle<int>
    {
        py += extraTop;
        auto r = juce::Rectangle<int> (px, py, pw, ph);
        py += ph + gap;
        return r;
    };
    auto sectionY = [&] { return py; }; // call BEFORE nextRow(kSectionSpacing)

    // Physics
    sectionYPhysics = sectionY();
    paramRows[0]->setBounds (nextRow (kSectionSpacing)); // gravity
    paramRows[1]->setBounds (nextRow());                 // atmosphere
    paramRows[2]->setBounds (nextRow());                 // wind
    paramRows[3]->setBounds (nextRow());                 // ballMass

    // Stutter
    sectionYStutter = sectionY();
    paramRows[4]->setBounds (nextRow (kSectionSpacing)); // bufferBars
    paramRows[5]->setBounds (nextRow());                 // resetBars
    syncWindowBtn.setBounds (nextRow());                 // syncWindow toggle

    // Filter
    sectionYFilter = sectionY();
    paramRows[6]->setBounds (nextRow (kSectionSpacing)); // filterCutoff
    paramRows[7]->setBounds (nextRow());                 // filterRes

    // Reverb
    sectionYReverb = sectionY();
    paramRows[8]->setBounds (nextRow (kSectionSpacing)); // reverbWet
    paramRows[9]->setBounds (nextRow());                 // reverbDecay

    // Saturation / Tremolo
    sectionYSatTrem = sectionY();
    paramRows[10]->setBounds (nextRow (kSectionSpacing)); // saturation
    paramRows[11]->setBounds (nextRow());                 // tremoloRate
    paramRows[12]->setBounds (nextRow());                 // tremoloDepth

    // Echo
    sectionYEcho = sectionY();
    paramRows[13]->setBounds (nextRow (kSectionSpacing)); // echoTaps
    paramRows[14]->setBounds (nextRow());                 // echoSpacing
    paramRows[15]->setBounds (nextRow());                 // echoFeedback

    // Output
    sectionYOutput = sectionY();
    paramRows[16]->setBounds (nextRow (kSectionSpacing)); // mix
}
