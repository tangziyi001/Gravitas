#include "PluginProcessor.h"
#include "PluginEditor.h"

GravitasAudioProcessor::GravitasAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    // Pre-allocate echo lines
    echoLines.resize (MaxEchoTaps);
}

GravitasAudioProcessor::~GravitasAudioProcessor() {}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
GravitasAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Physics
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("gravity",       "Gravity",       0.01f, 1.0f,  0.45f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("damping",       "Damping",       0.0f,  0.99f, 0.55f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("wind",          "Wind",          0.0f,  1.0f,  0.12f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("ballMass",      "Ball Mass",     0.1f,  2.0f,  1.0f));

    // Stutter
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("syncToHost",    "Sync to Host",  true));
    params.push_back (std::make_unique<juce::AudioParameterBool>  ("reverse",       "Reverse",       false));
    params.push_back (std::make_unique<juce::AudioParameterInt>   ("bufferBars",    "Buffer Bars",   1, 8, 2));

    // Filter
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("filterCutoff",  "Filter Cutoff", 200.0f, 18000.0f, 4000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("filterRes",     "Resonance",     0.1f,   4.0f,     1.2f));

    // Reverb
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("reverbWet",     "Reverb Wet",    0.0f,  1.0f,  0.30f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("reverbDecay",   "Reverb Decay",  0.1f,  8.0f,  2.0f));

    // Saturation
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("saturation",    "Saturation",    0.0f,  1.0f,  0.15f));

    // Tremolo
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("tremoloRate",   "Tremolo Rate",  0.01f, 4.0f,  0.35f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("tremoloDepth",  "Tremolo Depth", 0.0f,  1.0f,  0.25f));

    // Echo
    params.push_back (std::make_unique<juce::AudioParameterInt>   ("echoTaps",      "Echo Taps",     0, 8,   1));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("echoSpacing",   "Echo Spacing",  0.125f, 1.0f, 0.25f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("echoFeedback",  "Echo Feedback", 0.0f,  0.8f,  0.20f));

    // Mix
    params.push_back (std::make_unique<juce::AudioParameterFloat> ("mix",           "Mix",           0.0f,  1.0f,  0.70f));

    return { params.begin(), params.end() };
}

//==============================================================================
void GravitasAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    int bufferBars = static_cast<int> (*apvts.getRawParameterValue ("bufferBars"));
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) bpm = *b;
    }
    int bufferSamples = static_cast<int> (sampleRate * (60.0 / bpm) * 4.0 * bufferBars);
    circularBuffer.prepare (2, bufferSamples);
    stutterEngine.prepare (sampleRate, samplesPerBlock);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };

    svFilter.prepare (spec);
    svFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);

    reverb.prepare (spec);

    saturator.prepare (spec);
    saturator.functionToUse = [] (float x)
    {
        return std::tanh (x); // soft-clip
    };

    int maxDelaySamples = static_cast<int> (sampleRate * 2.0);
    for (auto& line : echoLines)
    {
        line.setMaximumDelayInSamples (maxDelaySamples); // must be before prepare
        line.prepare (spec);
    }

    prepared = true;
}

void GravitasAudioProcessor::releaseResources() {}

