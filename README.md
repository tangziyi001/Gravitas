# Gravitas

A physics-driven stutter sampler plugin for VST3 and AU. A ball moves through a gravitational field — its radial distance from the centre continuously controls how tightly the audio is stuttered. Every planet preset loads a distinct physical character, and the ball's trajectory shapes the sound in real time without manual automation.

**Developer:** ZetaSonic
**Formats:** VST3 · AU
**Platform:** macOS

---

## How it works

The plugin continuously records incoming audio into a circular buffer (the *capture window*). A physics-simulated ball moves in a 2D gravitational field. Its position is sampled 30 times per second and mapped to the stutter engine:

| Ball state | Effect |
|---|---|
| **Radial distance from centre** | Stutter repeat interval — centre = 1/32 bar (rapid chop), edge = 1 bar (slow loop) |
| **Atmosphere × distance** | Output gain — ball near centre with high atmosphere fades the signal toward silence; moving outward recovers gain |

The ball is pulled toward the centre by a spring (gravity), slowed by atmospheric drag (atmosphere), and buffeted by random impulses (wind). Audio loudness also kicks the ball outward. The result is an ever-changing, organic stutter pattern that breathes with the music.

### Stutter model

```
Repeat interval = 4 beats × (60s / bpm) × pow(1/32, 1 − dist)
```

- `dist = 0` (centre) → interval = 1/32 bar → 32 repeats per bar
- `dist = 1` (edge)   → interval = 1 full bar → single pass through the window
- Intervals are **continuous** — no grid snapping — giving a smooth "gravity pull" feel

### Window and gain model

The capture window holds a fixed slice of audio (size = **Capture Bars**) and only refreshes every **Reset Bars** bars. Between resets the stutter loops the same material. This separates *how often* fresh audio is captured from *how fast* the stutter cycles.

Output gain tracks `dist` as its target via an exponential smoother controlled by **Atmosphere**:

```
alpha = 1 − exp(−atmosphere × 30 / sampleRate)
gain += alpha × (dist − gain)   // each sample
```

At `atmosphere = 0`, gain never changes. At high atmosphere, gain collapses quickly as the ball falls inward and recovers when it drifts outward.

---

## Signal flow

```
Input
  │
  ├─► CircularBuffer (write)
  │
  └─► StutterEngine (read window at stutter rate)
        │
        ▼
      SVFilter (lowpass)
        │
        ▼
      Reverb
        │
        ▼
      Tanh Waveshaper (saturation)
        │
        ▼
      Tremolo LFO
        │
        ▼
      Echo Delay Taps
        │
        ▼
      Dry/Wet Blend  ←── Original input (dry copy)
        │
        ▼
      Output
```

### Thread safety

| Bridge | Direction | Mechanism |
|---|---|---|
| `ballX`, `ballY` | UI timer → audio thread | `std::atomic<float>`, relaxed ordering |
| `audioRMS` | audio thread → UI timer | `std::atomic<float>`, relaxed ordering |
| `displayGain`, `displayIntervalMs`, stutter state | audio thread → waveform display | `std::atomic`, relaxed ordering |
| All parameters | UI sliders ↔ audio thread | APVTS internal atomics via `getRawParameterValue` |

---

## Planet Presets

Each planet loads an artistic interpretation of its physical properties as sound parameters. Click a planet button to switch preset. Parameters can be adjusted freely after loading.

| Planet | Character | Notable features |
|---|---|---|
| ☿ **Mercury** | Bright, relentless, perpetual bounce | Zero damping, no reverb, very bright filter (12 kHz) |
| ♀ **Venus** | Slow, smothered, reversed | High damping, reverse playback, crushing reverb (75% wet, 6 s) |
| ♁ **Earth** | Balanced, musical | Moderate everything — a good starting point |
| ♂ **Mars** | Sparse, cold, drifty | Very low gravity and damping, dark filter (2.5 kHz), 2 echo taps |
| ♃ **Jupiter** | Chaotic, massive | Very high gravity + wind (rapid bouncing), heavy saturation, 8 echo taps |
| ♄ **Saturn** | Ringed, spacious, resonant | High resonance filter (Q = 3.0), majestic reverb (65%, 5 s), 8 echo taps |
| ⛢ **Uranus** | Sideways, turbulent | Reverse playback, high wind, very cold filter (800 Hz) |
| ♆ **Neptune** | Maximum chaos, darkest | Maximum wind, very low damping, darkest filter (500 Hz) |

---

## Parameters

### PHYSICS
Control the ball's movement, which drives everything else in real time.

| Parameter | Range | What it does |
|---|---|---|
| **Gravity** | 0.01 – 1.0 | Spring force pulling the ball toward the centre. Higher = tighter oscillation. Visualised as the count and brightness of contour rings. |
| **Atmosphere** | 0.0 – 0.99 | Velocity damping and gain-response rate. Higher = ball settles near centre faster (signal fades); lower = ball drifts freely (signal stays loud). Visualised as a dense fog overlay. |
| **Wind** | 0.0 – 1.0 | Random impulse applied to the ball each tick. Higher = chaotic, unpredictable trajectory. Visualised as turbulence in the radial field lines. |
| **Ball Mass** | 0.1 – 2.0 | Inertia. Heavier balls change direction more slowly. Visualised as the physical size of the ball. |

### STUTTER

