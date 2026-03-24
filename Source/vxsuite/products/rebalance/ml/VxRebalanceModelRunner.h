#pragma once

#include "VxRebalanceConfidence.h"
#include "VxRebalanceFeatureBuffer.h"
#include "../dsp/VxRebalanceDsp.h"

#include "../../../framework/VxSuiteSignalQuality.h"

#include <atomic>

#include <juce_core/juce_core.h>

namespace vxsuite::rebalance::ml {

class ModelRunner {
public:
    static constexpr int kModelHeadCount = 4;

    enum class Status {
        heuristicFallback = 0,
        umx4ModelDetectedRuntimePending,
        mlMasksActive
    };

    void prepare(double sampleRate, int maxBlockSize);
    void reset();
    void analyseBlock(const juce::AudioBuffer<float>& buffer,
                      Dsp::RecordingType recordingType,
                      const vxsuite::SignalQualitySnapshot& signalQuality);

    [[nodiscard]] const Dsp::MlMaskSnapshot& latestMaskSnapshot() const noexcept { return latestSnapshot; }
    [[nodiscard]] bool isUsingMlMasks() const noexcept {
        return status.load(std::memory_order_relaxed) == static_cast<int>(Status::mlMasksActive);
    }
    [[nodiscard]] juce::String statusText() const;

private:
    enum class ModelLayout {
        none = 0,
        umx4DerivedGuitar,
        fiveStem
    };

    [[nodiscard]] juce::File bundleResourcesDirectory() const;
    [[nodiscard]] juce::File findModelAsset() const;
    [[nodiscard]] ModelLayout detectModelLayout(const juce::File& modelFile) const noexcept;

    double sampleRateHz = 48000.0;
    int maxBlockSizePrepared = 0;
    FeatureBuffer featureBuffer;
    ConfidenceTracker confidenceTracker;
    Dsp::MlMaskSnapshot latestSnapshot;
    std::atomic<int> status { static_cast<int>(Status::heuristicFallback) };
    std::atomic<bool> modelDiscovered { false };
    std::atomic<float> latestBlendConfidence { 0.0f };
    ModelLayout detectedLayout = ModelLayout::none;
};

} // namespace vxsuite::rebalance::ml
