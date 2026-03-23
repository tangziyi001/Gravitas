#pragma once
#include <JuceHeader.h>
#include "CircularBuffer.h"

//==============================================================================
// Stutter engine tuning constants.
namespace StutterConst
{
    // Crossfade window applied at every slice boundary to prevent clicks.
    constexpr double kCrossfadeSec     = 0.005; // 5 ms

    // Hard floor on slice length in samples.  Below this we'd effectively read
    // a single sample on repeat, which sounds like a tone rather than stutter.
    constexpr int    kMinSliceSamples  = 64;

    // Hard floor on the effective capture window passed in from the processor.
    // Prevents divide-by-zero and pathological buffer reads.
    constexpr int    kMinCapSamples    = 256;

    // Assumes 4/4 time: one bar = 4 beats.  Used to convert ballX → slice length.
    constexpr double kBeatsPerBar      = 4.0;

    // The slice length mapping is: sliceSec = 1bar * pow(kMinSliceFraction, |ballX|).
    // At |ballX|=0 (centre) → 1 bar.  At |ballX|=1 (edge) → 1/32 bar.
    // 1/32 = 0.03125, which equals one thirty-second note subdivision.
    constexpr double kMinSliceFraction = 0.03125; // 1/32 note
}

// Reads slices from a CircularBuffer and produces stutter output.
// All methods called on the audio thread.
class StutterEngine
{
public:
    StutterEngine() = default;

    void prepare (double sampleRate, int samplesPerBlock)
    {
        sr = sampleRate;
        blockSize = samplesPerBlock;
        crossfadeSamples = static_cast<int> (sampleRate * StutterConst::kCrossfadeSec);
    }

    // ballX in [-1,1]: edges = tiny slices (1/32), center = 1 bar
    // ballY in [-1,1]: top = 100% wet, bottom = 0% wet
    // bpm: host tempo (0 = free-running, use default 120)
    // effectiveCapacitySamples: how far back to read (bars * samplesPerBar)
    void process (juce::AudioBuffer<float>& buffer,
                  CircularBuffer& circBuf,
                  float ballX, float ballY,
                  double bpm,
                  bool   syncToHost,
                  bool   reverse,
                  int    effectiveCapacitySamples)
    {
        float wet   = (ballY + 1.0f) * 0.5f;   // remap [-1,1] → [0,1]
        float dry   = 1.0f - wet;

        double usedBpm = (bpm > 0.0) ? bpm : 120.0;
        double secondsPerBeat = 60.0 / usedBpm;

        int effectiveCap = juce::jmax (StutterConst::kMinCapSamples,
                                       juce::jmin (effectiveCapacitySamples,
                                                   circBuf.getCapacity()));

        // Map |ballX| → slice length in samples
        // |ballX|: 0=center(long), 1=edge(short)
        float t = std::abs (ballX);            // [0,1]
        // Exponential mapping: short slices near edges feel more intense
        float sliceSec = static_cast<float> (secondsPerBeat * StutterConst::kBeatsPerBar
                                             * std::pow (StutterConst::kMinSliceFraction, t));
        int sliceSamples = juce::jmax (StutterConst::kMinSliceSamples,
                                       juce::jmin (static_cast<int> (sliceSec * sr),
                                                         effectiveCap));

        if (syncToHost)
            sliceSamples = juce::jmin (snapToGrid (sliceSamples, secondsPerBeat), effectiveCap);

        // If slice changed, start new slice from current write head position
        if (sliceSamples != currentSliceSamples)
        {
            currentSliceSamples = sliceSamples;
            sliceStart = (circBuf.getWritePos() - sliceSamples + circBuf.getCapacity())
                         % circBuf.getCapacity();
            readPos = 0;
            crossfadePos = 0;
        }

        int numSamples = buffer.getNumSamples();
        int numChannels = buffer.getNumChannels();

        for (int i = 0; i < numSamples; ++i)
        {
            // Compute read index into circular buffer
            int sampleOffset = reverse ? (currentSliceSamples - 1 - readPos) : readPos;
            int circIdx = (sliceStart + sampleOffset) % circBuf.getCapacity();

            // Crossfade gain at slice boundaries
            float gain = 1.0f;
            if (readPos < crossfadeSamples)
                gain = static_cast<float> (readPos) / crossfadeSamples;
            else if (readPos > currentSliceSamples - crossfadeSamples)
                gain = static_cast<float> (currentSliceSamples - readPos) / crossfadeSamples;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                float drySample  = buffer.getSample (ch, i);
                float wetSample  = circBuf.read (ch, circIdx) * gain;
                buffer.setSample (ch, i, dry * drySample + wet * wetSample);
            }

            // Advance read position, loop within slice
            readPos = (readPos + 1) % currentSliceSamples;
        }
    }

private:
    // Snap slice length to nearest musical subdivision
    int snapToGrid (int rawSamples, double secondsPerBeat) const
    {
        // Subdivisions: 1/32, 1/16, 1/8, 1/4, 1/2, 1 bar
        static const float subdivisions[] = { 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
        float rawBeats = static_cast<float> (rawSamples) / static_cast<float> (sr * secondsPerBeat);
        float best = subdivisions[0];
        for (float sub : subdivisions)
        {
            if (std::abs (sub - rawBeats) < std::abs (best - rawBeats))
                best = sub;
        }
        return static_cast<int> (best * secondsPerBeat * sr);
    }

    double sr        = 44100.0;
    int    blockSize = 512;
    int    crossfadeSamples    = 220;
    int    currentSliceSamples = 4096;
    int    sliceStart = 0;
    int    readPos    = 0;
    int    crossfadePos = 0;
};