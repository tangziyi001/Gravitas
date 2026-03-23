#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Physics/BallPhysics.h"
#include "Physics/PlanetPresets.h"
#include <BinaryData.h>

//==============================================================================
// Visualization tuning constants — all chosen empirically for feel.
namespace VisualConst
{
    // ── Timer ────────────────────────────────────────────────────────────────
    // Physics update and repaint rate.  30 Hz is smooth enough for the ball
    // motion and avoids burning CPU on the UI thread.
    constexpr int   kTimerHz              = 30;

    // ── Ball position mapping ────────────────────────────────────────────────
    // Ball world coords [-1,1] are scaled by this before mapping to pixels,
    // so the ball never touches the panel edge even at maximum displacement.
    constexpr float kFieldScale           = 0.85f;

    // ── Ball core rendering ──────────────────────────────────────────────────
    // massScale = kMassBase + ballMass * kMassGain  →  range [1.15, 1.6] across [0.1, 2.0]
    constexpr float kMassBase             = 0.7f;
    constexpr float kMassGain             = 0.45f;

    // Motion-blur stretch: length = (kStretchLenBase + speed * kStretchLenSpeed) * massScale
    constexpr float kStretchLenBase       = 14.0f; // px at rest
    constexpr float kStretchLenSpeed      = 22.0f; // px per unit speed

    // Width shrinks as speed increases (squash-and-stretch).  Clamped to kStretchWidMin.
    constexpr float kStretchWidBase       = 14.0f;
    constexpr float kStretchWidSpeed      =  5.0f;
    constexpr float kStretchWidMin        =  7.0f; // keeps the ball visible when fast

    // ── Ball glow layers ─────────────────────────────────────────────────────
    constexpr float kGlowRadBase          = 36.0f; // outer glow radius at rest (px)
    constexpr float kGlowRadSpeed         = 28.0f; // outer glow radius added per unit speed
    constexpr float kHaloRadBase          = 18.0f; // inner halo radius at rest (px)
    constexpr float kHaloRadSpeed         = 12.0f; // inner halo radius added per unit speed

    // ── Trail ────────────────────────────────────────────────────────────────
    constexpr int   kTrailMaxPoints       = 80;    // older points are dropped when full
    constexpr float kTrailAlphaScale      = 0.7f;  // max alpha at trail tip (fades as t²)
    constexpr float kTrailRadBase         = 1.5f;  // dot radius at the oldest point (px)
    constexpr float kTrailRadTip          = 5.0f;  // extra radius added at the newest point
    constexpr float kTrailRadSpeed        = 3.0f;  // extra radius per unit ball speed

    // ── Speed rings ──────────────────────────────────────────────────────────
    // A ring is emitted whenever ringTimer (accumulated as speed * kRingSpeedGain) > 1.
    constexpr float kRingSpeedGain        = 0.4f;
    // Ring alpha at birth = min(1, speed * kRingStrengthScale)
    constexpr float kRingStrengthScale    = 0.6f;
    // Each tick the ring ages by this amount; rings are removed when age >= 1.
    constexpr float kRingAgeInc           = 0.04f; // ring lives ~25 ticks = ~0.83 s at 30 Hz
    // Ring radius in pixels = age * kRingRadiusScale
    constexpr float kRingRadiusScale      = 60.0f;

    // ── Bounce flash ─────────────────────────────────────────────────────────
    // Flash triggers when |pos| exceeds this threshold on either axis.
    constexpr float kBounceThreshold      = 0.92f;
    // Flash alpha decays by this factor each tick (multiplicative).
    constexpr float kBounceDecay          = 0.75f;

    // ── Atmospheric haze / fog ───────────────────────────────────────────────
    // Centre glow alpha = damping * kHazeAlphaScale
    constexpr float kHazeAlphaScale       = 0.65f;
    // Dense fog only appears once damping exceeds this threshold.
    constexpr float kFogThreshold         = 0.30f;
    // Fog alpha = (damping - kFogThreshold) * kFogAlphaScale
    constexpr float kFogAlphaScale        = 0.60f;

