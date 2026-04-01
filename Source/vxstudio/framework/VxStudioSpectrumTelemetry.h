#pragma once

#include "VxStudioFft.h"
#include "VxStudioProduct.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace vxsuite::spectrum {

constexpr int kMaxTelemetrySlots = 24;
constexpr int kWaveformSamples = 512;
constexpr int kHistorySamples = 2048;
constexpr int kLevelTraceSamples = 512;

struct SnapshotView {
    bool active = false;
    bool silent = false;
    bool showTrace = false;
    int slotIndex = -1;
    int order = 0;
    std::uint64_t instanceId = 0;
    std::int64_t lastPublishMs = 0;
    double sampleRate = 48000.0;
    float levelTraceSeconds = 0.0f;
    int levelTraceCount = 0;
    float dryRms = 0.0f;
    float wetRms = 0.0f;
    std::array<float, 3> accentRgb { 0.8f, 0.8f, 0.8f };
    std::array<char, 32> productName {};
    std::array<char, 12> shortTag {};
    std::array<float, kWaveformSamples> dryWaveform {};
    std::array<float, kWaveformSamples> wetWaveform {};
    std::array<float, kLevelTraceSamples> dryLevelTrace {};
    std::array<float, kLevelTraceSamples> wetLevelTrace {};
};

struct DebugInfo {
    bool available = false;
    juce::String backendName;
    juce::String location;
    int activeSlots = 0;
    int traceSlots = 0;
};

class SnapshotRegistry {
public:
    static SnapshotRegistry& instance() noexcept;

    int registerSlot(const ProductIdentity& identity, bool showTrace, std::uint64_t& instanceIdOut);
    void unregisterSlot(int slotIndex, std::uint64_t instanceId) noexcept;

    [[nodiscard]] int maxSlots() const noexcept { return kMaxTelemetrySlots; }
    [[nodiscard]] bool readSlot(int slotIndex, SnapshotView& out) const noexcept;
    [[nodiscard]] DebugInfo debugInfo() const noexcept;

    [[nodiscard]] bool publish(int slotIndex,
                               std::uint64_t instanceId,
                               double sampleRate,
                               float levelTraceSeconds,
                               int levelTraceCount,
                               float dryRms,
                               float wetRms,
                               bool silent,
                               const std::array<float, kWaveformSamples>& dryWaveform,
                               const std::array<float, kWaveformSamples>& wetWaveform,
                               const std::array<float, kLevelTraceSamples>& dryLevelTrace,
                               const std::array<float, kLevelTraceSamples>& wetLevelTrace) noexcept;

private:
    static void copyLabel(std::string_view source, char* dest, std::size_t destSize) noexcept;
};

class SnapshotPublisher {
public:
    explicit SnapshotPublisher(const ProductIdentity& identity, bool showTrace = true);
    ~SnapshotPublisher();

    void prepare(double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;
    void publish(const juce::AudioBuffer<float>& dryBuffer,
                 const juce::AudioBuffer<float>& wetBuffer) noexcept;
    void publishSilence() noexcept;

    [[nodiscard]] std::uint64_t instanceId() const noexcept { return instanceIdValue; }
    [[nodiscard]] bool isActive() const noexcept { return slotIndex >= 0; }

private:
    void ensureRegistered() noexcept;
    void pushHistory(const juce::AudioBuffer<float>& dryBuffer,
                     const juce::AudioBuffer<float>& wetBuffer) noexcept;
    void buildWaveform(const std::array<float, kHistorySamples>& history,
                       std::array<float, kWaveformSamples>& waveform) const noexcept;
    void buildLevelTrace(const std::array<float, kLevelTraceSamples>& history,
                         int count,
                         int writePos,
                         std::array<float, kLevelTraceSamples>& trace) const noexcept;
    [[nodiscard]] float computeRms(const std::array<float, kHistorySamples>& history) const noexcept;

    ProductIdentity identityDescriptor;
    bool showTraceValue = true;
    int slotIndex = -1;
    std::uint64_t instanceIdValue = 0;
    double currentSampleRate = 48000.0;
    int writeIndex = 0;
    int historyCount = 0;
    int publishIntervalSamples = 1600;
    int samplesUntilPublish = 1600;
    int levelTraceWriteIndex = 0;
    int levelTraceCount = 0;
    int levelTraceBucketSizeSamples = 1600;
    int levelTraceBucketSampleCount = 0;
    double dryLevelBucketSumSquares = 0.0;
    double wetLevelBucketSumSquares = 0.0;
    std::array<float, kHistorySamples> dryHistory {};
    std::array<float, kHistorySamples> wetHistory {};
    std::array<float, kWaveformSamples> dryWaveform {};
    std::array<float, kWaveformSamples> wetWaveform {};
    std::array<float, kLevelTraceSamples> dryLevelHistory {};
    std::array<float, kLevelTraceSamples> wetLevelHistory {};
    std::array<float, kLevelTraceSamples> dryLevelTrace {};
    std::array<float, kLevelTraceSamples> wetLevelTrace {};
};

} // namespace vxsuite::spectrum