| Parameter | Range | What it does |
|---|---|---|
| **Capture Bars** | 1 – 8 | Size of the audio window that gets looped. The stutter engine reads within this many bars of audio. |
| **Reset Bars** | 1 – 8 | How often the capture window advances to the latest audio (in bars). Longer = window stays on older material for longer before jumping forward. |
| **Sync Window** | on/off | When on, each window reset aligns its start to the nearest beat boundary from the host transport. |

### FILTER
State-variable lowpass filter applied after stuttering.

| Parameter | Range | What it does |
|---|---|---|
| **Cutoff** | 200 – 18 000 Hz | Lowpass cutoff frequency. |
| **Resonance** | 0.1 – 4.0 | Filter Q. High values add a resonant peak at the cutoff (Saturn uses Q = 3 for a ring-like character). |

### REVERB

| Parameter | Range | What it does |
|---|---|---|
| **Reverb Wet** | 0.0 – 1.0 | Blend between dry and reverb. |
| **Reverb Decay** | 0.1 – 8.0 s | Reverb tail length, mapped to room size internally. |

### SATURATION / TREMOLO

| Parameter | Range | What it does |
|---|---|---|
| **Saturation** | 0.0 – 1.0 | Tanh soft-clip drive. 0 = clean, 1 = heavily clipped. |
| **Tremolo Rate** | 0.01 – 4.0 Hz | Amplitude LFO speed. |
| **Tremolo Depth** | 0.0 – 1.0 | Amplitude LFO depth. 0 = no tremolo. |

### ECHO
Up to 8 delay taps with decaying feedback.

| Parameter | Range | What it does |
|---|---|---|
| **Echo Taps** | 0 – 8 | Number of delay taps. 0 = no echo. |
| **Echo Spacing** | 0.125 – 1.0 beats | Time between taps. 0.125 = 1/8-note, 1.0 = 1 bar apart. |
| **Echo Feedback** | 0.0 – 0.8 | Tap decay. Higher = longer echo tail. |

### OUTPUT

| Parameter | Range | What it does |
|---|---|---|
| **Mix** | 0.0 – 1.0 | Dry/wet blend. 0 = original audio pass-through, 1 = fully processed signal. |

---

## Field Visualisation

The left panel shows the gravitational field and ball in real time.

| Visual element | Represents |
|---|---|
| Concentric rings | Equipotential contours. Count and brightness scale with **Gravity**. |
| Radial dashed lines | Field lines. They bow and deviate with **Wind**. |
| Coloured fog | Atmospheric density. Thickens with **Atmosphere**. |
| Ball size | Scales with **Ball Mass**. |
| Ball glow + trail | Ball speed — faster = longer trail and larger glow. |
| Gravity tether | Line from centre to ball; thickness scales with gravity. |
| Speed rings | Emitted when the ball moves fast; expand outward and fade. |
| Boundary flash | Pulse when the ball hits the ±1 boundary walls. |
| HUD (bottom-right) | Live audio-state readout: `dist` · `ivl` (ms) · `gain` |

The strip below the field visualises the circular buffer waveform, the current capture window (highlighted), and a live playhead showing where in the window playback is reading.

---

## Building

Requires JUCE installed at `~/JUCE` and Xcode command-line tools.

```bash
git clone <repo>
cd Gravitas
cmake -B build
cmake --build build --config Release
```

Output bundles:
- `build/Gravitas_artefacts/Release/VST3/Gravitas.vst3`
- `build/Gravitas_artefacts/Release/AU/Gravitas.component`

Install:
```bash
cp -r build/Gravitas_artefacts/Release/VST3/Gravitas.vst3 ~/Library/Audio/Plug-Ins/VST3/
cp -r build/Gravitas_artefacts/Release/AU/Gravitas.component ~/Library/Audio/Plug-Ins/Components/
```

---

## Project structure

```
Gravitas/
├── Source/
│   ├── PluginProcessor.h/cpp      — AudioProcessor, APVTS, DSP chain
│   ├── PluginEditor.h/cpp         — UI: planet field, waveform strip, param panel
│   ├── Physics/
│   │   ├── BallPhysics.h          — Spring-damper simulation (UI thread)
│   │   └── PlanetPresets.h        — 8 planet preset definitions
│   └── DSP/
│       ├── CircularBuffer.h/cpp   — Lock-free stereo ring buffer
│       └── StutterEngine.h        — Repeat-interval stutter with crossfade
├── Assets/
│   └── Textures/                  — Planet surface JPEGs (CC BY 4.0, solarsystemscope.com)
└── CMakeLists.txt
```

### Constants organisation

| Namespace | Location | Covers |
|---|---|---|
| `PhysicsConst` | `Physics/BallPhysics.h` | Spring, wind, bounce, audio-kick constants |
| `StutterConst` | `DSP/StutterEngine.h` | Crossfade, interval mapping, gain-response rate |
| `VisualConst` | `PluginEditor.h` | All rendering tuning values (~60 constants) |
| `UILayoutConst` | `PluginEditor.h` | Editor window dimensions, panel layout, colour alphas |
| *(anonymous)* | `PluginProcessor.cpp` | BPM fallback, buffer sizes, DSP scaling |

---

## License

MIT — see `LICENSE` for details.
Planet textures: [solarsystemscope.com](https://www.solarsystemscope.com/textures/) (CC BY 4.0).