    // ── Gravity rings (field contours) ───────────────────────────────────────
    // numRings = 1 + gravity * kRingCountScale  →  range 1..10 rings
    constexpr float kRingCountScale       = 9.0f;
    // Ring base alpha = kRingAlphaMin + gravity * kRingAlphaScale  →  0.12..0.84
    constexpr float kRingAlphaMin         = 0.12f;
    constexpr float kRingAlphaScale       = 0.72f;
    // Rings are blended this far toward white so they contrast any planet texture.
    constexpr float kRingWhiteMix         = 0.55f;
    // Innermost and outermost ring radii as a fraction of the field half-size.
    constexpr float kRingScaleMin         = 0.06f;
    constexpr float kRingScaleMax         = 0.88f;
    // Alpha falls off toward outer rings by this factor.
    constexpr float kRingAlphaFalloff     = 0.60f;
    // Stroke width tapers from kRingThickInner to (kRingThickInner - kRingThickTaper).
    constexpr float kRingThickInner       = 3.0f;
    constexpr float kRingThickTaper       = 1.5f;

    // ── Wind animation on rings ──────────────────────────────────────────────
    // Max fractional scale change applied by wind breathing.
    constexpr float kWindBreathAmp        = 0.20f;
    // Per-ring phase offset.  Must be close to an integer multiple of 2π so the
    // animation loops seamlessly when windPhase wraps 0→1 via fmod.
    constexpr float kWindBreathPhaseOff   = 1.05f; // ≈ 1 × 2π

    // ── Radial field lines ───────────────────────────────────────────────────
    constexpr int   kNumFieldLines        = 12;    // evenly spaced around the field
    // Max angular deviation from wind turbulence (radians).  0.47 rad ≈ ±27°.
    constexpr float kTurbulenceAmp        = 0.47f;
    // Per-line phase offset (≈ π/2 stagger between adjacent lines).
    constexpr float kTurbulencePhaseOff   = 1.57f;
    // Max perpendicular bow displacement at mid-radius (pixels at wind = 1).
    constexpr float kBowAmpPx             = 50.0f;
    // Phase offset per dash segment along the bow animation.
    constexpr float kBowPhaseOffPerDash   = 0.70f;
    // Both turbulence and bow multiply windPhase by this integer so fmod wraps cleanly.
    constexpr float kWindAnimMultiplier   = 2.0f;  // integer → seamless 0→1 wrap

    // Field line start/end as fractions of the field radius.
    constexpr float kFieldLineInnerFrac   = 0.08f;
    constexpr float kFieldLineOuterFrac   = 0.88f;
    constexpr int   kFieldLineDashes      = 8;     // dash segments per field line
    // Fraction of each dash slot occupied by the visible segment (rest is gap).
    constexpr float kDashGapFrac          = 0.42f;

    // ── Wind phase accumulation (timerCallback) ──────────────────────────────
    // windPhase += wind * kWindPhaseWindGain + kWindPhaseBaseRate  each tick.
    // kWindPhaseBaseRate ensures slow animation even when wind = 0.
    constexpr float kWindPhaseWindGain    = 0.08f;
    constexpr float kWindPhaseBaseRate    = 0.005f;

    // ── Vignette ─────────────────────────────────────────────────────────────
    // Alpha of the dark radial gradient that frames the field panel.
    constexpr float kVignetteEdgeAlpha    = 0.65f;

    // ── Gravity tether ───────────────────────────────────────────────────────
    // Tether alpha = kTetherAlphaMin + gravity * kTetherAlphaGrav
    constexpr float kTetherAlphaMin       = 0.15f;
    constexpr float kTetherAlphaGrav      = 0.35f;
    // Tether stroke width = kTetherWidthBase + gravity * kTetherWidthGrav
    constexpr float kTetherWidthBase      = 1.2f;
    constexpr float kTetherWidthGrav      = 2.0f;
    // Anchor dot (centre) radius = kAnchorRadBase + gravity * kAnchorRadGrav
    constexpr float kAnchorRadBase        = 3.0f;
    constexpr float kAnchorRadGrav        = 5.0f;
    // Anchor is drawn slightly brighter than the tether line.
    constexpr float kAnchorAlphaBoost     = 1.5f;

