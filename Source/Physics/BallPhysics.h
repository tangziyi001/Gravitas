#pragma once
#include <JuceHeader.h>
#include "PlanetPresets.h"

//==============================================================================
// Physics tuning constants — all chosen empirically for feel.
// Adjust these to change how the ball moves without touching the equations.
namespace PhysicsConst
{
    // Controls how quickly physics parameters (gravity, damping, wind, mass) lerp
    // toward a new preset.  Passed as the exponent rate to exp(-dt * k).
    // At 30 Hz (dt ≈ 0.033 s), alpha ≈ 0.125 → ~80% of the way in ~0.5 s.
    constexpr float kParamSmooth       = 4.0f;

    // Normalises the spring force so the free oscillation period is roughly 2–3 s
    // at the default gravity of 0.45.  Derived empirically; increase to tighten.
    constexpr float kSpringScale       = 12.0f;

    // Scales the random wind impulse added to acceleration each tick.
    // At wind = 1 the ball receives up to ±kWindImpulseScale units per axis.
    constexpr float kWindImpulseScale  = 2.5f;

    // Scales the audio-reactive kick.  At full-scale RMS = 1 the ball gets a
    // ±kAudioKickStrength push, making loud transients visibly shake the ball.
    constexpr float kAudioKickStrength = 3.0f;

    // Coefficient of restitution on boundary walls: fraction of velocity
    // retained after a bounce.  0.6 = 60% elastic — feels physical without
    // the ball sticking or amplifying indefinitely.
    constexpr float kBounceRestitution = 0.6f;
}

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

    BallPhysics()
    {
        // Start off-center so the spring force has something to act on
        state.x  =  0.35f;
        state.y  =  0.15f;
        state.vx =  0.0f;
        state.vy =  0.2f;
    }

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
        float alpha = 1.0f - std::exp (-dt * PhysicsConst::kParamSmooth);
        gravity  += alpha * (targetGravity  - gravity);
        damping  += alpha * (targetDamping  - damping);
        wind     += alpha * (targetWind     - wind);
        ballMass += alpha * (targetBallMass - ballMass);

        // Spring force toward center — scaled so oscillation period is ~2-3s
        float ax = -gravity * state.x / ballMass * PhysicsConst::kSpringScale;
        float ay = -gravity * state.y / ballMass * PhysicsConst::kSpringScale;

        // Random wind impulses
        if (wind > 0.001f)
        {
            ax += wind * (random.nextFloat() * 2.0f - 1.0f) * PhysicsConst::kWindImpulseScale;
            ay += wind * (random.nextFloat() * 2.0f - 1.0f) * PhysicsConst::kWindImpulseScale;
        }

        // Audio-reactive kick — louder input = bigger shove
        float kickStrength = audioRMS * PhysicsConst::kAudioKickStrength;
        ax += kickStrength * (random.nextFloat() * 2.0f - 1.0f);
        ay += kickStrength * (random.nextFloat() * 2.0f - 1.0f);

        // Integrate velocity — fixed damping: was multiplying by *60 which
        // gave a negative factor (~-0.09) and killed all motion instantly
        state.vx = (state.vx + ax * dt) * (1.0f - damping * dt);
        state.vy = (state.vy + ay * dt) * (1.0f - damping * dt);

        // Integrate position
        state.x += state.vx * dt;
        state.y += state.vy * dt;

        // Elastic boundary [-1, 1]
        if (state.x >  1.0f) { state.x =  1.0f; state.vx *= -PhysicsConst::kBounceRestitution; }
        if (state.x < -1.0f) { state.x = -1.0f; state.vx *= -PhysicsConst::kBounceRestitution; }
        if (state.y >  1.0f) { state.y =  1.0f; state.vy *= -PhysicsConst::kBounceRestitution; }
        if (state.y < -1.0f) { state.y = -1.0f; state.vy *= -PhysicsConst::kBounceRestitution; }
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