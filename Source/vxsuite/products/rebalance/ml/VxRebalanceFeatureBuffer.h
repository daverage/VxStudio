#pragma once

#include "../dsp/VxRebalanceDsp.h"

#include <array>

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite::rebalance::ml {

struct FeatureSnapshot {
    float rms = 0.0f;
    float crest = 1.0f;
    float monoScore = 0.0f;
    std::array<float, 16> bandEnergy {};
};

class FeatureBuffer {
public:
    void prepare(double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;
    FeatureSnapshot analyseBlock(const juce::AudioBuffer<float>& buffer, int numSamples) noexcept;

private:
    double sampleRateHz = 48000.0;
    int blockSize = 0;
};

} // namespace vxsuite::rebalance::ml
