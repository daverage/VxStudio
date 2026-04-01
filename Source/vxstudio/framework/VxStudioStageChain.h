#pragma once

#include "VxStudioAudioProcessStage.h"

#include <array>

namespace vxsuite {

template <size_t StageCount>
class StageChain {
public:
    StageChain() = default;

    template <typename... Stages>
    explicit StageChain(Stages&... stages) {
        static_assert(sizeof...(Stages) == StageCount);
        stages_ = { { &stages... } };
    }

    template <typename... Stages>
    void resetStages(Stages&... stages) {
        static_assert(sizeof...(Stages) == StageCount);
        stages_ = { { &stages... } };
    }

    void prepare(const double sampleRate, const int maxBlockSize) {
        for (auto* stage : stages_) {
            if (stage != nullptr)
                stage->prepare(sampleRate, maxBlockSize);
        }
    }

    void reset() {
        for (auto* stage : stages_) {
            if (stage != nullptr)
                stage->reset();
        }
    }

    [[nodiscard]] int totalLatencySamples() const noexcept {
        int total = 0;
        for (auto* stage : stages_) {
            if (stage != nullptr)
                total += stage->getLatencySamples();
        }
        return total;
    }

    bool processInPlace(juce::AudioBuffer<float>& buffer,
                        const std::array<float, StageCount>& amounts,
                        const ProcessOptions& options) {
        bool processed = false;
        for (size_t i = 0; i < StageCount; ++i) {
            if (auto* stage = stages_[i])
                processed = stage->processInPlace(buffer, amounts[i], options) || processed;
        }
        return processed;
    }

private:
    std::array<AudioProcessStage*, StageCount> stages_ {};
};

} // namespace vxsuite
