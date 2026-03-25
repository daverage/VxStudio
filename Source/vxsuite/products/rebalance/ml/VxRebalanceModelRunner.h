#pragma once

#include "VxRebalanceConfidence.h"
#include "VxRebalanceFeatureBuffer.h"
#include "VxRebalanceOnnxModel.h"
#include "../dsp/VxRebalanceDsp.h"

#include "../../../framework/VxSuiteFft.h"
#include "../../../framework/VxSuiteSignalQuality.h"
#include "../../../../../ThirdParty/resampler/Resampler.hpp"

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

    void prepare(double sampleRate, int maxBlockSize, const juce::File& onnxFile);
    void reset();
    void analyseBlock(const juce::AudioBuffer<float>& buffer,
                      Dsp::RecordingType recordingType,
                      const vxsuite::SignalQualitySnapshot& signalQuality);

    // Safe to call on audio thread — returns the snapshot copied in analyseBlock().
    [[nodiscard]] const Dsp::MlMaskSnapshot& latestMaskSnapshot() const noexcept { return audioThreadSnapshot; }
    [[nodiscard]] bool isUsingMlMasks() const noexcept {
        return status.load(std::memory_order_relaxed) == static_cast<int>(Status::mlMasksActive);
    }
    [[nodiscard]] bool hasDiscoveredModel() const noexcept {
        return modelDiscovered.load(std::memory_order_relaxed);
    }
    [[nodiscard]] juce::String statusText() const;

private:
    struct SampleFifo {
        std::vector<float> buffer;
        int writePos = 0;
        int available = 0;

        void reset(int capacity);
        void clear();
        void push(const float* data, int count);
    };

    struct AnalysisChannel {
        SampleFifo inputFifo;
        std::unique_ptr<Resampler<1, 1>> resampler;
        std::vector<float> sourceScratch;
        std::vector<float> resampleIn;
        std::vector<float> resampleOut;
    };

    void processModelFrame(const vxsuite::SignalQualitySnapshot& signalQuality);

    double sampleRateHz = 48000.0;
    int maxBlockSizePrepared = 0;
    FeatureBuffer featureBuffer;
    ConfidenceTracker confidenceTracker;
    vxsuite::RealFft fft;
    std::vector<float> window;
    std::array<AnalysisChannel, 2> channels;
    std::array<std::vector<float>, 2> fftBuffers;
    std::vector<float> modelInputMagnitudes;
    std::vector<float> frameHistory;
    int frameWriteIndex = 0;
    int frameCount = 0;
    std::string lastError;
    // latestSnapshot: written by ORT callback thread, read in analyseBlock under lock.
    // audioThreadSnapshot: audio-thread-only copy, updated in analyseBlock after lock.
    juce::SpinLock snapshotLock;
    Dsp::MlMaskSnapshot latestSnapshot;
    Dsp::MlMaskSnapshot audioThreadSnapshot;
    std::atomic<bool> inferenceInFlight { false };
    std::atomic<int> status { static_cast<int>(Status::heuristicFallback) };
    std::atomic<bool> modelDiscovered { false };
    std::atomic<float> latestBlendConfidence { 0.0f };
    // onnxModel declared last so its destructor (which waits for pending async)
    // runs before the snapshot/lock members are destroyed.
    OnnxUmx4Model onnxModel;
};

} // namespace vxsuite::rebalance::ml
