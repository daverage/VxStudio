#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

namespace vxsuite {
namespace speech_clarity {

// Split-band de-esser: bandpass-extracts the sibilance region (5.5 kHz, Q=1.5),
// applies a soft-knee compressor to that band only, and subtracts the gain-reduced
// band from the original — leaving the rest of the spectrum untouched.
// Fast attack (1 ms) / slow release (100 ms) envelope ensures the onset is caught.
class DeEsserDsp {
public:
    struct Params {
        float strength = 0.0f;            // 0-1: ratio and max-reduction depth
        float detectionIntensity = 0.0f;  // 0-1: scales effect depth
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void process(juce::AudioBuffer<float>& buffer, const Params& params);

private:
    struct BiquadState {
        float b0=1,b1=0,b2=0,a1=0,a2=0;
        float x1=0,x2=0,y1=0,y2=0;
    };
    struct ChannelState {
        BiquadState bp;
        float env = 0.0f;
    };

    static void  designBandPass(double sr, float hz, float q, BiquadState&) noexcept;
    static float processBiquad(float x, BiquadState&) noexcept;

    std::vector<ChannelState> channels;
    double sampleRate  = 48000.0;
    float  attCoeff    = 0.0f;
    float  relCoeff    = 0.0f;
    float  thresholdLin = 0.0f;
};

} // namespace speech_clarity
} // namespace vxsuite