//==============================================================================
void GravitasAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (!prepared) return;

    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // Compute RMS of input for audio-reactive ball kick
    float rms = 0.0f;
    for (int ch = 0; ch < totalNumInputChannels; ++ch)
        rms += buffer.getRMSLevel (ch, 0, buffer.getNumSamples());
    rms /= juce::jmax (1, totalNumInputChannels);
    audioRMS.store (rms, std::memory_order_relaxed);

    // Record into circular buffer
    circularBuffer.write (buffer, buffer.getNumSamples());

    // Read params
    double bpm = 120.0;
    bool   hostSynced = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) { bpm = *b; hostSynced = true; }
        }
    }

    float bx      = ballX.load (std::memory_order_relaxed);
    float by      = ballY.load (std::memory_order_relaxed);
    bool  sync    = static_cast<bool> (*apvts.getRawParameterValue ("syncToHost")) && hostSynced;
    bool  rev     = static_cast<bool> (*apvts.getRawParameterValue ("reverse"));

    // --- Stutter ---
    stutterEngine.process (buffer, circularBuffer, bx, by, bpm, sync, rev);

    // --- Filter ---
    float cutoff = *apvts.getRawParameterValue ("filterCutoff");
    float res    = *apvts.getRawParameterValue ("filterRes");
    svFilter.setCutoffFrequency (cutoff);
    svFilter.setResonance (res);
    {
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        svFilter.process (ctx);
    }

    // --- Reverb ---
    {
        juce::Reverb::Parameters rp;
        rp.wetLevel   = *apvts.getRawParameterValue ("reverbWet");
        float decayVal = static_cast<float> (*apvts.getRawParameterValue ("reverbDecay"));
        rp.roomSize   = juce::jmap (decayVal, 0.1f, 8.0f, 0.0f, 1.0f);
        rp.damping    = 0.5f;
        rp.dryLevel   = 1.0f - rp.wetLevel;
        reverb.setParameters (rp);
        juce::dsp::AudioBlock<float> block (buffer);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        reverb.process (ctx);
    }

    // --- Saturation ---
    {
        float drive = 1.0f + *apvts.getRawParameterValue ("saturation") * 8.0f;
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                data[i] = std::tanh (data[i] * drive) / std::tanh (drive);
        }
    }

    // --- Tremolo ---
    {
        float rate  = *apvts.getRawParameterValue ("tremoloRate");
        float depth = *apvts.getRawParameterValue ("tremoloDepth");
        float phaseInc = rate / static_cast<float> (getSampleRate());
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float lfo = 1.0f - depth * 0.5f * (1.0f + std::sin (juce::MathConstants<float>::twoPi * tremoloPhase));
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.getWritePointer(ch)[i] *= lfo;
            tremoloPhase = std::fmod (tremoloPhase + phaseInc, 1.0f);
        }
    }

    // --- Echo taps ---
    {
        int   taps     = static_cast<int> (*apvts.getRawParameterValue ("echoTaps"));
        float spacing  = *apvts.getRawParameterValue ("echoSpacing");   // beats
        float feedback = *apvts.getRawParameterValue ("echoFeedback");
        float beatSamples = static_cast<float> (getSampleRate() * 60.0 / bpm);

        for (int t = 0; t < taps && t < MaxEchoTaps; ++t)
        {
            float delaySamples = beatSamples * spacing * (t + 1);
            float tapGain = std::pow (feedback, static_cast<float> (t + 1));
            echoLines[(size_t) t].setDelay (delaySamples);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                {
                    float delayed = echoLines[(size_t) t].popSample (ch);
                    echoLines[(size_t) t].pushSample (ch, data[i]);
                    data[i] += delayed * tapGain;
                }
            }
        }
    }

    // --- Final mix (already blended in stutter, but apply overall mix) ---
    float mix = *apvts.getRawParameterValue ("mix");
    buffer.applyGain (mix);
}

//==============================================================================
void GravitasAudioProcessor::setPlanet (int index)
{
    if (index < 0 || index >= Planets::Count) return;
    currentPlanetIndex = index;

    const auto& p = Planets::All[index];

    // Push preset values into APVTS (smoothly interpolated by host automation)
    auto set = [&] (const char* id, float val)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (val));
    };

    set ("gravity",      p.gravity);
    set ("damping",      p.damping);
    set ("wind",         p.wind);
    set ("ballMass",     p.ballMass);
    set ("filterCutoff", p.filterCutoff);
    set ("filterRes",    p.filterResonance);
    set ("reverbWet",    p.reverbWet);
    set ("reverbDecay",  p.reverbDecay);
    set ("saturation",   p.saturation);
    set ("tremoloRate",  p.tremoloRate);
    set ("tremoloDepth", p.tremoloDepth);
    set ("echoSpacing",  p.echoSpacing);
    set ("echoFeedback", p.echoFeedback);

    if (auto* param = apvts.getParameter ("echoTaps"))
        param->setValueNotifyingHost (param->convertTo0to1 ((float) p.echoTaps));
    if (auto* param = apvts.getParameter ("reverse"))
        param->setValueNotifyingHost (p.reversePlayback ? 1.0f : 0.0f);
}

//==============================================================================
juce::AudioProcessorEditor* GravitasAudioProcessor::createEditor()
{
    return new GravitasAudioProcessorEditor (*this);
}

//==============================================================================
void GravitasAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void GravitasAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GravitasAudioProcessor();
}