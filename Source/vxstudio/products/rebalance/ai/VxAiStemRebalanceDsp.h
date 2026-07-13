#pragma once

#include "../VxRebalanceTypes.h"

#include <array>

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite::rebalance::ai {

class StemRebalanceDsp {
public:
    using DebugSnapshot = vxsuite::rebalance::DebugSnapshot;
    using RecordingType = vxsuite::rebalance::RecordingType;

    static constexpr int kSourceCount = vxsuite::rebalance::kSourceCount;
    static constexpr int kControlCount = vxsuite::rebalance::kControlCount;
    static constexpr int kDebugBins = vxsuite::rebalance::kDebugBins;

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void setControlTargets(const std::array<float, kControlCount>& normalizedValues);
    void setRecordingType(RecordingType newType) noexcept;
    void setStemFrame(const StemFrame& frame) noexcept;
    void process(juce::AudioBuffer<float>& buffer);

    [[nodiscard]] int latencySamples() const noexcept { return kStemFrameSamples; }
    [[nodiscard]] DebugSnapshot getDebugSnapshot() const noexcept;
    [[nodiscard]] bool hasUsableFrame() const noexcept { return queueCount > 0; }

private:
    [[nodiscard]] float recordingTrustScale() const noexcept;
    [[nodiscard]] float sourcePreserveBlend(int source) const noexcept;
    void publishDebugFrame() noexcept;

    static constexpr int kStemQueueSize = 8;
    static constexpr int kBoundaryFadeSamples = 64;
    double sampleRateHz = 48000.0;
    int preparedChannels = 0;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, kControlCount> controlSmoothers;
    std::array<float, kControlCount> currentControlValues {};
    std::array<float, kControlCount> targetControlValues {};
    RecordingType recordingType = RecordingType::studio;
    std::array<StemFrame, kStemQueueSize> stemQueue {};
    int queueReadIndex = 0;
    int queueWriteIndex = 0;
    int queueCount = 0;
    std::uint64_t lastAcceptedSequence = 0;
    std::uint64_t renderedSequence = 0;
    double frameReadPosition = 0.0;
    float lastOutputSample[kStemFrameChannels] {};
    float boundaryStartSample[kStemFrameChannels] {};
    int boundaryFadeRemaining = 0;
    DebugSnapshot debugSnapshot {};
};

} // namespace vxsuite::rebalance::ai
