#pragma once
#include <JuceHeader.h>
#include <limits>
#include "CircularBuffer.h"

//==============================================================================
// Stutter engine tuning constants.
namespace StutterConst
{
    // Crossfade window applied at every cycle boundary to prevent clicks.
    constexpr double kCrossfadeSec      = 0.005; // 5 ms

    // Hard floor on the repeat interval in samples.
    constexpr int    kMinIntervalSamples = 64;

    // Hard floor on the effective capture window passed in from the processor.
    constexpr int    kMinCapSamples     = 256;

    // Assumes 4/4 time: one bar = 4 beats.
    constexpr double kBeatsPerBar       = 4.0;

    // Repeat interval mapping: interval = 1bar * pow(kMinIntervalFrac, 1 - dist).
    // At dist=0 (centre) → 1/32 bar.  At dist=1 (edge) → 1 bar.
    // 1/32 = 0.03125 (one thirty-second note).
    constexpr double kMinIntervalFrac   = 0.03125;

    // Bandwidth (Hz) of the gain-smoothing filter.
    // currentGain tracks distance-from-centre as its target, approaching it at a rate
    // set by atmosphere.  Higher rate = faster response to ball movement.
    // alpha per sample = 1 - exp(-atmosphere * kGainResponseRate / sampleRate).
    constexpr float  kGainResponseRate  = 30.0f;
}

// Stutter engine — repeat-interval model.
//
// The radial distance of the ball from centre controls how often the captured
// window is restarted (centre = tight stutter, edge = plays full window once).
// Atmosphere drives an exponential gain decay within each cycle so the repeated
// output fades like a delay echo.  All methods run on the audio thread.
class StutterEngine
{
public:
    StutterEngine() = default;

    int   getWindowStart()      const { return windowStart; }
    int   getPlayPos()          const { return playPos; }
    int   getIntervalSamples()  const { return currentIntervalSamples; }
    float getCurrentGain()      const { return currentGain; }

    // Aliases used by PluginProcessor display atomics
    int   getSliceStart()   const { return windowStart; }
    int   getReadPos()      const { return playPos; }
    int   getSliceSamples() const { return currentIntervalSamples; }

    void prepare (double sampleRate, int samplesPerBlock)
    {
        sr = sampleRate;
        blockSize = samplesPerBlock;
        crossfadeSamples = static_cast<int> (sampleRate * StutterConst::kCrossfadeSec);
    }

    // ballX, ballY in [-1, 1] — only radial distance is used.
    // bpm — used to convert the interval fraction to samples (host BPM or 120 fallback).
    // reverse — play each repeated window backwards.
    // syncWindow — when true, each new window is snapped to the last beat boundary.
    // beatPhaseSamples — samples since the last beat at the block start (from host ppqPosition).
    // resetWindowSamples — how far back the window jumps on each cycle restart (resetBars × samplesPerBar).
    // atmosphere in [0, 0.99] — gain smoothing rate; 0 = no gain change regardless of position.
    // effectiveCapacitySamples — total capture window in samples (bufferBars × samplesPerBar);
    //                            used to clamp the repeat interval.
    void process (juce::AudioBuffer<float>& buffer,
                  CircularBuffer& circBuf,
                  float  ballX,
                  float  ballY,
                  double bpm,
                  bool   reverse,
                  bool   syncWindow,
                  int    beatPhaseSamples,
                  int    resetWindowSamples,
                  float  atmosphere,
                  int    effectiveCapacitySamples)
    {
        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        const double usedBpm        = (bpm > 0.0) ? bpm : 120.0;
        const double secondsPerBeat = 60.0 / usedBpm;

        const int cap = juce::jmax (StutterConst::kMinCapSamples,
                                    juce::jmin (effectiveCapacitySamples,
                                                circBuf.getCapacity()));

        // ── Repeat interval ────────────────────────────────────────────────
        // Radial distance [0, 1]: centre → shortest interval, edge → full window.
        const float dist = juce::jlimit (0.0f, 1.0f,
                                          std::sqrt (ballX * ballX + ballY * ballY));
        const float intervalSec = static_cast<float> (
            secondsPerBeat * StutterConst::kBeatsPerBar
            * std::pow (StutterConst::kMinIntervalFrac, 1.0f - dist));
        // Interval is always continuous — no grid snap.  This gives the "gravity"
        // feeling where stuttering intensifies smoothly as the ball falls inward.
        const int newInterval = juce::jlimit (StutterConst::kMinIntervalSamples, cap,
                                              static_cast<int> (intervalSec * sr));

        // ── Per-sample gain smoothing ──────────────────────────────────────
        // currentGain continuously tracks `dist` as its target.
        // When the ball is near the centre (dist ≈ 0), gain fades toward silence.
        // When the ball moves to the edge (dist ≈ 1), gain recovers toward unity.
        // atmosphere=0 → alpha=0 → currentGain never moves (no effect at all).
        // Higher atmosphere → faster response to ball position changes.
        const float alpha = 1.0f - std::exp (
            -atmosphere * StutterConst::kGainResponseRate / static_cast<float> (sr));

        const int bufCap = circBuf.getCapacity();

        for (int i = 0; i < numSamples; ++i)
        {
            // ── Window reset clock (independent of stutter cycle) ──────────
            // Fires every resetWindowSamples — grabs the latest `cap` samples
            // of audio and holds them until the next reset.
            if (resetCounter >= resetWindowSamples)
            {
                resetCounter = 0;
                if (syncWindow)
                {
                    int lastBeatWP = ((circBuf.getWritePos() - beatPhaseSamples) % bufCap + bufCap) % bufCap;
                    windowStart    = ((lastBeatWP - cap) % bufCap + bufCap) % bufCap;
                }
                else
                {
                    windowStart = ((circBuf.getWritePos() - cap) % bufCap + bufCap) % bufCap;
                }
            }
            ++resetCounter;

            // ── Stutter cycle restart ──────────────────────────────────────
            // Loops within the fixed window; does NOT move windowStart.
            if (playPos >= currentIntervalSamples)
            {
                playPos = 0;
                currentIntervalSamples = newInterval;
                // currentGain is NOT reset — continuous state tracking dist.
            }

            // ── Smooth gain toward target (dist) ───────────────────────────
            currentGain += alpha * (dist - currentGain);

            // ── Read from circular buffer ──────────────────────────────────
            const int sampleOffset = reverse ? (currentIntervalSamples - 1 - playPos)
                                             : playPos;
            const int circIdx = (windowStart + sampleOffset) % circBuf.getCapacity();

            // ── Crossfade envelope at cycle boundaries (prevents clicks) ───
            float env = currentGain;
            if (crossfadeSamples > 0)
            {
                if (playPos < crossfadeSamples)
                    env *= static_cast<float> (playPos) / static_cast<float> (crossfadeSamples);
                else if (playPos > currentIntervalSamples - crossfadeSamples)
                    env *= static_cast<float> (currentIntervalSamples - playPos)
                           / static_cast<float> (crossfadeSamples);
            }

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample (ch, i, circBuf.read (ch, circIdx) * env);

            ++playPos;
        }
    }

private:
    double sr               = 44100.0;
    int    blockSize        = 512;
    int    crossfadeSamples = 220;

    int    windowStart            = 0;
    int    playPos                = 0;
    int    currentIntervalSamples = 4096;
    float  currentGain            = 1.0f;
    // Initialised to a large value so the window snaps on the very first process call
    // rather than waiting one full reset period before producing any output.
    int    resetCounter           = std::numeric_limits<int>::max();
};
