#pragma once
#include <JuceHeader.h>

// Lock-free stereo circular buffer for recording incoming audio.
// Written on the audio thread only; read by StutterEngine on the same thread.
class CircularBuffer
{
public:
    CircularBuffer() = default;

    void prepare (int numChannels, int maxSamples)
    {
        buffer.setSize (numChannels, maxSamples, false, true, false);
        capacity = maxSamples;
        writePos = 0;
    }

    void write (const juce::AudioBuffer<float>& src, int numSamples)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            int srcCh = juce::jmin (ch, src.getNumChannels() - 1);
            const float* in = src.getReadPointer (srcCh);

            for (int i = 0; i < numSamples; ++i)
            {
                buffer.setSample (ch, (writePos + i) % capacity, in[i]);
            }
        }
        writePos = (writePos + numSamples) % capacity;
    }

    // Read a sample at a given position (wrapping). position in [0, capacity).
    float read (int channel, int position) const
    {
        return buffer.getSample (channel, position % capacity);
    }

    int getCapacity() const { return capacity; }
    int getWritePos() const { return writePos; }

private:
    juce::AudioBuffer<float> buffer;
    int capacity = 0;
    int writePos = 0;
};