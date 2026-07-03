#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

namespace vxsuite::rebalance::ai {

constexpr int kStemBackendChannelCount = 2;
constexpr int kStemBackendStemCount = 4;

enum StemBackendStem {
    stemDrums = 0,
    stemBass = 1,
    stemVocals = 2,
    stemOther = 3
};

class IStemSeparationBackend {
public:
    using ChannelBuffers = std::array<std::vector<float>, kStemBackendChannelCount>;
    using StemBuffers = std::array<ChannelBuffers, kStemBackendStemCount>;

    virtual ~IStemSeparationBackend() = default;

    virtual bool prepare(double sampleRate,
                         int outputChunkSize,
                         int contextSize,
                         int numChannels,
                         juce::String& error) = 0;

    virtual void reset() = 0;

    virtual bool processFrame(const ChannelBuffers& context,
                              const ChannelBuffers& input,
                              const ChannelBuffers& lowFreq,
                              float normalizationGain,
                              StemBuffers& outputStems,
                              juce::String& error) = 0;

    [[nodiscard]] virtual juce::String backendName() const = 0;
    [[nodiscard]] virtual juce::String statusText() const = 0;
    [[nodiscard]] virtual double lastProcessMs() const noexcept = 0;
    [[nodiscard]] virtual double averageProcessMs() const noexcept = 0;
    [[nodiscard]] virtual double maxProcessMs() const noexcept = 0;
};

} // namespace vxsuite::rebalance::ai
