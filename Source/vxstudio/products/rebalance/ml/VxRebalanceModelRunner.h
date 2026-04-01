#pragma once

#include "VxRebalanceConfidence.h"
#include "VxRebalanceDemucsModel.h"
#include "VxRebalanceFeatureBuffer.h"
#include "VxRebalanceOnnxModel.h"
#include "VxRebalanceSpleeterModel.h"
#include "../dsp/VxRebalanceDsp.h"

#include "../../../framework/VxStudioFft.h"
#include "../../../framework/VxStudioSignalQuality.h"
#include "../../../../../ThirdParty/resampler/Resampler.hpp"

#include <atomic>

#include <juce_core/juce_core.h>

namespace vxsuite::rebalance::ml {

class ModelRunner final : private juce::Thread {
public:
    enum class Status {
        heuristicFallback = 0,
        modelDetectedRuntimePending,
        mlMasksActive
    };

    enum class ActiveModel { none = 0, spleeter, umx4, demucs6 };
    enum class ModelPreference { any = 0, spleeter, umx4, demucs6 };

    ModelRunner();
    ~ModelRunner() override;

    void prepare(double sampleRate, int maxBlockSize,
                 const juce::File& demucsFile,
                 const juce::File& spleeterFile, const juce::File& umx4File,
                 ModelPreference preference = ModelPreference::any);
    void reset();
    void reloadModels(const juce::File& demucsFile,
                      const juce::File& spleeterFile, const juce::File& umx4File,
                      ModelPreference preference = ModelPreference::any);
    void analyseBlock(const juce::AudioBuffer<float>& buffer,
                      Dsp::RecordingType recordingType,
                      const vxsuite::SignalQualitySnapshot& signalQuality);

    // Safe to call on audio thread — lock-free seqlock copy of the latest worker-published snapshot.
    [[nodiscard]] Dsp::MlMaskSnapshot latestMaskSnapshot() const noexcept;
    [[nodiscard]] bool isUsingMlMasks() const noexcept {
        return status.load(std::memory_order_relaxed) == static_cast<int>(Status::mlMasksActive);
    }
    [[nodiscard]] bool hasDiscoveredModel() const noexcept {
        return modelDiscovered.load(std::memory_order_relaxed);
    }
    [[nodiscard]] bool hasLoadedModel() const noexcept {
        return activeModel.load(std::memory_order_relaxed) != static_cast<int>(ActiveModel::none);
    }
    [[nodiscard]] juce::String statusText() const;

private:
    static constexpr int kPendingBlockQueueCapacity = 8;

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

    struct PendingBlock {
        juce::AudioBuffer<float> audio;
        int numSamples = 0;
        int numChannels = 0;
        Dsp::RecordingType recordingType = Dsp::RecordingType::studio;
        vxsuite::SignalQualitySnapshot signalQuality {};
    };

    void run() override;
    void clearPendingBlocks() noexcept;
    void primeCenteredPadding(SampleFifo& fifo) const;
    void resetAnalysisState();
    void loadModelFiles(const juce::File& demucsFile,
                        const juce::File& spleeterFile, const juce::File& umx4File,
                        ModelPreference preference);
    void processPendingBlock(const PendingBlock& block);
    void processModelFrame(const vxsuite::SignalQualitySnapshot& signalQuality);
    void processDemucsChunk(const vxsuite::SignalQualitySnapshot& signalQuality);

    // Sized for the larger of the two models so we never reallocate.
    static constexpr int kMaxModelBins   = OnnxUmx4Model::kModelBins;   // 2049 > Spleeter's 2048
    static constexpr int kMaxModelFrames = OnnxSpleeterModel::kModelFrames; // 512 > UMX4's 64
    static constexpr int kMaxModelFftSize = OnnxUmx4Model::kModelBins * 2 - 2;
    static constexpr int kMaxModelHopSize = kMaxModelFftSize / 4;

    double sampleRateHz = 48000.0;
    int maxBlockSizePrepared = 0;
    int currentModelFftOrder = 12;
    int currentModelFftSize = kMaxModelFftSize;
    int currentModelHopSize = kMaxModelHopSize;
    int currentModelFrames = OnnxUmx4Model::kModelFrames;
    int currentModelBins   = OnnxUmx4Model::kModelBins;
    bool currentModelHasNyquist = true;
    FeatureBuffer featureBuffer;
    ConfidenceTracker confidenceTracker;
    vxsuite::RealFft fft;
    std::vector<float> window;
    std::array<AnalysisChannel, 2> channels;
    std::array<std::vector<float>, 2> fftBuffers;
    std::vector<float> modelInputMagnitudes;
    std::vector<float> frameHistory;
    std::vector<float> umxTemporalMagnitudes;
    int frameWriteIndex = 0;
    int frameCount = 0;
    bool umxTemporalMagnitudesPrimed = false;
    // Demucs-specific: chunk accumulation and analysis FFT (DSP resolution).
    std::vector<float> demucsChunkL;
    std::vector<float> demucsChunkR;
    vxsuite::RealFft demucsAnalysisFft;
    std::vector<float> demucsAnalysisWindow;
    std::vector<float> demucsWorkBuf;
    std::string lastError;
    mutable std::atomic<uint32_t> snapshotSequence { 0 };
    Dsp::MlMaskSnapshot latestSnapshot;
    juce::CriticalSection stateLock;
    juce::WaitableEvent workerWakeEvent;
    juce::AbstractFifo pendingBlocks { kPendingBlockQueueCapacity };
    std::array<PendingBlock, kPendingBlockQueueCapacity> pendingBlockQueue;
    std::atomic<bool> inferenceInFlight { false };
    std::atomic<int> status { static_cast<int>(Status::heuristicFallback) };
    std::atomic<bool> modelDiscovered { false };
    std::atomic<bool> workerPrepared { false };
    std::atomic<float> latestBlendConfidence { 0.0f };
    std::atomic<int> activeModel { static_cast<int>(ActiveModel::none) };
    // Model objects declared last — their destructors wait for pending async
    // callbacks to complete before returning, so all members they reference
    // (snapshots, locks) must still be alive at that point.
    OnnxUmx4Model onnxModel;
    OnnxSpleeterModel spleeterModel;
    OnnxDemucsModel demucsModel;
};

} // namespace vxsuite::rebalance::ml
