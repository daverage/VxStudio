#pragma once

#include "../VxRebalanceTypes.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cstdint>
#include <memory>

namespace vxsuite::rebalance::ai {

class RealtimeStemSplitter {
public:
    static constexpr int kOutputChunkSize = 512;
    static constexpr int kContextSize = 1024;
    static constexpr int kInternalChunkSize = kContextSize + kOutputChunkSize + kContextSize;
    static constexpr double kModelSampleRate = 44100.0;

    struct DebugSnapshot {
        bool available = false;
        bool hasFrame = false;
        float latestConfidence = 0.0f;
        std::uint64_t submittedFrames = 0;
        std::uint64_t completedFrames = 0;
        std::uint64_t failedFrames = 0;
        std::uint64_t droppedFrames = 0;
    };

    RealtimeStemSplitter();
    ~RealtimeStemSplitter();

    RealtimeStemSplitter(const RealtimeStemSplitter&) = delete;
    RealtimeStemSplitter& operator=(const RealtimeStemSplitter&) = delete;

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    [[nodiscard]] bool isAvailable() const noexcept { return available; }
    [[nodiscard]] int latencySamples() const noexcept { return available ? kOutputChunkSize : 0; }
    [[nodiscard]] juce::String statusText() const;
    [[nodiscard]] DebugSnapshot getDebugSnapshot() const noexcept;

    [[nodiscard]] bool processBlock(const juce::AudioBuffer<float>& input,
                                    vxsuite::rebalance::AiMaskFrame& maskFrame) noexcept;
    [[nodiscard]] bool processBlock(const juce::AudioBuffer<float>& input,
                                    vxsuite::rebalance::AiMaskFrame& maskFrame,
                                    vxsuite::rebalance::StemFrame& stemFrame) noexcept;

private:
    struct Impl;

    void release();

    std::unique_ptr<Impl> impl;
    double currentSampleRate = 0.0;
    int currentMaxBlockSize = 0;
    int currentChannelCount = 0;
    bool available = false;
    juce::String unavailableReason { "HS-TasNet runtime not linked" };
};

} // namespace vxsuite::rebalance::ai
