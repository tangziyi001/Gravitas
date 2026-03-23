#include "PluginProcessor.h"
#include "PluginEditor.h"

GravitasAudioProcessorEditor::GravitasAudioProcessorEditor (GravitasAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), planetField (p)
{
    setResizable (false, false);

    // Planet selector buttons
    static const char* symbols[] = { "\xe2\x98\xbf", "\xe2\x99\x80", "\xe2\x99\x81", "\xe2\x99\x82",
                                     "\xe2\x99\x83", "\xe2\x99\x84", "\xe2\x9b\xa2", "\xe2\x99\x86" };
    for (int i = 0; i < Planets::Count; ++i)
    {
        auto& btn = planetButtons[(size_t) i];
        btn.setButtonText (juce::String (juce::CharPointer_UTF8 (symbols[i])) + " " + Planets::All[i].name);
        btn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff1a1a2e));
        btn.setColour (juce::TextButton::buttonOnColourId, Planets::All[i].colour.darker (0.3f));
        btn.setColour (juce::TextButton::textColourOffId,  juce::Colours::white.withAlpha (0.6f));
        btn.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        btn.setClickingTogglesState (false);
        btn.onClick = [this, i] { selectPlanet (i); };
        addAndMakeVisible (btn);
    }

    addAndMakeVisible (planetField);

    // Build param rows (right panel)
    auto& apvts = audioProcessor.apvts;
    auto addRow = [&] (const juce::String& label, const char* id)
    {
        paramRows.push_back (std::make_unique<ParamRow> (
            label, apvts.getParameter (id), apvts));
        addAndMakeVisible (*paramRows.back());
    };

    addRow ("Gravity",        "gravity");
    addRow ("Atmosphere",     "damping");
    addRow ("Wind",           "wind");
    addRow ("Ball Mass",      "ballMass");
    addRow ("Capture Bars",   "bufferBars");
    addRow ("Cutoff",         "filterCutoff");
    addRow ("Resonance",      "filterRes");
    addRow ("Reverb Wet",     "reverbWet");
    addRow ("Reverb Decay",   "reverbDecay");
    addRow ("Saturation",     "saturation");
    addRow ("Tremolo Rate",   "tremoloRate");
    addRow ("Tremolo Depth",  "tremoloDepth");
    addRow ("Echo Taps",      "echoTaps");
    addRow ("Echo Spacing",   "echoSpacing");
    addRow ("Echo Feedback",  "echoFeedback");
    addRow ("Mix",            "mix");

    // Select current planet
    selectPlanet (audioProcessor.getCurrentPlanetIndex());

    setSize (960, 600);
}

GravitasAudioProcessorEditor::~GravitasAudioProcessorEditor() {}

//==============================================================================
void GravitasAudioProcessorEditor::selectPlanet (int index)
{
    selectedPlanet = index;
    audioProcessor.setPlanet (index);
    planetField.setPlanet (index);

    for (int i = 0; i < Planets::Count; ++i)
        planetButtons[(size_t) i].setToggleState (i == index, juce::dontSendNotification);
}

//==============================================================================
void GravitasAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Overall background
    g.fillAll (juce::Colour (0xff0d0d1a));

    // Header background
    g.setColour (juce::Colour (0xff12122a));
    g.fillRect (0, 0, getWidth(), 48);

    // Title
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (18.0f).withStyle ("Bold"));
    g.drawText ("GRAVITAS", getWidth() - 140, 0, 130, 48, juce::Justification::centredRight);

    // Right panel divider
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRect (640, 48, 1, getHeight() - 48 - 40);

    // Bottom bar background
    g.setColour (juce::Colour (0xff12122a));
    g.fillRect (0, getHeight() - 40, getWidth(), 40);

    // Section labels in param panel
    auto drawSection = [&] (const juce::String& text, int y)
    {
        g.setColour (Planets::All[selectedPlanet].colour.withAlpha (0.7f));
        g.setFont (juce::FontOptions (10.0f).withStyle ("Bold"));
        g.drawText (text, 648, y, 300, 16, juce::Justification::centredLeft);
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.drawHorizontalLine (y + 14, 648.0f, 952.0f);
    };

    drawSection ("PHYSICS",              sectionYPhysics);
    drawSection ("STUTTER",             sectionYStutter);
    drawSection ("FILTER",              sectionYFilter);
    drawSection ("REVERB",              sectionYReverb);
    drawSection ("SATURATION / TREMOLO", sectionYSatTrem);
    drawSection ("ECHO",                sectionYEcho);
    drawSection ("OUTPUT",              sectionYOutput);
}

void GravitasAudioProcessorEditor::resized()
{
    // Planet selector row
    int btnW = getWidth() / Planets::Count;
    for (int i = 0; i < Planets::Count; ++i)
        planetButtons[(size_t) i].setBounds (i * btnW, 0, btnW, 48);

    // Planet field: left 640px, below header, above bottom bar
    planetField.setBounds (0, 48, 640, getHeight() - 48 - 40);

    // Param rows — right panel
    int px = 648, py = 72, pw = 300, ph = 22, gap = 2;

    // nextRow records the current py as a section header y, then advances
    auto nextRow = [&] (int extraTop = 0) -> juce::Rectangle<int>
    {
        py += extraTop;
        auto r = juce::Rectangle<int> (px, py, pw, ph);
        py += ph + gap;
        return r;
    };
    auto sectionY = [&] { return py; }; // call BEFORE the nextRow(18) that follows

    // Physics
    sectionYPhysics = sectionY();
    paramRows[0]->setBounds (nextRow (18)); // gravity
    paramRows[1]->setBounds (nextRow());    // damping
    paramRows[2]->setBounds (nextRow());    // wind
    paramRows[3]->setBounds (nextRow());    // ballMass

    // Stutter
    sectionYStutter = sectionY();
    paramRows[4]->setBounds (nextRow (18)); // bufferBars

    // Filter
    sectionYFilter = sectionY();
    paramRows[5]->setBounds (nextRow (18)); // filterCutoff
    paramRows[6]->setBounds (nextRow());    // filterRes

    // Reverb
    sectionYReverb = sectionY();
    paramRows[7]->setBounds (nextRow (18));
    paramRows[8]->setBounds (nextRow());

    // Sat / Tremolo
    sectionYSatTrem = sectionY();
    paramRows[9]->setBounds  (nextRow (18));
    paramRows[10]->setBounds (nextRow());
    paramRows[11]->setBounds (nextRow());

    // Echo
    sectionYEcho = sectionY();
    paramRows[12]->setBounds (nextRow (18));
    paramRows[13]->setBounds (nextRow());
    paramRows[14]->setBounds (nextRow());

    // Mix
    sectionYOutput = sectionY();
    paramRows[15]->setBounds (nextRow (18));
}