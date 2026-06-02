#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

namespace vxsuite {
namespace speech_clarity {

// Level-relative breath gate: applies smooth gain reduction when the external
// detector signals a quiet, broadband passage. Reduction scales linearly with
// detectionIntensity and strength (max ~8 dB at full settings).
// Attack 10 ms / release 80 ms prevents pumping artefacts.
class DeBreathDsp {
public:
    struct Params {
        float strength = 0.0f;            // 0-1: reduction depth
        float detectionIntensity = 0.0f;  // 0-1: scales reduction depth
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void process(juce::AudioBuffer<float>& buffer, const Params& params);

private:
    struct ChannelState {
        float currentGain = 1.0f;
    };

    std::vector<ChannelState> channels;
    double sampleRate = 48000.0;
    float  attCoeff   = 0.0f;
    float  relCoeff   = 0.0f;
};

} // namespace speech_clarity
} // namespace vxsuite
