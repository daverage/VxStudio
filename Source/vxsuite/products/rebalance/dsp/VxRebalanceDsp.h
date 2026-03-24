#pragma once

#include "../../../framework/VxSuiteFft.h"
#include "../../../framework/VxSuiteSignalQuality.h"
#include "../../../framework/VxSuiteSpectralHelpers.h"

#include <atomic>
#include <array>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite::rebalance {

class Dsp {
public:
    enum class RecordingType : int {
        studio = 0,
        live = 1,
        phoneRough = 2
    };

    enum SourceIndex {
        vocalsSource = 0,
        drumsSource = 1,
        bassSource = 2,
        guitarSource = 3,
        otherSource = 4
    };

    struct BandRegion {
        float lo = 0.0f;
        float hi = 0.0f;
        float weight = 0.0f;
    };

    struct SourceBandProfile {
        std::array<BandRegion, 6> regions {};
    };

    struct RebalanceModeProfile {
        SourceBandProfile vocals;
        SourceBandProfile bass;
        SourceBandProfile drums;
        SourceBandProfile guitars;
        SourceBandProfile other;
        float confidenceFloor = 0.18f;
        float attackMs = 20.0f;
        float releaseMs = 90.0f;
        float harmonicTrust = 1.0f;
        float transientTrust = 0.82f;
        float lowEndProtection = 0.20f;
        float stereoWidthTrust = 0.85f;
        float residualFallbackStrength = 0.20f;
        float maxBoostDb = 6.0f;
        float maxCutDb = 9.0f;
        float globalStrength = 1.0f;
        float vocalCenterBias = 0.22f;
        float harmonicContinuityWeight = 1.0f;
        float drumTransientEmphasis = 0.90f;
        float residualFallback = 0.22f;
        float lowEndUnityBlendStartHz = 35.0f;
        float lowEndUnityBlendEndHz = 80.0f;
    };

    static constexpr int kSourceCount = 5;
    static constexpr int kControlCount = 6;
    static constexpr int kFftOrder = 10;
    static constexpr int kFftSize = 1 << kFftOrder;
    static constexpr int kHopSize = kFftSize / 4;
    static constexpr int kBins = kFftSize / 2 + 1;

    struct AnalysisContext {
        float vocalDominance = 0.0f;
        float intelligibility = 0.0f;
        float speechPresence = 0.0f;
        float transientRisk = 0.0f;
    };

    struct MlMaskSnapshot {
        bool available = false;
        float confidence = 0.0f;
        bool derivedGuitarFromOther = false;
        std::array<std::array<float, kBins>, kSourceCount> masks {};
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void setControlTargets(const std::array<float, kControlCount>& normalizedValues);
    void setAnalysisContext(const AnalysisContext& newContext) noexcept;
    void setSignalQuality(const vxsuite::SignalQualitySnapshot& newQuality) noexcept;
    void setRecordingType(RecordingType newType) noexcept;
    void setMlMaskSnapshot(const MlMaskSnapshot& snapshot) noexcept;
    void process(juce::AudioBuffer<float>& buffer);

    [[nodiscard]] int latencySamples() const noexcept { return kFftSize; }

private:
    struct ChannelState {
        std::vector<float> inputFifo;
        std::vector<float> outputFifo;
        std::vector<float> fftData;
        std::vector<float> ola;
        int inputCount = 0;
        int inputWritePos = 0;
        int outputCount = 0;
        int outputReadPos = 0;
        int outputWritePos = 0;
        int olaWritePos = 0;
    };

    void processFrame();
    void computeMasks(const std::array<float, kBins>& analysisMag,
                      const std::array<float, kBins>& centerWeight,
                      const std::array<float, kBins>& sideWeight);
    [[nodiscard]] const RebalanceModeProfile& currentModeProfile() const noexcept;
    [[nodiscard]] float modeAwareBandWeight(float hz, const SourceBandProfile& profile) const noexcept;
    [[nodiscard]] float smoothBand(float hz, float lo, float hi) const noexcept;
    [[nodiscard]] float binToHz(int bin) const noexcept;
    [[nodiscard]] float gaussianPeak(float x, float centre, float sigma) const noexcept;
    [[nodiscard]] float mappedSourceGainDb(int sourceIndex) const noexcept;

    double sampleRateHz = 48000.0;
    int preparedChannels = 0;
    int maxBlockSizePrepared = 0;
    vxsuite::RealFft fft;
    std::vector<float> window;
    std::vector<ChannelState> channels;
    std::array<std::atomic<float>, kControlCount> targetControlValues {};
    std::array<float, kControlCount> currentControlValues {};
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, kControlCount> controlSmoothers;
    std::atomic<int> targetRecordingType { static_cast<int>(RecordingType::studio) };
    std::atomic<float> targetVocalDominance { 0.0f };
    std::atomic<float> targetIntelligibility { 0.0f };
    std::atomic<float> targetSpeechPresence { 0.0f };
    std::atomic<float> targetTransientRisk { 0.0f };
    std::atomic<float> targetMonoScore { 0.0f };
    std::atomic<float> targetCompressionScore { 0.0f };
    std::atomic<float> targetTiltScore { 0.0f };
    std::atomic<float> targetSeparationConfidence { 1.0f };
    MlMaskSnapshot mlMaskSnapshot;
    std::array<float, kBins> prevAnalysisMag {};
    std::array<float, kBins> bassContinuity {};
    std::array<float, kBins> compositeGain {};
    std::array<std::array<float, kBins>, kSourceCount> smoothedMasks {};
    bool masksPrimed = false;
};

} // namespace vxsuite::rebalance
