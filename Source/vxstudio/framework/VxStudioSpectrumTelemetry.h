#pragma once

#include "VxStudioFft.h"
#include "VxStudioProduct.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

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
    bool registrationAttempted = false;
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
    std::uint64_t contextKeyHash = 0;
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

    std::uint64_t registerAnalyserDomain(std::string_view ownerStageId, std::uint64_t contextKeyHash = 0) noexcept;
    void unregisterAnalyserDomain(std::uint64_t analysisDomainId) noexcept;
    // Updates the contextKeyHash of an already-registered domain (e.g. after updateTrackProperties fires).
    bool updateDomainContextKey(std::uint64_t analysisDomainId, std::uint64_t contextKeyHash) noexcept;
    [[nodiscard]] bool latestDomainForProcess(std::uint64_t hostProcessId, DomainView& out, std::uint64_t contextKeyHash = 0) const noexcept;
    [[nodiscard]] bool latestActiveDomain(DomainView& out, std::uint64_t contextKeyHash = 0) const noexcept;
    [[nodiscard]] int allDomainsForProcess(std::uint64_t hostProcessId,
                                           std::array<std::uint64_t, kMaxDomains>& out, std::uint64_t contextKeyHash = 0) const noexcept;
    [[nodiscard]] bool ownerStageIdForDomain(std::uint64_t domainId,
                                             std::array<char, 32>& out) const noexcept;
    [[nodiscard]] std::uint64_t currentProcessId() const noexcept;
    [[nodiscard]] std::uint64_t fallbackDomainIdForCurrentProcess() const noexcept;
    [[nodiscard]] std::uint32_t getDomainGeneration() const noexcept;

private:
    mutable juce::CriticalSection registryMutex;
    std::atomic<std::uint32_t> domainGeneration { 0 };
};

class StageRegistry {
public:
    static StageRegistry& instance() noexcept;

    int registerStage(const ProductIdentity& identity,
                      std::uint64_t analysisDomainId,
                      std::uint64_t ctorInstanceId,
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
    [[nodiscard]] std::uint32_t getStageGeneration() const noexcept;

    // In-process track info side-table. Not in shared memory — zero layout impact.
    struct TrackInfo {
        juce::String  name;
        juce::String  channelUID;       // VST3 kChannelUIDKey — stable, unique per track
        std::uint32_t colourARGB = 0;   // VST3 kChannelColorKey
        std::int64_t  runtimeID  = 0;   // VST3 kChannelRuntimeIDKey — unique per session
        // Computed grouping key: FNV(channelUID) > runtimeID cast > 0 (unknown).
        // Never changes identity of a slot — display grouping only.
        std::uint64_t stableId   = 0;
        bool          isReliable = false; // true when channelUID or runtimeID was provided
    };
    void setTrackInfo(std::uint64_t instanceId, const TrackInfo& info) noexcept;
    [[nodiscard]] TrackInfo getTrackInfo(std::uint64_t instanceId) const noexcept;
    void clearTrackInfo(std::uint64_t instanceId) noexcept;
    [[nodiscard]] static std::uint64_t buildTrackStableId(const juce::String& channelUID,
                                                           std::int64_t runtimeID,
                                                           const juce::String& name = {}) noexcept;

    // Legacy name-only helpers kept for internal use.
    void setTrackName(std::uint64_t instanceId, const juce::String& name) noexcept;
    [[nodiscard]] juce::String getTrackName(std::uint64_t instanceId) const noexcept;
    void clearTrackName(std::uint64_t instanceId) noexcept;

private:
    mutable juce::CriticalSection registryMutex;
    std::atomic<std::uint32_t> stageGeneration { 0 };

    mutable juce::CriticalSection trackNameMutex;
    std::unordered_map<std::uint64_t, TrackInfo> trackInfos;
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
    [[nodiscard]] bool isActive() const noexcept { return slotIndex >= 0; }

    // Called on the message thread when the host reports track properties.
    void setTrackName(const juce::String& name) noexcept;
    void setTrackColour(std::uint32_t argb) noexcept;
    void setTrackRuntimeID(std::int64_t runtimeId) noexcept;
    void setChannelUID(const juce::String& uid) noexcept;
    // Sets a hint used to prefer a domain whose contextKeyHash matches this value.
    // Call with the track's stableId after updateTrackProperties is received.
    void setContextKeyHint(std::uint64_t hint) noexcept;

private:
    void ensureRegistered() noexcept;
    void refreshDomainBinding(bool force = false) noexcept;
    void maybePublish(bool bypassed, int numChannels) noexcept;
    void flushTrackInfoToRegistry() noexcept;  // call under trackNameLock
    static void copyLabel(std::string_view source, char* dest, std::size_t destSize) noexcept;

    ProductIdentity identityDescriptor;
    int slotIndex = -1;
    std::uint64_t instanceIdValue = 0;
    // Atomic mirror of instanceIdValue — safe to read from the message thread
    // (e.g. in updateTrackProperties setters) without data race.
    std::atomic<std::uint64_t> registeredInstanceId { 0 };
    std::uint64_t localOrderIdValue = 0;
    std::uint64_t analysisDomainIdValue = 0;
    double currentSampleRate = 48000.0;
    int publishIntervalSamples = 2400;
    int samplesUntilPublish = 2400;
    int domainRefreshCountdown = 0;
    std::uint32_t lastSeenDomainGeneration = 0;
    std::unique_ptr<SummaryAccumulator> inputAccumulator;
    std::unique_ptr<SummaryAccumulator> outputAccumulator;
    bool registrationAttempted = false;

    // Track info supplied by the host via updateTrackProperties (message thread).
    // Read on the audio thread during publish — protected by trackNameLock.
    mutable juce::CriticalSection trackNameLock;
    std::array<char, 64> pendingTrackName {};
    std::uint32_t pendingTrackColour = 0;
    std::int64_t  pendingTrackRuntimeID = 0;
    std::array<char, 64> pendingChannelUID {};
    bool trackInfoDirty = false;
    std::array<char, 64> publishedTrackName {};
    std::uint32_t publishedTrackColour = 0;
    std::int64_t  publishedTrackRuntimeID = 0;
    std::array<char, 64> publishedChannelUID {};

    // Stable identity for this plugin instance lifetime. Generated once in the
    // constructor. Used to evict only THIS instance's previous slot when re-
    // registering — never other instances of the same plugin type.
    const std::uint64_t ctorInstanceId;

    // Track hash supplied by the host via updateTrackProperties.
    // Used by refreshDomainBinding to prefer a domain with a matching contextKeyHash.
    std::uint64_t contextKeyHint = 0;
};

} // namespace vxsuite::analysis
