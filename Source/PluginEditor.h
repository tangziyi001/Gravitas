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
        currentPlanetIndex = idx;
        physics.setPlanet (Planets::All[idx]);
        currentColour = Planets::All[idx].colour;
        loadTexture (idx);
        startTimerHz (30);
    }

    void setPlanet (int index)
    {
        currentPlanetIndex = index;
        physics.setPlanet (Planets::All[index]);
        currentColour = Planets::All[index].colour;
        loadTexture (index);
        repaint();
    }

    void timerCallback() override
    {
        // Live-read physics params so sliders update visuals immediately
        currentGravity  = *proc.apvts.getRawParameterValue ("gravity");
        currentDamping  = *proc.apvts.getRawParameterValue ("damping");
        currentWind     = *proc.apvts.getRawParameterValue ("wind");
        currentBallMass = *proc.apvts.getRawParameterValue ("ballMass");

        // Also push live values into physics sim (so the ball responds while dragging)
        {
            PlanetPreset live = Planets::All[currentPlanetIndex];
            live.gravity  = currentGravity;
            live.damping  = currentDamping;
            live.wind     = currentWind;
            live.ballMass = currentBallMass;
            physics.setPlanet (live);
        }

        windPhase = std::fmod (windPhase + currentWind * 0.08f + 0.005f, 1.0f);

        float rms = proc.audioRMS.load (std::memory_order_relaxed);
        physics.update (1.0f / 30.0f, rms);

        proc.ballX.store (physics.getStutterAxis(),  std::memory_order_relaxed);
        proc.ballY.store (physics.getIntensityAxis(), std::memory_order_relaxed);

        auto s = physics.getState();

        // Detect boundary bounce (position near ±1 and velocity reversed)
        bool bounced = (std::abs (s.x) > 0.92f || std::abs (s.y) > 0.92f);
        if (bounced && !wasBouncing)
            bounceFlash = 1.0f;
        wasBouncing = bounced;
        bounceFlash *= 0.75f; // decay

        // Speed rings — emit a new ring when moving fast
        float spd = physics.speed();
        ringTimer += spd * 0.4f;
        if (ringTimer > 1.0f)
        {
            rings.push_back ({ s.x, s.y, 0.0f, juce::jmin (1.0f, spd * 0.6f) });
            ringTimer = 0.0f;
        }
        for (auto& r : rings) r.age += 0.04f;
        rings.erase (std::remove_if (rings.begin(), rings.end(),
                                     [] (const Ring& r) { return r.age >= 1.0f; }),
                     rings.end());

        // Trail — store with velocity magnitude for width
        trail.push_back ({ s.x, s.y, spd });
        if (trail.size() > 80) trail.erase (trail.begin());

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        float cx = bounds.getCentreX();
        float cy = bounds.getCentreY();
        float rx = bounds.getWidth()  * 0.5f;
        float ry = bounds.getHeight() * 0.5f;

        // ── Planet surface texture ─────────────────────────────────────────
        if (surfaceTexture.isValid())
            g.drawImage (surfaceTexture, bounds, juce::RectanglePlacement::fillDestination);
        else
            g.fillAll (juce::Colour (0xff0a0a12));

        // ── Atmospheric haze — grows thick with high damping ───────────────
        {
            // Centre glow
            float hazeAlpha = currentDamping * 0.65f;
            juce::ColourGradient haze (currentColour.withAlpha (hazeAlpha), cx, cy,
                                       juce::Colours::transparentBlack, 0.0f, 0.0f, true);
            g.setGradientFill (haze);
            g.fillRect (bounds);

            // Dense fog overlay — really packs in when atmosphere/damping is high
            float fogAlpha = juce::jmax (0.0f, currentDamping - 0.3f) * 0.6f;
            if (fogAlpha > 0.005f)
            {
                g.setColour (currentColour.darker (0.5f).withAlpha (fogAlpha));
                g.fillRect (bounds);
            }
        }

        // ── Vignette ───────────────────────────────────────────────────────
        {
            juce::ColourGradient vig (juce::Colours::transparentBlack, cx, cy,
                                      juce::Colours::black.withAlpha (0.65f), 0.0f, 0.0f, true);
            g.setGradientFill (vig);
            g.fillRect (bounds);
        }

        auto s = physics.getState();
        float bx = cx + s.x * rx * 0.85f;
        float by = cy + s.y * ry * 0.85f;
        float spd = physics.speed();

        // ── Magnetic field contours ────────────────────────────────────────
        drawFieldContour (g, cx, cy, rx, ry);

        // ── Gravity tether — line from center to ball ──────────────────────
        {
            float tAlpha = 0.15f + currentGravity * 0.35f;
            g.setColour (currentColour.withAlpha (tAlpha));
            juce::Path tether;
            tether.startNewSubPath (cx, cy);
            tether.lineTo (bx, by);
            g.strokePath (tether, juce::PathStrokeType (1.2f + currentGravity * 2.0f,
                                                         juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));

            // Center anchor dot — larger with stronger gravity
            float anchorR = 3.0f + currentGravity * 5.0f;
            g.setColour (currentColour.withAlpha (tAlpha * 1.5f));
            g.fillEllipse (cx - anchorR, cy - anchorR, anchorR * 2.0f, anchorR * 2.0f);
        }

        // ── Trail ─────────────────────────────────────────────────────────
        for (int i = 1; i < (int) trail.size(); ++i)
        {
            float t     = (float) i / (float) trail.size();
            float alpha = t * t * 0.7f;
            float r     = 1.5f + t * 5.0f + trail[(size_t) i].spd * 3.0f;
            float tx    = cx + trail[(size_t) i].x * rx * 0.85f;
            float ty    = cy + trail[(size_t) i].y * ry * 0.85f;
            // Colour shifts from planet colour toward white at the tip
            auto col = currentColour.interpolatedWith (juce::Colours::white, t * 0.4f);
            g.setColour (col.withAlpha (alpha));
            g.fillEllipse (tx - r, ty - r, r * 2.0f, r * 2.0f);
        }

        // ── Speed rings ────────────────────────────────────────────────────
        for (const auto& ring : rings)
        {
            float ringR  = (ring.age * 60.0f);
            float alpha  = ring.strength * (1.0f - ring.age) * 0.5f;
            float px     = cx + ring.x * rx * 0.85f;
            float py     = cy + ring.y * ry * 0.85f;
            g.setColour (currentColour.withAlpha (alpha));
            g.drawEllipse (px - ringR, py - ringR, ringR * 2.0f, ringR * 2.0f, 1.5f);
        }

        // ── Boundary flash ─────────────────────────────────────────────────
        if (bounceFlash > 0.01f)
        {
            g.setColour (currentColour.withAlpha (bounceFlash * 0.5f));
            g.drawRect (bounds.reduced (2.0f), 2.0f);
        }

        // ── Ball glow ──────────────────────────────────────────────────────
        {
            // Outer soft glow
            float glowR = 36.0f + spd * 28.0f;
            juce::ColourGradient glow (currentColour.withAlpha (0.55f), bx, by,
                                       juce::Colours::transparentBlack, bx + glowR, by, false);
            g.setGradientFill (glow);
            g.fillEllipse (bx - glowR, by - glowR, glowR * 2.0f, glowR * 2.0f);

            // Inner bright halo
            float haloR = 18.0f + spd * 12.0f;
            juce::ColourGradient halo (currentColour.brighter (0.4f).withAlpha (0.75f), bx, by,
                                       juce::Colours::transparentBlack, bx + haloR, by, false);
            g.setGradientFill (halo);
            g.fillEllipse (bx - haloR, by - haloR, haloR * 2.0f, haloR * 2.0f);
        }

        // ── Ball core — stretched in direction of travel ───────────────────
        {
            // Heavier ball = visually larger core
            float massScale  = 0.7f + currentBallMass * 0.45f;
            float stretchLen = (14.0f + spd * 22.0f) * massScale;
            float stretchW   = juce::jmax (7.0f, (14.0f - spd * 5.0f) * massScale);
            float angle      = std::atan2 (s.vy, s.vx);

            juce::Path ballPath;
            ballPath.addEllipse (-stretchLen * 0.5f, -stretchW * 0.5f, stretchLen, stretchW);
            juce::AffineTransform xform = juce::AffineTransform::rotation (angle)
                                          .translated (bx, by);
            g.setColour (currentColour.brighter (0.8f));
            g.fillPath (ballPath, xform);

            // Outer white ring for crisp edge
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.strokePath (ballPath, juce::PathStrokeType (1.5f), xform);

            // Bright specular highlight
            g.setColour (juce::Colours::white.withAlpha (0.95f));
            g.fillEllipse (bx - 3.5f, by - 5.0f, 6.0f, 5.0f);
        }

        // ── Physics HUD — tiny readouts so slider changes are undeniable ───
        {
            auto fmt = [] (float v) { return juce::String (v, 2); };
            juce::String hud = "G:" + fmt (currentGravity)
                             + "  D:" + fmt (currentDamping)
                             + "  W:" + fmt (currentWind)
                             + "  M:" + fmt (currentBallMass);
            g.setFont (juce::FontOptions (10.0f));
            int hudW = 300, hudH = 16;
            int hudX = (int) bounds.getRight() - hudW - 6;
            int hudY = (int) bounds.getBottom() - hudH - 4;
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle ((float) hudX - 2, (float) hudY - 1,
                                    (float) hudW + 4, (float) hudH + 2, 3.0f);
            g.setColour (currentColour.withAlpha (0.9f));
            g.drawText (hud, hudX, hudY, hudW, hudH, juce::Justification::centredRight);
        }
    }

