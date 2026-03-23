# Gravitas — Signal Flow

```mermaid
graph TD
    subgraph UIThread["🖥 UI THREAD (30Hz Timer — PluginEditor::timerCallback)"]
        APVTSRead["Read APVTS Parameters\ngravity · damping · wind · ballMass"]
        BPUpdate["BallPhysics::update\n(dt=1/30s, audioRMS)"]
        BPWrite["Write ballX, ballY\nto atomics"]
    end

    subgraph AudioThread["🔊 AUDIO THREAD (processBlock)"]
        Input["Input Audio\nStereo Buffer"]
        RMSCalc["Calculate Input RMS\n→ audioRMS atomic"]
        CircWrite["CircularBuffer::write\nRecord last N bars"]

        StutterRead["Read Atomics\nballX · ballY · bpm"]
        StutterProc["StutterEngine::process\nballX → slice length\nballY → wet/dry mix"]

        FilterProc["StateVariableTPTFilter\ncutoff · resonance"]
        ReverbProc["juce::dsp::Reverb\nwetLevel · roomSize"]
        SatProc["Tanh Waveshaper\nsaturation → drive"]
        TremProc["Tremolo LFO\nrate · depth"]
        EchoProc["Echo Delay Taps\ntaps · spacing · feedback"]
        MixProc["Final Mix Gain"]
        Output["Output Audio"]
    end

    subgraph APVTSParams["⚙️ APVTS Parameters"]
        PPhysics["PHYSICS\ngravity · damping · wind · ballMass"]
        PStutter["STUTTER\nsyncToHost · reverse · bufferBars"]
        PFilter["FILTER\nfilterCutoff · filterRes"]
        PReverb["REVERB\nreverbWet · reverbDecay"]
        PSat["SATURATION"]
        PTrem["TREMOLO\ntremoloRate · tremoloDepth"]
        PEcho["ECHO\nechoTaps · echoSpacing · echoFeedback"]
        PMix["OUTPUT\nmix"]
    end

    subgraph Atomics["🔄 Lock-Free Atomics"]
        BallXY["ballX · ballY\nUI → Audio"]
        AudioRMS["audioRMS\nAudio → UI"]
    end

    %% UI thread flow
    APVTSRead --> BPUpdate
    BPUpdate --> BPWrite
    BPWrite --> BallXY

    %% Audio input
    Input --> RMSCalc
    RMSCalc --> AudioRMS
    AudioRMS -.30Hz poll.-> BPUpdate
    Input --> CircWrite

    %% Stutter
    BallXY --> StutterRead
    StutterRead --> StutterProc
    CircWrite -.read slice.-> StutterProc
    PStutter -.-> StutterProc

    %% DSP chain
    StutterProc --> FilterProc
    PFilter -.-> FilterProc
    FilterProc --> ReverbProc
    PReverb -.-> ReverbProc
    ReverbProc --> SatProc
    PSat -.-> SatProc
    SatProc --> TremProc
    PTrem -.-> TremProc
    TremProc --> EchoProc
    PEcho -.-> EchoProc
    EchoProc --> MixProc
    PMix -.-> MixProc
    MixProc --> Output

    %% Preset → physics
    PPhysics -.planet preset.-> APVTSRead

    style UIThread fill:#1a2a3a,stroke:#4a9fd9,color:#cce8ff
    style AudioThread fill:#2a1a0a,stroke:#d97a20,color:#ffe8cc
    style Atomics fill:#2a1a3a,stroke:#9a50d9,color:#ead9ff
    style APVTSParams fill:#0a2a1a,stroke:#30a060,color:#ccffe8
```

## Thread boundaries

| Bridge | Direction | Mechanism |
|---|---|---|
| `ballX`, `ballY` | UI timer → audio thread | `std::atomic<float>`, relaxed ordering |
| `audioRMS` | audio thread → UI timer | `std::atomic<float>`, relaxed ordering |
| All parameters | UI sliders → audio thread | APVTS internal atomics via `getRawParameterValue` |

## DSP chain order (processBlock)

1. **CircularBuffer write** — incoming audio recorded continuously
2. **StutterEngine** — reads a slice of the circular buffer; `ballX` sets slice length (1/32 note → 1 bar), `ballY` sets wet/dry
3. **StateVariableTPT Filter** — lowpass, cutoff + resonance from APVTS
4. **Reverb** — `juce::dsp::Reverb`, wet level + room size
5. **Saturation** — `tanh(x · drive) / tanh(drive)` soft clip
6. **Tremolo** — sine LFO amplitude modulation
7. **Echo** — up to 8 delay taps with feedback
8. **Mix** — final output gain