#pragma once
#include <JuceHeader.h>

struct PlanetPreset
{
    juce::String name;
    juce::Colour colour;

    // Physics (ball behaviour)
    float gravity;          // spring pull toward center [0.01 - 1.0]
    float damping;          // velocity bleed per frame  [0.0  - 0.99]
    float wind;             // random impulse magnitude  [0.0  - 1.0]
    float ballMass;         // inertia                   [0.1  - 2.0]

    // Stutter
    bool  reversePlayback;  // retrograde rotation
    bool  swapAxes;         // Uranus: Y drives rate, X drives intensity

    // Filter
    float filterCutoff;     // Hz [200 - 18000]
    float filterResonance;  // [0.1 - 4.0]

    // Reverb
    float reverbWet;        // [0.0 - 1.0]
    float reverbDecay;      // seconds [0.1 - 8.0]

    // Saturation
    float saturation;       // [0.0 - 1.0]

    // Tremolo
    float tremoloRate;      // Hz [0.01 - 4.0]
    float tremoloDepth;     // [0.0 - 1.0]

    // Echo
    int   echoTaps;         // [0 - 8]
    float echoSpacing;      // beats [0.125 - 1.0]
    float echoFeedback;     // [0.0 - 0.8]
};

namespace Planets
{
    inline constexpr int Count = 8;

    inline const PlanetPreset Mercury {
        "Mercury",
        juce::Colour (0xffb5b5b5),
        // physics: moderate spring, zero damping → perpetual bouncing
        0.35f, 0.0f, 0.0f, 0.8f,
        // stutter
        false, false,
        // filter: scorching hot → very bright
        12000.f, 1.4f,
        // reverb: no atmosphere → bone dry
        0.0f, 0.3f,
        // saturation: weak magnetic
        0.05f,
        // tremolo: extremely slow rotation
        0.02f, 0.1f,
        // echo: no moons
        0, 0.25f, 0.0f
    };

    inline const PlanetPreset Venus {
        "Venus",
        juce::Colour (0xffe8c97a),
        // physics: high damping → slow lazy oscillation; low wind
        0.35f, 0.88f, 0.15f, 1.2f,
        // stutter: retrograde rotation
        true, false,
        // filter: hot but smothered
        5000.f, 2.2f,
        // reverb: crushing dense atmosphere
        0.75f, 6.0f,
        // saturation: no magnetic field
        0.0f,
        // tremolo: very slow, reversed feel
        0.03f, 0.3f,
        // echo: no moons
        0, 0.25f, 0.0f
    };

    inline const PlanetPreset Earth {
        "Earth",
        juce::Colour (0xff4a90d9),
        // physics: balanced
        0.45f, 0.55f, 0.12f, 1.0f,
        false, false,
        4000.f, 1.2f,
        0.30f, 2.0f,
        0.15f,
        0.35f, 0.25f,
        1, 0.25f, 0.2f
    };

    inline const PlanetPreset Mars {
        "Mars",
        juce::Colour (0xffc1440e),
        // physics: low spring + very low damping → long lazy drifts
        0.15f, 0.08f, 0.10f, 0.9f,
        false, false,
        // filter: cold → darker
        2500.f, 1.1f,
        // reverb: barely any atmosphere
        0.10f, 1.0f,
        0.05f,
        0.30f, 0.15f,
        // echo: 2 close moons
        2, 0.125f, 0.15f
    };

    inline const PlanetPreset Jupiter {
        "Jupiter",
        juce::Colour (0xffc88b3a),
        // physics: very high spring, high wind chaos
        0.85f, 0.60f, 0.80f, 1.8f,
        false, false,
        // filter: cold dark cloud tops
        1500.f, 1.8f,
        // reverb: dense gas giant
        0.55f, 4.0f,
        // saturation: enormous magnetic field
        0.80f,
        // tremolo: fast rotation
        1.80f, 0.45f,
        // echo: 8 taps (capped), close spacing
        8, 0.125f, 0.50f
    };

    inline const PlanetPreset Saturn {
        "Saturn",
        juce::Colour (0xffe4d191),
        // physics: powerful but slightly looser than Jupiter; very high wind
        0.70f, 0.62f, 0.85f, 1.6f,
        false, false,
        // filter: colder than Jupiter
        1000.f, 3.0f,   // high resonance for ring-like comb character
        // reverb: majestic, spacious
        0.65f, 5.0f,
        0.50f,
        1.60f, 0.40f,
        // echo: 8 taps, wider spread
        8, 0.25f, 0.55f
    };

    inline const PlanetPreset Uranus {
        "Uranus",
        juce::Colour (0xff7de8e8),
        // physics: moderate; high wind
        0.40f, 0.50f, 0.70f, 1.1f,
        // retrograde + sideways axes
        true, true,
        // filter: very cold
        800.f, 1.5f,
        0.55f, 4.0f,
        0.30f,
        // tremolo: slow but reversed feel
        0.25f, 0.35f,
        5, 0.25f, 0.35f
    };

    inline const PlanetPreset Neptune {
        "Neptune",
        juce::Colour (0xff3f54ba),
        // physics: moderate spring, low damping, MAXIMUM wind
        0.42f, 0.20f, 1.0f, 1.0f,
        false, false,
        // filter: coldest → darkest
        500.f, 1.3f,
        0.50f, 3.5f,
        0.25f,
        0.55f, 0.30f,
        4, 0.25f, 0.30f
    };

    inline const PlanetPreset All[Count] = {
        Mercury, Venus, Earth, Mars, Jupiter, Saturn, Uranus, Neptune
    };
}