private:
    void drawFieldContour (juce::Graphics& g, float cx, float cy, float rx, float ry)
    {
        // ── Gravity rings — count and brightness scale clearly with gravity ─
        // At gravity=0.01 → 1 faint ring.  At gravity=1.0 → 10 bright rings.
        int   numRings  = 1 + static_cast<int> (currentGravity * 9.0f);   // 1..10
        float baseAlpha = 0.12f + currentGravity * 0.72f;                  // 0.12..0.84

        // Ring colour: interpolate toward white so it always contrasts
        // against the planet texture (pure planet colour blends in)
        auto ringColour = currentColour.interpolatedWith (juce::Colours::white, 0.55f);

        for (int i = 1; i <= numRings; ++i)
        {
            float t     = (float) i / (float) numRings;
            float scale = 0.06f + t * 0.82f;

            float windBreath = currentWind * 0.20f
                               * std::sin (windPhase * juce::MathConstants<float>::twoPi
                                           + (float) i * 1.05f);
            scale = juce::jlimit (0.04f, 0.95f, scale + windBreath);

            float alpha = baseAlpha * (1.0f - t * 0.60f);
            float thick = 3.0f - t * 1.5f;

            float erx = rx * scale;
            float ery = ry * scale;

            // Dark shadow stroke → ensures contrast on bright planet textures
            g.setColour (juce::Colours::black.withAlpha (alpha * 0.5f));
            g.drawEllipse (cx - erx, cy - ery, erx * 2.0f, ery * 2.0f, thick + 2.0f);

            // Bright ring on top
            g.setColour (ringColour.withAlpha (alpha));
            g.drawEllipse (cx - erx, cy - ery, erx * 2.0f, ery * 2.0f, thick);
        }

        // ── Radial field lines — wind bows them dramatically ───────────────
        const int numLines = 12;
        for (int i = 0; i < numLines; ++i)
        {
            float baseAngle  = (float) i / (float) numLines * juce::MathConstants<float>::twoPi;
            // Angular turbulence: at wind=1, lines deviate up to ±27°
            float turbulence = currentWind * 0.47f
                               * std::sin (windPhase * juce::MathConstants<float>::twoPi * 2.0f
                                           + (float) i * 1.57f);
            float angle = baseAngle + turbulence;
            float cosA  = std::cos (angle);
            float sinA  = std::sin (angle);
            float alpha = baseAlpha * 0.50f;

            const float innerFrac = 0.08f;
            const float outerFrac = 0.88f;
            const int   numDashes = 8;
            for (int d = 0; d < numDashes; ++d)
            {
                float fA   = innerFrac + (outerFrac - innerFrac) * ((float) d         / numDashes);
                float fB   = innerFrac + (outerFrac - innerFrac) * ((float)(d + 0.42f) / numDashes);
                float fMid = (fA + fB) * 0.5f;

                // Perpendicular bow: at wind=1, max bow ~50px at mid-radius
                float bow = currentWind * 50.0f * fMid * (1.0f - fMid)
                            * std::sin (windPhase * juce::MathConstants<float>::twoPi * 2.0f
                                        + (float)(i * 3 + d) * 0.7f);

                float x1 = cx + cosA * rx * fA + (-sinA) * bow;
                float y1 = cy + sinA * ry * fA + ( cosA) * bow;
                float x2 = cx + cosA * rx * fB + (-sinA) * bow;
                float y2 = cy + sinA * ry * fB + ( cosA) * bow;

                float dashAlpha = alpha * (1.0f - (float) d / numDashes * 0.6f);
                g.setColour (currentColour.withAlpha (dashAlpha));
                juce::Path dash;
                dash.startNewSubPath (x1, y1);
                dash.lineTo (x2, y2);
                g.strokePath (dash, juce::PathStrokeType (1.2f));

                if (d == numDashes - 1)
                {
                    float tickLen = 6.0f;
                    float dx = x2 - cx, dy = y2 - cy;
                    float len = std::sqrt (dx * dx + dy * dy);
                    if (len > 0.001f) { dx /= len; dy /= len; }
                    float perpX = -dy * tickLen * 0.5f;
                    float perpY =  dx * tickLen * 0.5f;
                    juce::Path tick;
                    tick.startNewSubPath (x2 - dx * tickLen - perpX, y2 - dy * tickLen - perpY);
                    tick.lineTo (x2, y2);
                    tick.lineTo (x2 - dx * tickLen + perpX, y2 - dy * tickLen + perpY);
                    g.setColour (currentColour.withAlpha (dashAlpha * 1.6f));
                    g.strokePath (tick, juce::PathStrokeType (1.2f));
                }
            }
        }
    }

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
    }

    GravitasAudioProcessor& proc;
    BallPhysics physics;
    juce::Colour currentColour { juce::Colour (0xff4a90d9) };
    juce::Image  surfaceTexture;
    int          currentPlanetIndex { 2 };

    // Live-read each timer tick from APVTS so sliders drive visuals immediately
    float currentGravity  { 0.45f };
    float currentDamping  { 0.55f };
    float currentWind     { 0.12f };
    float currentBallMass { 1.0f  };
    float windPhase       { 0.0f  };

    struct TrailPoint { float x, y, spd; };
    std::vector<TrailPoint> trail;

    struct Ring { float x, y, age, strength; };
    std::vector<Ring> rings;

    float bounceFlash { 0.0f };
    bool  wasBouncing { false };
    float ringTimer   { 0.0f };
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
    int sectionYPhysics = 0, sectionYStutter = 0, sectionYFilter = 0,
        sectionYReverb  = 0, sectionYSatTrem = 0, sectionYEcho   = 0,
        sectionYOutput  = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GravitasAudioProcessorEditor)
};