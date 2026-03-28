#pragma once
#include <JuceHeader.h>
#include "DSP/CircularBuffer.h"
#include "DSP/StutterEngine.h"
#include "Physics/PlanetPresets.h"

class GravitasAudioProcessor : public juce::AudioProcessor
{
public:
    GravitasAudioProcessor();
    ~GravitasAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return JucePlugin_Name; }
    bool   acceptsMidi()  const override { return false; }
    bool   producesMidi() const override { return false; }
    bool   isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    //==============================================================================
    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Called from UI thread — sets physics engine to new planet
    void setPlanet (int index);
    int  getCurrentPlanetIndex() const { return currentPlanetIndex; }

    // Atomic bridge: UI timer writes ball position, audio thread reads it
    std::atomic<float> ballX { 0.0f };
    std::atomic<float> ballY { 0.0f };

    // Atomic bridge: audio thread writes RMS, UI timer reads it
    std::atomic<float> audioRMS { 0.0f };

    // Atomic bridge: audio thread writes stutter/buffer state, waveform display reads it
    std::atomic<int>   displaySliceStart   { 0 };
    std::atomic<int>   displayReadPos      { 0 };
    std::atomic<int>   displaySliceSamples { 4096 };
    std::atomic<int>   displayWritePos     { 0 };
    std::atomic<int>   displayCapacity     { 0 };
    // Stutter audio-state for HUD
    std::atomic<float> displayGain        { 1.0f }; // current output gain [0,1]
    std::atomic<int>   displayIntervalMs  { 0 };    // current stutter interval in ms

    // Read-only access to the circular buffer for waveform display (UI thread)
    const CircularBuffer& getCircularBuffer() const { return circularBuffer; }

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    CircularBuffer            circularBuffer;
    StutterEngine             stutterEngine;
    juce::AudioBuffer<float>  dryBuffer; // pre-processing copy for dry/wet blend

    // DSP processors
    juce::dsp::StateVariableTPTFilter<float> svFilter;
    juce::dsp::Reverb                        reverb;
    juce::dsp::WaveShaper<float>             saturator;

    // Tremolo LFO state
    float tremoloPhase = 0.0f;

    // Echo delay lines (one per tap, stereo)
    std::vector<juce::dsp::DelayLine<float>> echoLines;
    static constexpr int MaxEchoTaps = 8;

    int  currentPlanetIndex = 2; // Earth default
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GravitasAudioProcessor)
};