    // ── Trail colour ─────────────────────────────────────────────────────────
    // Trail dots blend this far toward white at the tip (most recent point).
    constexpr float kTrailWhiteMix        = 0.4f;

    // ── Speed ring rendering ─────────────────────────────────────────────────
    constexpr float kSpeedRingAlphaFactor = 0.5f;  // alpha = strength * (1-age) * this
    constexpr float kSpeedRingStrokeWidth = 1.5f;

    // ── Boundary flash ───────────────────────────────────────────────────────
    constexpr float kBounceFlashAlpha     = 0.5f;  // flash rect alpha at full strength

    // ── Ball specular highlight ──────────────────────────────────────────────
    // Small bright ellipse offset from ball centre to simulate surface highlight.
    constexpr float kSpecularOffX         = 3.5f;  // px left of ball centre
    constexpr float kSpecularOffY         = 5.0f;  // px above ball centre
    constexpr float kSpecularW            = 6.0f;
    constexpr float kSpecularH            = 5.0f;

    // ── Field contour rendering details ──────────────────────────────────────
    // Shadow stroke is drawn at half the ring alpha to ensure contrast on bright textures.
    constexpr float kRingShadowAlphaRatio = 0.5f;
    // Shadow stroke is this many px thicker than the bright ring on top.
    constexpr float kRingShadowThickExtra = 2.0f;
    // Field line alpha as a fraction of the ring base alpha.
    constexpr float kFieldLineAlphaRatio  = 0.5f;
    // Outer dashes are faded by this fraction (alpha multiplied toward zero at outermost dash).
    constexpr float kDashAlphaFalloff     = 0.6f;
    // Arrowhead at the end of each field line is drawn slightly brighter.
    constexpr float kArrowAlphaBoost      = 1.6f;
    // Length of the arrowhead tick marks (px).
    constexpr float kArrowTickLen         = 6.0f;

    // ── Ball glow / halo / core ──────────────────────────────────────────────
    constexpr float kGlowAlpha            = 0.55f;
    constexpr float kHaloBrighter         = 0.4f;  // brighter() factor for inner halo colour
    constexpr float kHaloAlpha            = 0.75f;
    constexpr float kBallCoreBrighter     = 0.8f;  // brighter() factor for the ball core fill
    constexpr float kBallStrokeAlpha      = 0.55f; // alpha of the white outline ring
    constexpr float kSpecularAlpha        = 0.95f; // near-white specular highlight

    // ── Atmospheric fog rendering ────────────────────────────────────────────
    constexpr float kFogDarkenAmount      = 0.5f;  // darker() factor applied to fog colour
    constexpr float kFogMinAlpha          = 0.005f; // skip fog fill below this alpha (perf guard)

    // ── Boundary flash rect ──────────────────────────────────────────────────
    constexpr float kBounceFlashMin       = 0.01f; // skip drawing below this flash strength
    constexpr float kBounceFlashInset     = 2.0f;  // inset from panel edge (px)

    // ── Gravity ring scale clamps ────────────────────────────────────────────
    // Wind breath can push ring scale outside the nominal range.
    // These are hard limits to prevent rings collapsing or overflowing the field.
    constexpr float kRingScaleClampMin    = 0.04f;
    constexpr float kRingScaleClampMax    = 0.95f;