namespace vxsuite::analysis {

constexpr int kMaxDomains = 8;
constexpr int kMaxStageSlots = 48;
constexpr int kSummarySpectrumBins = 256;
constexpr int kSummaryEnvelopeBins = 96;
constexpr int kSummarySpectrumFftOrder = 13;
constexpr int kSummarySpectrumFftSize = 1 << kSummarySpectrumFftOrder;

enum class DetailLevel : std::uint8_t {
    tier1 = 0,
    tier2 = 1
};

struct DomainView {
    bool active = false;
    int slotIndex = -1;
    std::uint64_t analysisDomainId = 0;
    std::uint64_t hostProcessId = 0;
    std::uint64_t creationTimeMs = 0;
};

struct AnalysisSummary {
    std::array<float, kSummarySpectrumBins> spectrum {};
    std::array<float, kSummaryEnvelopeBins> envelope {};
    float rms = 0.0f;
    float peak = 0.0f;
    float crestFactor = 0.0f;
    float transientScore = 0.0f;
    float stereoWidth = 0.0f;
    float correlation = 0.0f;
};

struct StageIdentity {
    std::array<char, 32> stageId {};
    std::uint64_t instanceId = 0;
    std::uint64_t localOrderId = 0;
    std::array<char, 64> stageName {};
    StageType stageType = StageType::unknown;
    std::uint32_t chainOrderHint = 0;
    std::array<char, 16> pluginFamily {};
    std::uint32_t semanticFlags = 0;
    std::uint32_t telemetryFlags = 0;
};

struct StageState {
    std::uint64_t timestampMs = 0;
    bool isLive = false;
    bool isBypassed = false;
    bool isSilent = true;
    DetailLevel detailLevel = DetailLevel::tier1;
    float sampleRate = 48000.0f;
    std::uint8_t numChannels = 0;
};

struct StageTelemetry {
    StageIdentity identity;
    StageState state;
    AnalysisSummary inputSummary;
    AnalysisSummary outputSummary;
};

struct StageView {
    bool active = false;
    int slotIndex = -1;
    std::uint64_t analysisDomainId = 0;
    StageTelemetry telemetry;
};

struct SummaryAccumulator {
    static constexpr int kMetricHistoryBlocks = 256;

    std::array<float, kSummaryEnvelopeBins> envelope {};
    std::array<double, kMetricHistoryBlocks> blockSumSquares {};
    std::array<float, kMetricHistoryBlocks> blockPeaks {};
    std::array<int, kMetricHistoryBlocks> blockSampleCounts {};
    int metricWriteIndex = 0;
    int metricCount = 0;
    int shortMetricReadIndex = 0;
    int longMetricReadIndex = 0;
    double shortSumSquares = 0.0;
    double longSumSquares = 0.0;
    int shortWindowSamples = 1;
    int longWindowSamples = 1;
    int shortSamples = 0;
    int longSamples = 0;
    float heldPeakDb = -120.0f;
    int envelopeWriteIndex = 0;
    int envelopeFilled = 0;
    int envelopeSamplesPerBucket = 1;
    int envelopeSampleCounter = 0;
    int fftHopSize = 512;
    int fftSamplesSinceUpdate = 0;
    double midEnergy = 0.0;
    double sideEnergy = 0.0;
    double leftEnergy = 0.0;
    double rightEnergy = 0.0;
    double lrDot = 0.0;
    int stereoSamples = 0;
    float sampleRate = 48000.0f;
    int monoHistoryWriteIndex = 0;
    std::array<float, kSummarySpectrumFftSize> monoHistory {};
    std::array<float, kSummarySpectrumFftSize> spectrumWindow {};
    std::array<float, kSummarySpectrumFftSize * 2> fftData {};
    std::array<float, kSummarySpectrumBins> spectrum {};
    vxsuite::RealFft fft;

