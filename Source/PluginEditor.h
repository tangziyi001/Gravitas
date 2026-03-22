#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Physics/BallPhysics.h"
#include "Physics/PlanetPresets.h"
#include <BinaryData.h>

//==============================================================================
// Renders the 2D planet field + animated ball
class PlanetFieldComponent : public juce::Component,
                              public juce::Timer
{
public:
    PlanetFieldComponent (GravitasAudioProcessor& p) : proc (p)
    {
        int idx = proc.getCurrentPlanetIndex();
        physics.setPlanet (Planets::All[idx]);
        currentColour  = Planets::All[idx].colour;
        loadTexture (idx);
        startTimerHz (30);
    }

    void setPlanet (int index)
    {
        physics.setPlanet (Planets::All[index]);
        currentColour = Planets::All[index].colour;
        loadTexture (index);
        repaint();
    }

    void timerCallback() override
    {
        float rms = proc.audioRMS.load (std::memory_order_relaxed);
        physics.update (1.0f / 30.0f, rms);

        proc.ballX.store (physics.getStutterAxis(),   std::memory_order_relaxed);
        proc.ballY.store (physics.getIntensityAxis(),  std::memory_order_relaxed);

        // Store trail
        auto s = physics.getState();
        trail.push_back ({ s.x, s.y });
        if (trail.size() > 40) trail.erase (trail.begin());

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        float cx = bounds.getCentreX();
        float cy = bounds.getCentreY();
        float rx = bounds.getWidth()  * 0.5f;
        float ry = bounds.getHeight() * 0.5f;

        // Planet surface texture
        if (surfaceTexture.isValid())
        {
            g.drawImage (surfaceTexture, bounds, juce::RectanglePlacement::fillDestination);
        }
        else
        {
            g.fillAll (juce::Colour (0xff0a0a12));
        }

        // Atmospheric haze overlay — opacity scales with damping (atmosphere density)
        float hazeAlpha = currentDamping * 0.45f;
        juce::ColourGradient haze (currentColour.withAlpha (hazeAlpha), cx, cy,
                                   juce::Colours::transparentBlack, 0.0f, 0.0f, true);
        g.setGradientFill (haze);
        g.fillRect (bounds);

        // Dark vignette edges
        juce::ColourGradient vignette (juce::Colours::transparentBlack, cx, cy,
                                       juce::Colours::black.withAlpha (0.6f), 0.0f, 0.0f, true);
        g.setGradientFill (vignette);
        g.fillRect (bounds);

        // Ball trail
        for (int i = 0; i < (int) trail.size(); ++i)
        {
            float alpha = (float) i / (float) trail.size() * 0.5f;
            float r = 4.0f * ((float) i / trail.size());
            float tx = cx + trail[(size_t) i].x * rx * 0.85f;
            float ty = cy + trail[(size_t) i].y * ry * 0.85f;
            g.setColour (currentColour.withAlpha (alpha));
            g.fillEllipse (tx - r, ty - r, r * 2.0f, r * 2.0f);
        }

        // Ball glow
        auto s = physics.getState();
        float bx = cx + s.x * rx * 0.85f;
        float by = cy + s.y * ry * 0.85f;
        float speed = physics.speed();
        float glowRadius = 20.0f + speed * 15.0f;

        juce::ColourGradient glow (currentColour.withAlpha (0.4f), bx, by,
                                   juce::Colours::transparentBlack, bx + glowRadius, by, false);
        g.setGradientFill (glow);
        g.fillEllipse (bx - glowRadius, by - glowRadius, glowRadius * 2, glowRadius * 2);

        // Ball core
        g.setColour (currentColour.brighter (0.8f));
        g.fillEllipse (bx - 6.0f, by - 6.0f, 12.0f, 12.0f);
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillEllipse (bx - 3.0f, by - 5.0f, 4.0f, 4.0f); // highlight
    }

private:
    void loadTexture (int planetIndex)
    {
        struct Entry { const char* data; int size; };
        static const Entry entries[] = {
            { BinaryData::mercury_jpg, BinaryData::mercury_jpgSize },
            { BinaryData::venus_jpg,   BinaryData::venus_jpgSize   },
            { BinaryData::earth_jpg,   BinaryData::earth_jpgSize   },
            { BinaryData::mars_jpg,    BinaryData::mars_jpgSize    },
            { BinaryData::jupiter_jpg, BinaryData::jupiter_jpgSize },
            { BinaryData::saturn_jpg,  BinaryData::saturn_jpgSize  },
            { BinaryData::uranus_jpg,  BinaryData::uranus_jpgSize  },
            { BinaryData::neptune_jpg, BinaryData::neptune_jpgSize },
        };
        if (planetIndex < 0 || planetIndex >= 8) return;
        const auto& e = entries[planetIndex];
        juce::MemoryInputStream stream (e.data, (size_t) e.size, false);
        surfaceTexture = juce::ImageFileFormat::loadFrom (stream);
        currentDamping = Planets::All[planetIndex].damping;
    }

    GravitasAudioProcessor& proc;
    BallPhysics physics;
    juce::Colour currentColour { juce::Colour (0xff4a90d9) };
    juce::Image  surfaceTexture;
    float        currentDamping { 0.55f };

    struct Point2f { float x, y; };
    std::vector<Point2f> trail;
};

//==============================================================================
// Slim horizontal slider row with label + value readout
class ParamRow : public juce::Component
{
public:
    ParamRow (const juce::String& label, juce::RangedAudioParameter* param,
              juce::AudioProcessorValueTreeState& apvts)
        : labelText (label)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 18);
        addAndMakeVisible (slider);

        if (param)
            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>
                         (apvts, param->getParameterID(), slider);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        slider.setBounds (r.removeFromRight (getWidth() - 90));
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.setFont (11.0f);
        g.drawText (labelText, 0, 0, 88, getHeight(), juce::Justification::centredLeft);
    }

private:
    juce::String labelText;
    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

//==============================================================================
class GravitasAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    GravitasAudioProcessorEditor (GravitasAudioProcessor&);
    ~GravitasAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    GravitasAudioProcessor& audioProcessor;

    // Planet selector buttons
    std::array<juce::TextButton, Planets::Count> planetButtons;

    // Field
    PlanetFieldComponent planetField;

    // Parameter rows
    std::vector<std::unique_ptr<ParamRow>> paramRows;

    void selectPlanet (int index);
    int selectedPlanet = 2; // Earth

    // Section label y positions — set in resized(), read in paint()
    int sectionYPhysics = 0, sectionYFilter = 0, sectionYReverb = 0,
        sectionYSatTrem = 0, sectionYEcho   = 0, sectionYOutput = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GravitasAudioProcessorEditor)
};