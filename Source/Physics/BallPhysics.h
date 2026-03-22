#pragma once
#include <JuceHeader.h>
#include "PlanetPresets.h"

// Runs on the UI timer thread (never the audio thread).
// Ball position is published to audio thread via atomics in PluginProcessor.
class BallPhysics
{
public:
    struct State
    {
        float x = 0.0f;   // [-1, 1]  left/right
        float y = 0.0f;   // [-1, 1]  top/bottom
        float vx = 0.0f;
        float vy = 0.0f;
    };

    BallPhysics() = default;

    // Call once when preset changes; smoothly lerps params over transitionSec
    void setPlanet (const PlanetPreset& preset)
    {
        targetGravity  = preset.gravity;
        targetDamping  = preset.damping;
        targetWind     = preset.wind;
        targetBallMass = preset.ballMass;
        swapAxes       = preset.swapAxes;
    }

    // dt in seconds; audioRMS in [0,1] — called from a 30fps timer
    void update (float dt, float audioRMS)
    {
        // Smoothly interpolate toward target planet params
        float alpha = 1.0f - std::exp (-dt * 4.0f);
        gravity  += alpha * (targetGravity  - gravity);
        damping  += alpha * (targetDamping  - damping);
        wind     += alpha * (targetWind     - wind);
        ballMass += alpha * (targetBallMass - ballMass);

        // Spring force toward center (gravity)
        float ax = -gravity * state.x / ballMass;
        float ay = -gravity * state.y / ballMass;

        // Random wind impulses
        if (wind > 0.001f)
        {
            ax += wind * (random.nextFloat() * 2.0f - 1.0f) * 0.08f;
            ay += wind * (random.nextFloat() * 2.0f - 1.0f) * 0.08f;
        }

        // Audio-reactive kick — louder input = bigger shove
        float kickStrength = audioRMS * 0.4f;
        ax += kickStrength * (random.nextFloat() * 2.0f - 1.0f);
        ay += kickStrength * (random.nextFloat() * 2.0f - 1.0f);

        // Integrate velocity
        state.vx = (state.vx + ax * dt) * (1.0f - damping * dt * 60.0f);
        state.vy = (state.vy + ay * dt) * (1.0f - damping * dt * 60.0f);

        // Integrate position
        state.x += state.vx * dt;
        state.y += state.vy * dt;

        // Elastic boundary [-1, 1]
        if (state.x >  1.0f) { state.x =  1.0f; state.vx *= -0.6f; }
        if (state.x < -1.0f) { state.x = -1.0f; state.vx *= -0.6f; }
        if (state.y >  1.0f) { state.y =  1.0f; state.vy *= -0.6f; }
        if (state.y < -1.0f) { state.y = -1.0f; state.vy *= -0.6f; }
    }

    // Returns position with axes swapped if planet requires it (Uranus)
    float getStutterAxis() const { return swapAxes ? state.y : state.x; }
    float getIntensityAxis() const { return swapAxes ? state.x : state.y; }

    const State& getState() const { return state; }

    float speed() const
    {
        return std::sqrt (state.vx * state.vx + state.vy * state.vy);
    }

private:
    State state;
    juce::Random random;
    bool swapAxes = false;

    float gravity  = 0.45f, targetGravity  = 0.45f;
    float damping  = 0.55f, targetDamping  = 0.55f;
    float wind     = 0.12f, targetWind     = 0.12f;
    float ballMass = 1.0f,  targetBallMass = 1.0f;
};