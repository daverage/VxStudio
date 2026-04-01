#pragma once

#include "../../../framework/VxStudioFft.h"
#include "../../../framework/VxStudioSignalQuality.h"
#include "../../../framework/VxStudioSpectralHelpers.h"

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
        SourceBandProfile drums;
        SourceBandProfile bass;
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
    static constexpr int kDebugBins = 96;

    // Harmonic grouping constants
    static constexpr int kMaxPeaks = 24;
    static constexpr int kMaxClusters = 24;
    static constexpr int kClusterHarmonics = 8;

    // Phase 2: Object tracking constants
    static constexpr int kMaxTrackedClusters = 32;
    static constexpr int kMaxTransientEvents = 32;

    struct SpectralPeak {
        int bin = 0;
        float hz = 0.0f;
        float magnitude = 0.0f;
    };

    struct HarmonicCluster {
        bool active = false;
        int rootBin = 0;
        float rootHz = 0.0f;
        float strength = 0.0f;
        int memberCount = 0;
        std::array<int, kClusterHarmonics> memberBins {};
        std::array<float, kSourceCount> sourceScores {};
        int dominantSource = otherSource;
        float confidence = 0.0f;
    };

    // Phase 2: Tracked cluster for object continuity across frames
    struct TrackedCluster {
        bool active = false;
        int id = -1;

        float estimatedF0Hz = 0.0f;
        float meanHz = 0.0f;
        float meanMagnitude = 0.0f;

        float stereoCenter = 0.0f;
        float stereoWidth = 0.0f;

        float onsetStrength = 0.0f;
        float sustainStrength = 0.0f;

        int dominantSource = otherSource;
        std::array<float, kSourceCount> sourceProbabilities {};

        float confidence = 0.0f;
        float ageFrames = 0.0f;
        float inactiveFrames = 0.0f;
        float transientBoost = 0.0f;
        int transientBoostFrames = 0;
        
        // Lifecycle state for render authority
        enum class LifecycleState { newborn, stable, decaying };
        LifecycleState lifecycleState = LifecycleState::newborn;

        std::array<int, kClusterHarmonics> lastMemberBins {};
        int lastMemberCount = 0;
    };

    // Phase 2: Transient event for attack handling
    struct TransientEvent {
        bool active = false;
        int id = -1;
        int linkedClusterTrackId = -1;
        int birthFrame = 0;
        float peakHz = 0.0f;
        float bandwidthHz = 0.0f;
        float magnitude = 0.0f;
        float stereoWidth = 0.0f;
        float drumProbability = 0.0f;
        float guitarProbability = 0.0f;
        float vocalProbability = 0.0f;
    };

    struct AnalysisContext {
        float vocalDominance = 0.0f;
        float intelligibility = 0.0f;
        float speechPresence = 0.0f;
        float transientRisk = 0.0f;
    };

    struct DebugSnapshot {
        std::array<int, kDebugBins> dominantSources {};
        std::array<float, kDebugBins> confidence {};
        std::array<float, kDebugBins> dominantMasks {};
        std::array<float, kDebugBins> otherMasks {};
        std::array<float, kSourceCount> dominantCoverage {};
        float overallConfidence = 0.0f;
        int frameCounter = 0;
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void setControlTargets(const std::array<float, kControlCount>& normalizedValues);
    void setAnalysisContext(const AnalysisContext& newContext) noexcept;
    void setSignalQuality(const vxsuite::SignalQualitySnapshot& newQuality) noexcept;
    void setRecordingType(RecordingType newType) noexcept;
    void process(juce::AudioBuffer<float>& buffer);

    [[nodiscard]] int latencySamples() const noexcept { return kFftSize; }
    [[nodiscard]] DebugSnapshot getDebugSnapshot() const noexcept;

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

    // Harmonic grouping and source persistence
    void detectSpectralPeaks(const std::array<float, kBins>& analysisMag);
    void buildHarmonicClusters(const std::array<float, kBins>& analysisMag);
    void analyseClusterSources(const std::array<float, kBins>& analysisMag,
                               const std::array<float, kBins>& centerWeight,
                               const std::array<float, kBins>& sideWeight,
                               float transientPrior,
                               float steadyPriorScale);
    void buildSpectralEnvelope(const std::array<float, kBins>& analysisMag);
    [[nodiscard]] float vocalFormantSupport(float hz, float localMag, float envelopeMag) const noexcept;
    [[nodiscard]] float guitarTonalSupport(float hz, float localMag, float envelopeMag,
                                           float centered, float wide, float steadyPrior) const noexcept;
    void applySourcePersistence(std::array<std::array<float, kBins>, kSourceCount>& conditionedMasks,
                                float transientPrior) noexcept;

    // Phase 2: Object-based tracking and rendering
    void updateTrackedClusters(const std::array<float, kBins>& analysisMag,
                               const std::array<float, kBins>& centerWeight,
                               const std::array<float, kBins>& sideWeight);
    [[nodiscard]] float estimateClusterF0Hz(const HarmonicCluster& cluster) const noexcept;
    [[nodiscard]] int findBestTrackedClusterMatch(const HarmonicCluster& cluster) const noexcept;
    void ageTrackedClusters() noexcept;
    void detectTransientEvents(const std::array<float, kBins>& analysisMag,
                               const std::array<float, kBins>& centerWeight,
                               const std::array<float, kBins>& sideWeight,
                               float transientPrior);
    void updateTransientEvents() noexcept;
    void updateObjectSourceProbabilities(const std::array<float, kBins>& analysisMag,
                                         const std::array<float, kBins>& centerWeight,
                                         const std::array<float, kBins>& sideWeight,
                                         float transientPrior,
                                         float steadyPriorScale);
    void writeObjectOwnershipToBins() noexcept;
    void applyObjectOwnershipToMasks(std::array<std::array<float, kBins>, kSourceCount>& masks) noexcept;
    void buildForegroundBackgroundRender() noexcept;
    [[nodiscard]] float computeSourceContributionMultiplier(int source, float sliderNormalized, float strength) const noexcept;

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
    std::array<float, kBins> prevAnalysisMag {};
    std::array<float, kBins> bassContinuity {};
    std::array<float, kBins> compositeGain {};
    std::array<float, kBins> prevCompositeGain {};
    std::array<std::array<float, kBins>, kSourceCount> smoothedMasks {};
    bool masksPrimed = false;

    // Harmonic grouping and source persistence state
    std::array<int, kBins> previousWinningSource {};
    std::array<float, kBins> previousWinningConfidence {};
    std::array<float, kBins> sourcePersistence {};

    std::array<SpectralPeak, kMaxPeaks> detectedPeaks {};
    std::array<HarmonicCluster, kMaxClusters> harmonicClusters {};
    int detectedPeakCount = 0;
    int harmonicClusterCount = 0;

    std::array<float, kBins> smoothedLogSpectrum {};
    std::array<float, kBins> spectralEnvelope {};

    // Phase 2: Object tracking state
    std::array<TrackedCluster, kMaxTrackedClusters> trackedClusters {};
    int nextTrackedClusterId = 1;
    std::array<TransientEvent, kMaxTransientEvents> transientEvents {};
    int nextTransientEventId = 1;
    std::array<int, kBins> binOwningCluster {};
    std::array<float, kBins> binOwnershipConfidence {};
    std::array<float, kBins> spectralFlux {};
    int currentFrameCount = 0;
    std::array<std::atomic<int>, kDebugBins> debugDominantSources {};
    std::array<std::atomic<float>, kDebugBins> debugConfidence {};
    std::array<std::atomic<float>, kDebugBins> debugDominantMasks {};
    std::array<std::atomic<float>, kDebugBins> debugOtherMasks {};
    std::array<std::atomic<float>, kSourceCount> debugDominantCoverage {};
    std::atomic<float> debugOverallConfidence { 0.0f };
    std::atomic<int> debugFrameCounter { 0 };
};

} // namespace vxsuite::rebalance