    // ── Physics HUD ──────────────────────────────────────────────────────────
    constexpr float kHudFontSize          = 10.0f;
    constexpr int   kHudWidth             = 300;
    constexpr int   kHudHeight            = 16;
    constexpr int   kHudPadX              = 6;    // gap from right panel edge (px)
    constexpr int   kHudPadY              = 4;    // gap from bottom panel edge (px)
    constexpr float kHudBgAlpha           = 0.5f;
    constexpr float kHudCornerRadius      = 3.0f;
    constexpr float kHudTextAlpha         = 0.9f;
}

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
        startTimerHz (VisualConst::kTimerHz);
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

        windPhase = std::fmod (windPhase + currentWind * VisualConst::kWindPhaseWindGain
                                         + VisualConst::kWindPhaseBaseRate, 1.0f);

        float rms = proc.audioRMS.load (std::memory_order_relaxed);
        physics.update (1.0f / VisualConst::kTimerHz, rms);

        proc.ballX.store (physics.getStutterAxis(),  std::memory_order_relaxed);
        proc.ballY.store (physics.getIntensityAxis(), std::memory_order_relaxed);

        auto s = physics.getState();

        // Detect boundary bounce (position near ±1 and velocity reversed)
        bool bounced = (std::abs (s.x) > VisualConst::kBounceThreshold
                     || std::abs (s.y) > VisualConst::kBounceThreshold);
        if (bounced && !wasBouncing)
            bounceFlash = 1.0f;
        wasBouncing = bounced;
        bounceFlash *= VisualConst::kBounceDecay;

        // Speed rings — emit a new ring when moving fast
        float spd = physics.speed();
        ringTimer += spd * VisualConst::kRingSpeedGain;
        if (ringTimer > 1.0f)
        {
            rings.push_back ({ s.x, s.y, 0.0f, juce::jmin (1.0f, spd * VisualConst::kRingStrengthScale) });
            ringTimer = 0.0f;
        }
        for (auto& r : rings) r.age += VisualConst::kRingAgeInc;
        rings.erase (std::remove_if (rings.begin(), rings.end(),
                                     [] (const Ring& r) { return r.age >= 1.0f; }),
                     rings.end());

        // Trail — store with velocity magnitude for width
        trail.push_back ({ s.x, s.y, spd });
        if (trail.size() > (size_t) VisualConst::kTrailMaxPoints) trail.erase (trail.begin());

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
            float hazeAlpha = currentDamping * VisualConst::kHazeAlphaScale;
            juce::ColourGradient haze (currentColour.withAlpha (hazeAlpha), cx, cy,
                                       juce::Colours::transparentBlack, 0.0f, 0.0f, true);
            g.setGradientFill (haze);
            g.fillRect (bounds);

            // Dense fog overlay — really packs in when atmosphere/damping is high
            float fogAlpha = juce::jmax (0.0f, currentDamping - VisualConst::kFogThreshold)
                             * VisualConst::kFogAlphaScale;
            if (fogAlpha > VisualConst::kFogMinAlpha)
            {
                g.setColour (currentColour.darker (VisualConst::kFogDarkenAmount).withAlpha (fogAlpha));
                g.fillRect (bounds);
            }
        }

        // ── Vignette ───────────────────────────────────────────────────────
        {
            juce::ColourGradient vig (juce::Colours::transparentBlack, cx, cy,
                                      juce::Colours::black.withAlpha (VisualConst::kVignetteEdgeAlpha), 0.0f, 0.0f, true);
            g.setGradientFill (vig);
            g.fillRect (bounds);
        }

        auto s = physics.getState();
        float bx = cx + s.x * rx * VisualConst::kFieldScale;
        float by = cy + s.y * ry * VisualConst::kFieldScale;
        float spd = physics.speed();

        // ── Magnetic field contours ────────────────────────────────────────
        drawFieldContour (g, cx, cy, rx, ry);

        // ── Gravity tether — line from center to ball ──────────────────────
        {
            float tAlpha = VisualConst::kTetherAlphaMin + currentGravity * VisualConst::kTetherAlphaGrav;
            g.setColour (currentColour.withAlpha (tAlpha));
            juce::Path tether;
            tether.startNewSubPath (cx, cy);
            tether.lineTo (bx, by);
            g.strokePath (tether, juce::PathStrokeType (VisualConst::kTetherWidthBase + currentGravity * VisualConst::kTetherWidthGrav,
                                                         juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));

            // Center anchor dot — larger with stronger gravity
            float anchorR = VisualConst::kAnchorRadBase + currentGravity * VisualConst::kAnchorRadGrav;
            g.setColour (currentColour.withAlpha (tAlpha * VisualConst::kAnchorAlphaBoost));
            g.fillEllipse (cx - anchorR, cy - anchorR, anchorR * 2.0f, anchorR * 2.0f);
        }

        // ── Trail ─────────────────────────────────────────────────────────
        for (int i = 1; i < (int) trail.size(); ++i)
        {
            float t     = (float) i / (float) trail.size();
            float alpha = t * t * VisualConst::kTrailAlphaScale;
            float r     = VisualConst::kTrailRadBase + t * VisualConst::kTrailRadTip
                          + trail[(size_t) i].spd * VisualConst::kTrailRadSpeed;
            float tx    = cx + trail[(size_t) i].x * rx * VisualConst::kFieldScale;
            float ty    = cy + trail[(size_t) i].y * ry * VisualConst::kFieldScale;
            // Colour shifts from planet colour toward white at the tip
            auto col = currentColour.interpolatedWith (juce::Colours::white, t * VisualConst::kTrailWhiteMix);
            g.setColour (col.withAlpha (alpha));
            g.fillEllipse (tx - r, ty - r, r * 2.0f, r * 2.0f);
        }

        // ── Speed rings ────────────────────────────────────────────────────
        for (const auto& ring : rings)
        {
            float ringR  = ring.age * VisualConst::kRingRadiusScale;
            float alpha  = ring.strength * (1.0f - ring.age) * VisualConst::kSpeedRingAlphaFactor;
            float px     = cx + ring.x * rx * VisualConst::kFieldScale;
            float py     = cy + ring.y * ry * VisualConst::kFieldScale;
            g.setColour (currentColour.withAlpha (alpha));
            g.drawEllipse (px - ringR, py - ringR, ringR * 2.0f, ringR * 2.0f, VisualConst::kSpeedRingStrokeWidth);
        }

        // ── Boundary flash ─────────────────────────────────────────────────
        if (bounceFlash > VisualConst::kBounceFlashMin)
        {
            g.setColour (currentColour.withAlpha (bounceFlash * VisualConst::kBounceFlashAlpha));
            g.drawRect (bounds.reduced (VisualConst::kBounceFlashInset), VisualConst::kBounceFlashInset);
        }

        // ── Ball glow ──────────────────────────────────────────────────────
        {
            // Outer soft glow
            float glowR = VisualConst::kGlowRadBase + spd * VisualConst::kGlowRadSpeed;
            juce::ColourGradient glow (currentColour.withAlpha (VisualConst::kGlowAlpha), bx, by,
                                       juce::Colours::transparentBlack, bx + glowR, by, false);
            g.setGradientFill (glow);
            g.fillEllipse (bx - glowR, by - glowR, glowR * 2.0f, glowR * 2.0f);

            // Inner bright halo
            float haloR = VisualConst::kHaloRadBase + spd * VisualConst::kHaloRadSpeed;
            juce::ColourGradient halo (currentColour.brighter (VisualConst::kHaloBrighter).withAlpha (VisualConst::kHaloAlpha), bx, by,
                                       juce::Colours::transparentBlack, bx + haloR, by, false);
            g.setGradientFill (halo);
            g.fillEllipse (bx - haloR, by - haloR, haloR * 2.0f, haloR * 2.0f);
        }

        // ── Ball core — stretched in direction of travel ───────────────────
        {
            // Heavier ball = visually larger core
            float massScale  = VisualConst::kMassBase + currentBallMass * VisualConst::kMassGain;
            float stretchLen = (VisualConst::kStretchLenBase + spd * VisualConst::kStretchLenSpeed) * massScale;
            float stretchW   = juce::jmax (VisualConst::kStretchWidMin,
                                           (VisualConst::kStretchWidBase - spd * VisualConst::kStretchWidSpeed) * massScale);
            float angle      = std::atan2 (s.vy, s.vx);

            juce::Path ballPath;
            ballPath.addEllipse (-stretchLen * 0.5f, -stretchW * 0.5f, stretchLen, stretchW);
            juce::AffineTransform xform = juce::AffineTransform::rotation (angle)
                                          .translated (bx, by);
            g.setColour (currentColour.brighter (VisualConst::kBallCoreBrighter));
            g.fillPath (ballPath, xform);

            // Outer white ring for crisp edge
            g.setColour (juce::Colours::white.withAlpha (VisualConst::kBallStrokeAlpha));
            g.strokePath (ballPath, juce::PathStrokeType (1.5f), xform);

            // Bright specular highlight
            g.setColour (juce::Colours::white.withAlpha (VisualConst::kSpecularAlpha));
            g.fillEllipse (bx - VisualConst::kSpecularOffX, by - VisualConst::kSpecularOffY,
                           VisualConst::kSpecularW, VisualConst::kSpecularH);
        }

        // ── Physics HUD — tiny readouts so slider changes are undeniable ───
        {
            auto fmt = [] (float v) { return juce::String (v, 2); };
            juce::String hud = "G:" + fmt (currentGravity)
                             + "  D:" + fmt (currentDamping)
                             + "  W:" + fmt (currentWind)
                             + "  M:" + fmt (currentBallMass);
            g.setFont (juce::FontOptions (VisualConst::kHudFontSize));
            int hudW = VisualConst::kHudWidth;
            int hudH = VisualConst::kHudHeight;
            int hudX = (int) bounds.getRight()  - hudW - VisualConst::kHudPadX;
            int hudY = (int) bounds.getBottom() - hudH - VisualConst::kHudPadY;
            g.setColour (juce::Colours::black.withAlpha (VisualConst::kHudBgAlpha));
            g.fillRoundedRectangle ((float) hudX - 2, (float) hudY - 1,
                                    (float) hudW + 4, (float) hudH + 2, VisualConst::kHudCornerRadius);
            g.setColour (currentColour.withAlpha (VisualConst::kHudTextAlpha));
            g.drawText (hud, hudX, hudY, hudW, hudH, juce::Justification::centredRight);
        }
    }