    void prepare(double sampleRate, int publishIntervalSamples) noexcept;
    void reset() noexcept;
    void update(const juce::AudioBuffer<float>& buffer) noexcept;
    [[nodiscard]] AnalysisSummary summary() const noexcept;
};

class DomainRegistry {
public:
    static DomainRegistry& instance() noexcept;

    std::uint64_t registerAnalyserDomain(std::string_view ownerStageId) noexcept;
    void unregisterAnalyserDomain(std::uint64_t analysisDomainId) noexcept;
    [[nodiscard]] bool latestDomainForProcess(std::uint64_t hostProcessId, DomainView& out) const noexcept;
    [[nodiscard]] bool latestActiveDomain(DomainView& out) const noexcept;
    [[nodiscard]] int allDomainsForProcess(std::uint64_t hostProcessId,
                                           std::array<std::uint64_t, kMaxDomains>& out) const noexcept;
    [[nodiscard]] bool ownerStageIdForDomain(std::uint64_t domainId,
                                             std::array<char, 32>& out) const noexcept;
    [[nodiscard]] std::uint64_t currentProcessId() const noexcept;
};

class StageRegistry {
public:
    static StageRegistry& instance() noexcept;

    int registerStage(const ProductIdentity& identity,
                      std::uint64_t analysisDomainId,
                      std::uint64_t& instanceIdOut,
                      std::uint64_t& localOrderIdOut) noexcept;
    void unregisterStage(int slotIndex, std::uint64_t instanceId) noexcept;
    [[nodiscard]] bool publish(int slotIndex,
                               std::uint64_t instanceId,
                               std::uint64_t analysisDomainId,
                               const StageTelemetry& telemetry) noexcept;
    [[nodiscard]] bool readStage(int slotIndex, StageView& out) const noexcept;
    [[nodiscard]] bool findStageByDomainAndStageId(std::uint64_t domainId,
                                                    const std::array<char, 32>& stageId,
                                                    StageView& out) const noexcept;
    [[nodiscard]] int maxSlots() const noexcept { return kMaxStageSlots; }
};

class StagePublisher {
public:
    explicit StagePublisher(const ProductIdentity& identity);
    ~StagePublisher();

    void prepare(double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;
    void publish(const juce::AudioBuffer<float>& inputBuffer,
                 const juce::AudioBuffer<float>& outputBuffer,
                 bool bypassed = false) noexcept;
    void publishBypassed(const juce::AudioBuffer<float>& buffer) noexcept;

    [[nodiscard]] std::uint64_t instanceId() const noexcept { return instanceIdValue; }
    [[nodiscard]] std::uint64_t analysisDomainId() const noexcept { return analysisDomainIdValue; }
    [[nodiscard]] std::uint64_t localOrderId() const noexcept { return localOrderIdValue; }

private:
    void ensureRegistered() noexcept;
    void refreshDomainBinding(bool force = false) noexcept;
    void maybePublish(bool bypassed, int numChannels) noexcept;
    static void copyLabel(std::string_view source, char* dest, std::size_t destSize) noexcept;

    ProductIdentity identityDescriptor;
    int slotIndex = -1;
    std::uint64_t instanceIdValue = 0;
    std::uint64_t localOrderIdValue = 0;
    std::uint64_t analysisDomainIdValue = 0;
    double currentSampleRate = 48000.0;
    int publishIntervalSamples = 2400;
    int samplesUntilPublish = 2400;
    int domainRefreshCountdown = 0;
    std::unique_ptr<SummaryAccumulator> inputAccumulator;
    std::unique_ptr<SummaryAccumulator> outputAccumulator;
};

} // namespace vxsuite::analysis
