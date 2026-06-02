#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

namespace vxsuite {
namespace speech_clarity {

// Frequency-selective plosive gate: LP-filters the signal at 180 Hz (where plosive
// bursts concentrate), tracks the LP-band envelope, and subtracts the gated LP
// component from the original — leaving the rest of the spectrum untouched.
// Fast attack (2 ms), medium release (40 ms). The gate depth is driven by
// detectionIntensity from the external crest-factor detector.
class DePolosiveDsp {
public:
    struct Params {
        float strength = 0.0f;            // 0-1: max gate depth (up to -12 dB on LP band)
        float detectionIntensity = 0.0f;  // 0-1: scales gate depth
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void process(juce::AudioBuffer<float>& buffer, const Params& params);

private:
    struct ChannelState {
        float lpZ1 = 0.0f, lpZ2 = 0.0f;
        float env      = 0.0f;
        float gateGain = 1.0f;
    };

    std::vector<ChannelState> channels;
    double sampleRate = 48000.0;
    float  lpB0=1, lpB1=0, lpB2=0, lpA1=0, lpA2=0;
    float  attCoeff = 0.0f;
    float  relCoeff = 0.0f;
};

} // namespace speech_clarity
} // namespace vxsuite