private:
    void drawFieldContour (juce::Graphics& g, float cx, float cy, float rx, float ry)
    {
        // ── Gravity rings — count and brightness scale clearly with gravity ─
        // At gravity=0.01 → 1 faint ring.  At gravity=1.0 → 10 bright rings.
        int   numRings  = 1 + static_cast<int> (currentGravity * VisualConst::kRingCountScale);
        float baseAlpha = VisualConst::kRingAlphaMin + currentGravity * VisualConst::kRingAlphaScale;

        // Ring colour: interpolate toward white so it always contrasts
        // against the planet texture (pure planet colour blends in)
        auto ringColour = currentColour.interpolatedWith (juce::Colours::white, VisualConst::kRingWhiteMix);

        for (int i = 1; i <= numRings; ++i)
        {
            float t     = (float) i / (float) numRings;
            float scale = VisualConst::kRingScaleMin + t * VisualConst::kRingScaleMax;

            float windBreath = currentWind * VisualConst::kWindBreathAmp
                               * std::sin (windPhase * juce::MathConstants<float>::twoPi
                                           + (float) i * VisualConst::kWindBreathPhaseOff);
            scale = juce::jlimit (VisualConst::kRingScaleClampMin, VisualConst::kRingScaleClampMax, scale + windBreath);

            float alpha = baseAlpha * (1.0f - t * VisualConst::kRingAlphaFalloff);
            float thick = VisualConst::kRingThickInner - t * VisualConst::kRingThickTaper;

            float erx = rx * scale;
            float ery = ry * scale;

            // Dark shadow stroke → ensures contrast on bright planet textures
            g.setColour (juce::Colours::black.withAlpha (alpha * VisualConst::kRingShadowAlphaRatio));
            g.drawEllipse (cx - erx, cy - ery, erx * 2.0f, ery * 2.0f, thick + VisualConst::kRingShadowThickExtra);

            // Bright ring on top
            g.setColour (ringColour.withAlpha (alpha));
            g.drawEllipse (cx - erx, cy - ery, erx * 2.0f, ery * 2.0f, thick);
        }

        // ── Radial field lines — wind bows them dramatically ───────────────
        const int numLines = VisualConst::kNumFieldLines;
        for (int i = 0; i < numLines; ++i)
        {
            float baseAngle  = (float) i / (float) numLines * juce::MathConstants<float>::twoPi;
            // Angular turbulence: at wind=1, lines deviate up to ±27°
            float turbulence = currentWind * VisualConst::kTurbulenceAmp
                               * std::sin (windPhase * juce::MathConstants<float>::twoPi * VisualConst::kWindAnimMultiplier
                                           + (float) i * VisualConst::kTurbulencePhaseOff);
            float angle = baseAngle + turbulence;
            float cosA  = std::cos (angle);
            float sinA  = std::sin (angle);
            float alpha = baseAlpha * VisualConst::kFieldLineAlphaRatio;

            const float innerFrac = VisualConst::kFieldLineInnerFrac;
            const float outerFrac = VisualConst::kFieldLineOuterFrac;
            const int   numDashes = VisualConst::kFieldLineDashes;
            for (int d = 0; d < numDashes; ++d)
            {
                float fA   = innerFrac + (outerFrac - innerFrac) * ((float) d                              / numDashes);
                float fB   = innerFrac + (outerFrac - innerFrac) * ((float)(d + VisualConst::kDashGapFrac) / numDashes);
                float fMid = (fA + fB) * 0.5f;

                // Perpendicular bow: at wind=1, max bow ~50px at mid-radius
                float bow = currentWind * VisualConst::kBowAmpPx * fMid * (1.0f - fMid)
                            * std::sin (windPhase * juce::MathConstants<float>::twoPi * VisualConst::kWindAnimMultiplier
                                        + (float)(i * 3 + d) * VisualConst::kBowPhaseOffPerDash);

                float x1 = cx + cosA * rx * fA + (-sinA) * bow;
                float y1 = cy + sinA * ry * fA + ( cosA) * bow;
                float x2 = cx + cosA * rx * fB + (-sinA) * bow;
                float y2 = cy + sinA * ry * fB + ( cosA) * bow;

                float dashAlpha = alpha * (1.0f - (float) d / numDashes * VisualConst::kDashAlphaFalloff);
                g.setColour (currentColour.withAlpha (dashAlpha));
                juce::Path dash;
                dash.startNewSubPath (x1, y1);
                dash.lineTo (x2, y2);
                g.strokePath (dash, juce::PathStrokeType (1.2f));

                if (d == numDashes - 1)
                {
                    float tickLen = VisualConst::kArrowTickLen;
                    float dx = x2 - cx, dy = y2 - cy;
                    float len = std::sqrt (dx * dx + dy * dy);
                    if (len > 0.001f) { dx /= len; dy /= len; }
                    float perpX = -dy * tickLen * 0.5f;
                    float perpY =  dx * tickLen * 0.5f;
                    juce::Path tick;
                    tick.startNewSubPath (x2 - dx * tickLen - perpX, y2 - dy * tickLen - perpY);
                    tick.lineTo (x2, y2);
                    tick.lineTo (x2 - dx * tickLen + perpX, y2 - dy * tickLen + perpY);
                    g.setColour (currentColour.withAlpha (dashAlpha * VisualConst::kArrowAlphaBoost));
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