#include "VxSuiteSpectrumTelemetry.h"
#include "VxSuiteBlockSmoothing.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <memory>

#include <juce_core/juce_core.h>

#if JUCE_WINDOWS
#include <processthreadsapi.h>
#else
#include <unistd.h>
#endif

namespace vxsuite::spectrum {

namespace {

constexpr std::uint32_t kSharedMagic = 0x56585350u; // VXSP
constexpr std::uint32_t kSharedVersion = 3u;
constexpr auto kSharedLockName = "vxsuite-spectrum-telemetry-lock";
constexpr auto kSharedFolderName = "VXSuiteShared";
constexpr auto kSharedFileName = "vxsuite-spectrum-telemetry.bin";
constexpr std::int64_t kSilentSlotReuseMs = 1500;

template <typename Value>
std::atomic_ref<Value> atomicRef(Value& value) noexcept {
    return std::atomic_ref<Value>(value);
}

template <typename ArrayType>
void clearArray(ArrayType& values) noexcept {
    values.fill(0.0f);
}

struct SharedSlot {
    std::uint32_t version = 0;
    std::uint32_t active = 0;
    std::uint32_t showTrace = 0;
    std::uint32_t silent = 1;
    std::uint64_t instanceId = 0;
    std::int32_t order = 0;
    std::int64_t lastPublishMs = 0;
    double sampleRate = 48000.0;
    float dryRms = 0.0f;
    float wetRms = 0.0f;
    std::array<float, 3> accentRgb { 0.8f, 0.8f, 0.8f };
    std::array<char, 32> productName {};
    std::array<char, 12> shortTag {};
    std::array<float, kWaveformSamples> dryWaveform {};
    std::array<float, kWaveformSamples> wetWaveform {};
};

struct SharedState {
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint64_t nextInstanceId = 1;
    std::int32_t nextOrder = 1;
    std::uint32_t reserved = 0;
    std::array<SharedSlot, kMaxTelemetrySlots> slots {};
};

class SharedMemoryRegion {
public:
    static SharedMemoryRegion& instance() {
        static SharedMemoryRegion region;
        return region;
    }

    SharedState* state() {
        initialiseIfNeeded();
        return reinterpret_cast<SharedState*>(mapping != nullptr ? mapping->getData() : nullptr);
    }

    juce::InterProcessLock& processLock() noexcept { return lock; }
    [[nodiscard]] juce::String location() const { return sharedFile.getFullPathName(); }
    [[nodiscard]] bool isAvailable() const noexcept { return mapping != nullptr; }

private:
    void initialiseIfNeeded() {
        const juce::ScopedLock scoped(localLock);
        if (mapping != nullptr)
            return;

        sharedFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                         .getChildFile(kSharedFolderName)
                         .getChildFile(kSharedFileName);
        sharedFile.getParentDirectory().createDirectory();

        if (!sharedFile.existsAsFile())
            sharedFile.create();

        const auto expectedBytes = static_cast<std::int64_t>(sizeof(SharedState));
        if (sharedFile.getSize() != expectedBytes) {
            juce::FileOutputStream stream(sharedFile);
            if (stream.openedOk()) {
                stream.setPosition(expectedBytes - 1);
                stream.writeByte(0);
                stream.flush();
            }
        }

        mapping = std::make_unique<juce::MemoryMappedFile>(sharedFile, juce::MemoryMappedFile::readWrite);
        auto* shared = reinterpret_cast<SharedState*>(mapping != nullptr ? mapping->getData() : nullptr);
        if (shared == nullptr)
            return;

        juce::InterProcessLock::ScopedLockType processScoped(lock);
        if (!processScoped.isLocked())
            return;

        if (shared->magic != kSharedMagic || shared->version != kSharedVersion) {
            std::memset(shared, 0, sizeof(SharedState));
            shared->magic = kSharedMagic;
            shared->version = kSharedVersion;
            shared->nextInstanceId = 1;
            shared->nextOrder = 1;
        }
    }

    juce::CriticalSection localLock;
    juce::InterProcessLock lock { juce::String(kSharedLockName) };
    juce::File sharedFile;
    std::unique_ptr<juce::MemoryMappedFile> mapping;
};

SharedSlot* sharedSlotAt(const int slotIndex) noexcept {
    auto* state = SharedMemoryRegion::instance().state();
    if (state == nullptr || slotIndex < 0 || slotIndex >= static_cast<int>(state->slots.size()))
        return nullptr;
    return &state->slots[static_cast<std::size_t>(slotIndex)];
}

} // namespace

SnapshotRegistry& SnapshotRegistry::instance() noexcept {
    static SnapshotRegistry registry;
    return registry;
}

int SnapshotRegistry::registerSlot(const ProductIdentity& identity,
                                   const bool showTrace,
                                   std::uint64_t& instanceIdOut) {
    auto* state = SharedMemoryRegion::instance().state();
    if (state == nullptr) {
        instanceIdOut = 0;
        return -1;
    }

    juce::InterProcessLock::ScopedLockType scoped(SharedMemoryRegion::instance().processLock());
    if (!scoped.isLocked()) {
        instanceIdOut = 0;
        return -1;
    }

    const auto nowMs = juce::Time::currentTimeMillis();

    for (int slotIndex = 0; slotIndex < static_cast<int>(state->slots.size()); ++slotIndex) {
        auto& slot = state->slots[static_cast<std::size_t>(slotIndex)];
        if (atomicRef(slot.active).load(std::memory_order_acquire) != 0u)
            continue;

        atomicRef(slot.version).store(1u, std::memory_order_release);
        slot.showTrace = showTrace ? 1u : 0u;
        slot.silent = 1u;
        slot.order = state->nextOrder++;
        slot.instanceId = state->nextInstanceId++;
        slot.lastPublishMs = nowMs;
        slot.sampleRate = 48000.0;
        slot.dryRms = 0.0f;
        slot.wetRms = 0.0f;
        slot.accentRgb = identity.theme.accentRgb;
        copyLabel(identity.productName, slot.productName.data(), slot.productName.size());
        copyLabel(identity.shortTag, slot.shortTag.data(), slot.shortTag.size());
        clearArray(slot.dryWaveform);
        clearArray(slot.wetWaveform);
        atomicRef(slot.active).store(1u, std::memory_order_release);
        atomicRef(slot.version).store(2u, std::memory_order_release);

        instanceIdOut = slot.instanceId;
        return slotIndex;
    }

    for (int slotIndex = 0; slotIndex < static_cast<int>(state->slots.size()); ++slotIndex) {
        auto& slot = state->slots[static_cast<std::size_t>(slotIndex)];
        if (atomicRef(slot.active).load(std::memory_order_acquire) == 0u)
            continue;
        if (atomicRef(slot.silent).load(std::memory_order_acquire) == 0u)
            continue;
        if ((nowMs - slot.lastPublishMs) < kSilentSlotReuseMs)
            continue;

        atomicRef(slot.version).store(1u, std::memory_order_release);
        slot.showTrace = showTrace ? 1u : 0u;
        slot.silent = 1u;
        slot.order = state->nextOrder++;
        slot.instanceId = state->nextInstanceId++;
        slot.lastPublishMs = nowMs;
        slot.sampleRate = 48000.0;
        slot.dryRms = 0.0f;
        slot.wetRms = 0.0f;
        slot.accentRgb = identity.theme.accentRgb;
        copyLabel(identity.productName, slot.productName.data(), slot.productName.size());
        copyLabel(identity.shortTag, slot.shortTag.data(), slot.shortTag.size());
        clearArray(slot.dryWaveform);
        clearArray(slot.wetWaveform);
        atomicRef(slot.active).store(1u, std::memory_order_release);
        atomicRef(slot.version).store(2u, std::memory_order_release);

        instanceIdOut = slot.instanceId;
        return slotIndex;
    }

    instanceIdOut = 0;
    return -1;
}

void SnapshotRegistry::unregisterSlot(const int slotIndex, const std::uint64_t instanceId) noexcept {
    auto* slot = sharedSlotAt(slotIndex);
    if (slot == nullptr)
        return;

    juce::InterProcessLock::ScopedLockType scoped(SharedMemoryRegion::instance().processLock());
    if (!scoped.isLocked())
        return;

    if (atomicRef(slot->active).load(std::memory_order_acquire) == 0u || slot->instanceId != instanceId)
        return;

    auto version = atomicRef(slot->version).load(std::memory_order_acquire);
    atomicRef(slot->version).store(version + 1u, std::memory_order_release);
    atomicRef(slot->active).store(0u, std::memory_order_release);
    slot->instanceId = 0;
    slot->order = 0;
    slot->showTrace = 0;
    slot->silent = 1u;
    slot->lastPublishMs = 0;
    slot->sampleRate = 48000.0;
    slot->dryRms = 0.0f;
    slot->wetRms = 0.0f;
    clearArray(slot->dryWaveform);
    clearArray(slot->wetWaveform);
    atomicRef(slot->version).store(version + 2u, std::memory_order_release);
}

bool SnapshotRegistry::readSlot(const int slotIndex, SnapshotView& out) const noexcept {
    auto* slot = sharedSlotAt(slotIndex);
    if (slot == nullptr)
        return false;

    for (int attempt = 0; attempt < 4; ++attempt) {
        const auto versionStart = atomicRef(slot->version).load(std::memory_order_acquire);
        if ((versionStart & 1u) != 0u)
            continue;

        if (atomicRef(slot->active).load(std::memory_order_acquire) == 0u)
            return false;

        out.active = true;
        out.silent = slot->silent != 0u;
        out.showTrace = slot->showTrace != 0u;
        out.slotIndex = slotIndex;
        out.order = slot->order;
        out.instanceId = slot->instanceId;
        out.lastPublishMs = slot->lastPublishMs;
        out.sampleRate = slot->sampleRate;
        out.dryRms = slot->dryRms;
        out.wetRms = slot->wetRms;
        out.accentRgb = slot->accentRgb;
        out.productName = slot->productName;
        out.shortTag = slot->shortTag;
        out.dryWaveform = slot->dryWaveform;
        out.wetWaveform = slot->wetWaveform;

        const auto versionEnd = atomicRef(slot->version).load(std::memory_order_acquire);
        if (versionStart == versionEnd && (versionEnd & 1u) == 0u)
            return true;
    }

    return false;
}

DebugInfo SnapshotRegistry::debugInfo() const noexcept {
    DebugInfo info;
    info.backendName = "shared-memory file";
    info.location = SharedMemoryRegion::instance().location();
    info.available = SharedMemoryRegion::instance().isAvailable();

    auto* state = SharedMemoryRegion::instance().state();
    if (state == nullptr)
        return info;

    for (const auto& slot : state->slots) {
        if (atomicRef(const_cast<std::uint32_t&>(slot.active)).load(std::memory_order_acquire) != 0u) {
            ++info.activeSlots;
            if (slot.showTrace != 0u)
                ++info.traceSlots;
        }
    }

    return info;
}

bool SnapshotRegistry::publish(int slotIndex,
                               const std::uint64_t instanceId,
                               const double sampleRate,
                               const float dryRms,
                               const float wetRms,
                               const bool silent,
                               const std::array<float, kWaveformSamples>& dryWaveform,
                               const std::array<float, kWaveformSamples>& wetWaveform) noexcept {
    auto* slot = sharedSlotAt(slotIndex);
    if (slot == nullptr)
        return false;

    if (atomicRef(slot->active).load(std::memory_order_acquire) == 0u || slot->instanceId != instanceId)
        return false;

    const auto version = atomicRef(slot->version).load(std::memory_order_acquire);
    atomicRef(slot->version).store(version + 1u, std::memory_order_release);
    slot->silent = silent ? 1u : 0u;
    slot->lastPublishMs = juce::Time::currentTimeMillis();
    slot->sampleRate = sampleRate;
    slot->dryRms = dryRms;
    slot->wetRms = wetRms;
    slot->dryWaveform = dryWaveform;
    slot->wetWaveform = wetWaveform;
    atomicRef(slot->version).store(version + 2u, std::memory_order_release);
    return true;
}

void SnapshotRegistry::copyLabel(const std::string_view source, char* dest, const std::size_t destSize) noexcept {
    if (dest == nullptr || destSize == 0)
        return;

    std::fill(dest, dest + destSize, '\0');
    const auto copyCount = std::min(source.size(), destSize - 1);
    std::copy_n(source.data(), static_cast<std::ptrdiff_t>(copyCount), dest);
}

SnapshotPublisher::SnapshotPublisher(const ProductIdentity& identity, const bool showTrace)
    : identityDescriptor(identity),
      showTraceValue(showTrace) {
    ensureRegistered();
}

SnapshotPublisher::~SnapshotPublisher() {
    SnapshotRegistry::instance().unregisterSlot(slotIndex, instanceIdValue);
}

void SnapshotPublisher::prepare(const double sampleRate, const int maxBlockSize) noexcept {
    ensureRegistered();
    currentSampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    publishIntervalSamples = std::max(512, std::min(kHistorySamples, static_cast<int>(currentSampleRate / 30.0)));
    samplesUntilPublish = std::max(publishIntervalSamples, std::max(1, maxBlockSize));
    reset();
}

void SnapshotPublisher::reset() noexcept {
    writeIndex = 0;
    historyCount = 0;
    samplesUntilPublish = publishIntervalSamples;
    clearArray(dryHistory);
    clearArray(wetHistory);
    clearArray(dryWaveform);
    clearArray(wetWaveform);
}

void SnapshotPublisher::ensureRegistered() noexcept {
    if (slotIndex >= 0 && instanceIdValue != 0)
        return;

    slotIndex = SnapshotRegistry::instance().registerSlot(identityDescriptor, showTraceValue, instanceIdValue);
}

void SnapshotPublisher::publish(const juce::AudioBuffer<float>& dryBuffer,
                                const juce::AudioBuffer<float>& wetBuffer) noexcept {
    if (slotIndex < 0)
        ensureRegistered();
    if (slotIndex < 0)
        return;

    const int numSamples = std::min(dryBuffer.getNumSamples(), wetBuffer.getNumSamples());
    if (numSamples <= 0)
        return;

    pushHistory(dryBuffer, wetBuffer);
    historyCount = std::min(kHistorySamples, historyCount + numSamples);
    samplesUntilPublish -= numSamples;
    if (samplesUntilPublish > 0)
        return;

    buildWaveform(dryHistory, dryWaveform);
    buildWaveform(wetHistory, wetWaveform);
    if (!SnapshotRegistry::instance().publish(slotIndex,
                                              instanceIdValue,
                                              currentSampleRate,
                                              computeRms(dryHistory),
                                              computeRms(wetHistory),
                                              false,
                                              dryWaveform,
                                              wetWaveform)) {
        slotIndex = -1;
        instanceIdValue = 0;
        ensureRegistered();
    }
    samplesUntilPublish = publishIntervalSamples;
}

void SnapshotPublisher::publishSilence() noexcept {
    if (slotIndex < 0)
        ensureRegistered();
    if (slotIndex < 0)
        return;

    clearArray(dryHistory);
    clearArray(wetHistory);
    clearArray(dryWaveform);
    clearArray(wetWaveform);
    writeIndex = 0;
    historyCount = 0;
    samplesUntilPublish = publishIntervalSamples;

    if (!SnapshotRegistry::instance().publish(slotIndex,
                                              instanceIdValue,
                                              currentSampleRate,
                                              0.0f,
                                              0.0f,
                                              true,
                                              dryWaveform,
                                              wetWaveform)) {
        slotIndex = -1;
        instanceIdValue = 0;
        ensureRegistered();
    }
}

void SnapshotPublisher::pushHistory(const juce::AudioBuffer<float>& dryBuffer,
                                    const juce::AudioBuffer<float>& wetBuffer) noexcept {
    const int dryChannels = dryBuffer.getNumChannels();
    const int wetChannels = wetBuffer.getNumChannels();
    const int samples = std::min(dryBuffer.getNumSamples(), wetBuffer.getNumSamples());
    if (dryChannels <= 0 || wetChannels <= 0 || samples <= 0)
        return;

    const float dryScale = 1.0f / static_cast<float>(dryChannels);
    const float wetScale = 1.0f / static_cast<float>(wetChannels);
    for (int sampleIndex = 0; sampleIndex < samples; ++sampleIndex) {
        float dryMono = 0.0f;
        for (int channel = 0; channel < dryChannels; ++channel)
            dryMono += dryBuffer.getSample(channel, sampleIndex);

        float wetMono = 0.0f;
        for (int channel = 0; channel < wetChannels; ++channel)
            wetMono += wetBuffer.getSample(channel, sampleIndex);

        dryHistory[static_cast<std::size_t>(writeIndex)] = dryMono * dryScale;
        wetHistory[static_cast<std::size_t>(writeIndex)] = wetMono * wetScale;
        writeIndex = (writeIndex + 1) % kHistorySamples;
    }
}

void SnapshotPublisher::buildWaveform(const std::array<float, kHistorySamples>& history,
                                      std::array<float, kWaveformSamples>& waveform) const noexcept {
    constexpr int samplesPerBucket = kHistorySamples / kWaveformSamples;
    static_assert(kHistorySamples % kWaveformSamples == 0);

    int readIndex = writeIndex;
    for (int bucket = 0; bucket < kWaveformSamples; ++bucket) {
        float sum = 0.0f;
        for (int offset = 0; offset < samplesPerBucket; ++offset) {
            const int wrappedIndex = (readIndex + offset) % kHistorySamples;
            sum += history[static_cast<std::size_t>(wrappedIndex)];
        }

        waveform[static_cast<std::size_t>(bucket)] = sum / static_cast<float>(samplesPerBucket);
        readIndex = (readIndex + samplesPerBucket) % kHistorySamples;
    }
}

float SnapshotPublisher::computeRms(const std::array<float, kHistorySamples>& history) const noexcept {
    double energy = 0.0;
    for (float sample : history)
        energy += static_cast<double>(sample) * static_cast<double>(sample);

    return static_cast<float>(std::sqrt(energy / static_cast<double>(history.size())));
}

} // namespace vxsuite::spectrum

namespace vxsuite::analysis {

namespace {

constexpr std::uint32_t kAnalysisSharedMagic = 0x5658414Eu; // VXAN
constexpr std::uint32_t kAnalysisSharedVersion = 2u;
constexpr auto kAnalysisSharedLockName = "vxsuite-analysis-telemetry-lock";
constexpr auto kAnalysisSharedFileName = "vxsuite-analysis-telemetry.bin";
constexpr auto kDomainSharedFileName = "vxsuite-analysis-domains.bin";
constexpr std::int64_t kStageSlotReuseMs = 1500;
constexpr int kTargetPublishHz = 15;
constexpr int kDomainRefreshSamples = 4096;
constexpr float kEnvelopeWindowSeconds = 0.25f;
constexpr float kPeakDecayDbPerSecond = 12.0f;

std::uint64_t osCurrentProcessId() noexcept {
#if JUCE_WINDOWS
    return static_cast<std::uint64_t>(::GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

template <typename Value>
std::atomic_ref<Value> analysisAtomicRef(Value& value) noexcept {
    return std::atomic_ref<Value>(value);
}

template <typename ArrayType>
void clearNumericArray(ArrayType& values) noexcept {
    values.fill(0);
}

struct SharedDomainSlot {
    std::uint32_t version = 0;
    std::uint32_t active = 0;
    std::uint64_t analysisDomainId = 0;
    std::uint64_t hostProcessId = 0;
    std::uint64_t creationTimeMs = 0;
};

struct SharedDomainState {
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint64_t nextDomainId = 1;
    std::uint64_t reserved = 0;
    std::array<SharedDomainSlot, kMaxDomains> slots {};
};

struct SharedStageSlot {
    std::uint32_t version = 0;
    std::uint32_t active = 0;
    std::uint64_t analysisDomainId = 0;
    StageTelemetry telemetry {};
};

struct SharedAnalysisState {
    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::uint64_t nextInstanceId = 1;
    std::uint64_t reserved = 0;
    std::array<std::uint64_t, kMaxDomains> nextLocalOrderIds {};
    std::array<SharedStageSlot, kMaxStageSlots> slots {};
};

class AnalysisMappedRegion {
public:
    AnalysisMappedRegion(const char* lockName, const char* fileName)
        : lock(juce::String(lockName)),
          fileBasename(fileName) {}

    template <typename StateType>
    StateType* state(const std::uint32_t magic, const std::uint32_t version) {
        initialiseIfNeeded<StateType>(magic, version);
        return reinterpret_cast<StateType*>(mapping != nullptr ? mapping->getData() : nullptr);
    }

    juce::InterProcessLock& processLock() noexcept { return lock; }

private:
    template <typename StateType>
    void initialiseIfNeeded(const std::uint32_t magic, const std::uint32_t version) {
        const juce::ScopedLock scoped(localLock);
        if (mapping != nullptr)
            return;

        sharedFile = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                         .getChildFile("VXSuiteShared")
                         .getChildFile(fileBasename);
        sharedFile.getParentDirectory().createDirectory();

        if (!sharedFile.existsAsFile())
            sharedFile.create();

        const auto expectedBytes = static_cast<std::int64_t>(sizeof(StateType));
        if (sharedFile.getSize() != expectedBytes) {
            juce::FileOutputStream stream(sharedFile);
            if (stream.openedOk()) {
                stream.setPosition(expectedBytes - 1);
                stream.writeByte(0);
                stream.flush();
            }
        }

        mapping = std::make_unique<juce::MemoryMappedFile>(sharedFile, juce::MemoryMappedFile::readWrite);
        auto* shared = reinterpret_cast<StateType*>(mapping != nullptr ? mapping->getData() : nullptr);
        if (shared == nullptr)
            return;

        juce::InterProcessLock::ScopedLockType processScoped(lock);
        if (!processScoped.isLocked())
            return;

        if (shared->magic != magic || shared->version != version) {
            std::memset(shared, 0, sizeof(StateType));
            shared->magic = magic;
            shared->version = version;
        }
    }

    juce::CriticalSection localLock;
    juce::InterProcessLock lock;
    const juce::String fileBasename;
    juce::File sharedFile;
    std::unique_ptr<juce::MemoryMappedFile> mapping;
};

AnalysisMappedRegion& domainRegion() {
    static AnalysisMappedRegion region(kAnalysisSharedLockName, kDomainSharedFileName);
    return region;
}

AnalysisMappedRegion& stageRegion() {
    static AnalysisMappedRegion region(kAnalysisSharedLockName, kAnalysisSharedFileName);
    return region;
}

SharedDomainState* domainState() {
    auto* state = domainRegion().state<SharedDomainState>(kAnalysisSharedMagic, kAnalysisSharedVersion);
    if (state != nullptr && state->nextDomainId == 0)
        state->nextDomainId = 1;
    return state;
}

SharedAnalysisState* analysisState() {
    auto* state = stageRegion().state<SharedAnalysisState>(kAnalysisSharedMagic, kAnalysisSharedVersion);
    if (state == nullptr)
        return nullptr;
    if (state->nextInstanceId == 0)
        state->nextInstanceId = 1;
    for (auto& nextLocalOrderId : state->nextLocalOrderIds)
        if (nextLocalOrderId == 0)
            nextLocalOrderId = 1;
    return state;
}

int domainIndexFor(const std::uint64_t analysisDomainId) noexcept {
    if (analysisDomainId == 0)
        return 0;
    return static_cast<int>(analysisDomainId % static_cast<std::uint64_t>(kMaxDomains));
}

SharedStageSlot* stageSlotAt(const int slotIndex) noexcept {
    auto* state = analysisState();
    if (state == nullptr || slotIndex < 0 || slotIndex >= static_cast<int>(state->slots.size()))
        return nullptr;
    return &state->slots[static_cast<std::size_t>(slotIndex)];
}

void copyFixedLabel(std::string_view source, char* dest, const std::size_t destSize) noexcept {
    if (dest == nullptr || destSize == 0)
        return;

    std::fill(dest, dest + destSize, '\0');
    const auto copyCount = std::min(source.size(), destSize - 1);
    std::copy_n(source.data(), static_cast<std::ptrdiff_t>(copyCount), dest);
}

void setStageIdentityFromProduct(StageIdentity& out,
                                 const ProductIdentity& identity,
                                 const std::uint64_t instanceId,
                                 const std::uint64_t localOrderId) noexcept {
    copyFixedLabel(identity.stageId.empty() ? identity.productName : identity.stageId,
                   out.stageId.data(),
                   out.stageId.size());
    out.instanceId = instanceId;
    out.localOrderId = localOrderId;
    copyFixedLabel(identity.productName, out.stageName.data(), out.stageName.size());
    out.stageType = identity.stageType;
    out.chainOrderHint = static_cast<std::uint32_t>(localOrderId);
    copyFixedLabel("VXSuite", out.pluginFamily.data(), out.pluginFamily.size());
    out.semanticFlags = identity.semanticFlags;
    out.telemetryFlags = identity.telemetryFlags;
}

} // namespace

void SummaryAccumulator::prepare(const double sampleRate, const int publishIntervalSamples) noexcept {
    this->sampleRate = static_cast<float>(sampleRate > 1000.0 ? sampleRate : 48000.0);
    juce::ignoreUnused(publishIntervalSamples);
    shortWindowSamples = std::max(1, static_cast<int>(this->sampleRate * 0.010f));
    longWindowSamples = std::max(1, static_cast<int>(this->sampleRate * 0.100f));
    envelopeSamplesPerBucket = std::max(1, static_cast<int>((this->sampleRate * kEnvelopeWindowSeconds)
                                                             / static_cast<float>(kSummaryEnvelopeBins)));
    fftHopSize = std::max(1, kSummarySpectrumFftSize / 4);
    fft.prepare(kSummarySpectrumFftOrder);
    for (int i = 0; i < kSummarySpectrumFftSize; ++i) {
        const float phase = juce::MathConstants<float>::twoPi * static_cast<float>(i)
            / static_cast<float>(kSummarySpectrumFftSize - 1);
        spectrumWindow[static_cast<std::size_t>(i)] = 0.5f - 0.5f * std::cos(phase);
    }
    reset();
}

void SummaryAccumulator::reset() noexcept {
    envelope.fill(0.0f);
    blockSumSquares.fill(0.0);
    blockPeaks.fill(0.0f);
    blockSampleCounts.fill(0);
    metricWriteIndex = 0;
    metricCount = 0;
    shortMetricReadIndex = 0;
    longMetricReadIndex = 0;
    shortSumSquares = 0.0;
    longSumSquares = 0.0;
    shortSamples = 0;
    longSamples = 0;
    heldPeakDb = -120.0f;
    envelopeWriteIndex = 0;
    envelopeFilled = 0;
    envelopeSampleCounter = 0;
    fftSamplesSinceUpdate = 0;
    midEnergy = 0.0;
    sideEnergy = 0.0;
    leftEnergy = 0.0;
    rightEnergy = 0.0;
    lrDot = 0.0;
    stereoSamples = 0;
    monoHistoryWriteIndex = 0;
    monoHistory.fill(0.0f);
    fftData.fill(0.0f);
    spectrum.fill(0.0f);
}

void SummaryAccumulator::update(const juce::AudioBuffer<float>& buffer) noexcept {
    const int channels = buffer.getNumChannels();
    const int samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return;

    const float monoScale = 1.0f / static_cast<float>(channels);
    double blockSumSquaresValue = 0.0;
    float blockPeak = 0.0f;
    for (int sampleIndex = 0; sampleIndex < samples; ++sampleIndex) {
        float mono = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
            mono += buffer.getSample(channel, sampleIndex);
        mono *= monoScale;

        const float absMono = std::abs(mono);
        blockSumSquaresValue += static_cast<double>(mono) * static_cast<double>(mono);
        blockPeak = std::max(blockPeak, absMono);
        monoHistory[static_cast<std::size_t>(monoHistoryWriteIndex)] = mono;
        monoHistoryWriteIndex = (monoHistoryWriteIndex + 1) % kSummarySpectrumFftSize;
        ++fftSamplesSinceUpdate;

        const float left = buffer.getSample(0, sampleIndex);
        const float right = buffer.getSample(std::min(1, channels - 1), sampleIndex);
        leftEnergy += static_cast<double>(left) * static_cast<double>(left);
        rightEnergy += static_cast<double>(right) * static_cast<double>(right);
        lrDot += static_cast<double>(left) * static_cast<double>(right);
        const float mid = 0.5f * (left + right);
        const float side = 0.5f * (left - right);
        midEnergy += static_cast<double>(mid) * static_cast<double>(mid);
        sideEnergy += static_cast<double>(side) * static_cast<double>(side);
        ++stereoSamples;
    }

    const int writeIndex = metricWriteIndex;
    blockSumSquares[static_cast<std::size_t>(writeIndex)] = blockSumSquaresValue;
    blockPeaks[static_cast<std::size_t>(writeIndex)] = blockPeak;
    blockSampleCounts[static_cast<std::size_t>(writeIndex)] = samples;

    metricWriteIndex = (metricWriteIndex + 1) % kMetricHistoryBlocks;
    if (metricCount < kMetricHistoryBlocks)
        ++metricCount;

    shortSumSquares += blockSumSquaresValue;
    longSumSquares += blockSumSquaresValue;
    shortSamples += samples;
    longSamples += samples;

    while (shortSamples > shortWindowSamples && metricCount > 0) {
        const int index = shortMetricReadIndex;
        shortSumSquares -= blockSumSquares[static_cast<std::size_t>(index)];
        shortSamples -= blockSampleCounts[static_cast<std::size_t>(index)];
        shortMetricReadIndex = (shortMetricReadIndex + 1) % kMetricHistoryBlocks;
    }

    while (longSamples > longWindowSamples && metricCount > 0) {
        const int index = longMetricReadIndex;
        longSumSquares -= blockSumSquares[static_cast<std::size_t>(index)];
        longSamples -= blockSampleCounts[static_cast<std::size_t>(index)];
        longMetricReadIndex = (longMetricReadIndex + 1) % kMetricHistoryBlocks;
    }

    const float blockPeakDb = juce::Decibels::gainToDecibels(std::max(1.0e-6f, blockPeak), -120.0f);
    if (blockPeakDb > heldPeakDb) {
        heldPeakDb = blockPeakDb;
    } else {
        const float elapsedSeconds = static_cast<float>(samples) / sampleRate;
        heldPeakDb = std::max(-120.0f, heldPeakDb - kPeakDecayDbPerSecond * elapsedSeconds);
    }

    envelopeSampleCounter += samples;
    const float longRms = longSamples > 0 ? static_cast<float>(std::sqrt(longSumSquares / static_cast<double>(longSamples))) : 0.0f;
    while (envelopeSampleCounter >= envelopeSamplesPerBucket) {
        envelope[static_cast<std::size_t>(envelopeWriteIndex)] = longRms;
        envelopeWriteIndex = (envelopeWriteIndex + 1) % kSummaryEnvelopeBins;
        envelopeFilled = std::min(kSummaryEnvelopeBins, envelopeFilled + 1);
        envelopeSampleCounter -= envelopeSamplesPerBucket;
    }

    while (fftSamplesSinceUpdate >= fftHopSize) {
        fftSamplesSinceUpdate -= fftHopSize;
        if (!fft.isReady())
            continue;

        int readIndex = monoHistoryWriteIndex;
        for (int i = 0; i < kSummarySpectrumFftSize; ++i) {
            fftData[static_cast<std::size_t>(i)] =
                monoHistory[static_cast<std::size_t>(readIndex)] * spectrumWindow[static_cast<std::size_t>(i)];
            readIndex = (readIndex + 1) % kSummarySpectrumFftSize;
        }
        std::fill(fftData.begin() + kSummarySpectrumFftSize, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForward(fftData.data());

        constexpr float kMinFreq = 20.0f;
        constexpr float kMaxFreq = 20000.0f;
        const float nyquist = std::max(200.0f, sampleRate * 0.5f);
        const float upperFreq = std::min(kMaxFreq, nyquist);
        const float fftScale = 2.0f / static_cast<float>(kSummarySpectrumFftSize);

        for (int band = 0; band < kSummarySpectrumBins; ++band) {
            const float startNorm = static_cast<float>(band) / static_cast<float>(kSummarySpectrumBins);
            const float endNorm = static_cast<float>(band + 1) / static_cast<float>(kSummarySpectrumBins);
            const float startFreq = kMinFreq * std::pow(upperFreq / kMinFreq, startNorm);
            const float endFreq = kMinFreq * std::pow(upperFreq / kMinFreq, endNorm);

            int startBin = juce::jlimit(1,
                                        kSummarySpectrumFftSize / 2,
                                        static_cast<int>(std::floor(startFreq * kSummarySpectrumFftSize / sampleRate)));
            int endBin = juce::jlimit(startBin,
                                      kSummarySpectrumFftSize / 2,
                                      static_cast<int>(std::ceil(endFreq * kSummarySpectrumFftSize / sampleRate)));

            // The lowest log bands can otherwise collapse to one or two FFT bins,
            // which makes the analyser overreact to single-bin fluctuations and
            // mis-shape the low end versus tools like SPAN.
            constexpr int kMinBinsPerBand = 3;
            if ((endBin - startBin + 1) < kMinBinsPerBand) {
                const int centerBin = juce::jlimit(1,
                                                   kSummarySpectrumFftSize / 2,
                                                   static_cast<int>(std::lround(std::sqrt(startFreq * endFreq)
                                                                                * kSummarySpectrumFftSize / sampleRate)));
                startBin = juce::jlimit(1, kSummarySpectrumFftSize / 2, centerBin - 1);
                endBin = juce::jlimit(startBin, kSummarySpectrumFftSize / 2, centerBin + 1);
            }

            double powerSum = 0.0;
            double weightSum = 0.0;
            float peakMagnitude = 0.0f;
            for (int bin = startBin; bin <= endBin; ++bin) {
                const double magnitude = static_cast<double>(fftData[static_cast<std::size_t>(bin)]) * fftScale;
                powerSum += magnitude * magnitude;
                weightSum += 1.0;
                peakMagnitude = std::max(peakMagnitude, static_cast<float>(magnitude));
            }

            if (weightSum > 0.0) {
                const float rmsMagnitude = static_cast<float>(std::sqrt(powerSum / weightSum));
                const int binCount = endBin - startBin + 1;
                const float peakWeight = binCount <= kMinBinsPerBand ? 0.30f : 0.75f;
                // Preserve prominent partials without letting single low-bin spikes
                // dominate the whole low-end contour.
                spectrum[static_cast<std::size_t>(band)] =
                    peakWeight * peakMagnitude + (1.0f - peakWeight) * rmsMagnitude;
            } else {
                spectrum[static_cast<std::size_t>(band)] = 0.0f;
            }
        }
    }
}

AnalysisSummary SummaryAccumulator::summary() const noexcept {
    AnalysisSummary out;
    if (envelopeFilled > 0) {
        const int start = envelopeFilled < kSummaryEnvelopeBins ? 0 : envelopeWriteIndex;
        for (int i = 0; i < kSummaryEnvelopeBins; ++i) {
            const int sourceIndex = (start + i) % kSummaryEnvelopeBins;
            out.envelope[static_cast<std::size_t>(i)] = envelope[static_cast<std::size_t>(sourceIndex)];
        }
        if (envelopeFilled < kSummaryEnvelopeBins) {
            const float latest = envelopeFilled > 0
                ? envelope[static_cast<std::size_t>((envelopeWriteIndex + kSummaryEnvelopeBins - 1) % kSummaryEnvelopeBins)]
                : 0.0f;
            const int padCount = kSummaryEnvelopeBins - envelopeFilled;
            for (int i = 0; i < padCount; ++i)
                out.envelope[static_cast<std::size_t>(i)] = latest;
        }
    } else {
        out.envelope.fill(0.0f);
    }

    out.rms = longSamples > 0 ? static_cast<float>(std::sqrt(longSumSquares / static_cast<double>(longSamples))) : 0.0f;
    out.peak = juce::Decibels::decibelsToGain(heldPeakDb, -120.0f);
    out.crestFactor = out.rms > 1.0e-6f ? out.peak / out.rms : 0.0f;
    const float shortRms = shortSamples > 0 ? static_cast<float>(std::sqrt(shortSumSquares / static_cast<double>(shortSamples))) : 0.0f;
    out.transientScore =
        juce::Decibels::gainToDecibels(std::max(1.0e-6f, shortRms), -120.0f)
        - juce::Decibels::gainToDecibels(std::max(1.0e-6f, out.rms), -120.0f);
    out.stereoWidth = static_cast<float>(sideEnergy / std::max(1.0, midEnergy));
    const double corrDenom = std::sqrt(std::max(1.0e-12, leftEnergy * rightEnergy));
    out.correlation = static_cast<float>(lrDot / corrDenom);
    out.spectrum = spectrum;

    return out;
}

DomainRegistry& DomainRegistry::instance() noexcept {
    static DomainRegistry registry;
    return registry;
}

std::uint64_t DomainRegistry::registerAnalyserDomain() noexcept {
    auto* state = domainState();
    if (state == nullptr)
        return 0;

    juce::InterProcessLock::ScopedLockType scoped(domainRegion().processLock());
    if (!scoped.isLocked())
        return 0;

    const auto nowMs = static_cast<std::uint64_t>(juce::Time::currentTimeMillis());
    const auto pid = osCurrentProcessId();
    for (auto& slot : state->slots) {
        if (analysisAtomicRef(slot.active).load(std::memory_order_acquire) != 0u)
            continue;

        analysisAtomicRef(slot.version).store(1u, std::memory_order_release);
        slot.analysisDomainId = state->nextDomainId++;
        slot.hostProcessId = pid;
        slot.creationTimeMs = nowMs;
        analysisAtomicRef(slot.active).store(1u, std::memory_order_release);
        analysisAtomicRef(slot.version).store(2u, std::memory_order_release);
        return slot.analysisDomainId;
    }

    return 0;
}

void DomainRegistry::unregisterAnalyserDomain(const std::uint64_t analysisDomainId) noexcept {
    auto* state = domainState();
    if (state == nullptr || analysisDomainId == 0)
        return;

    juce::InterProcessLock::ScopedLockType scoped(domainRegion().processLock());
    if (!scoped.isLocked())
        return;

    for (auto& slot : state->slots) {
        if (analysisAtomicRef(slot.active).load(std::memory_order_acquire) == 0u
            || slot.analysisDomainId != analysisDomainId) {
            continue;
        }

        const auto version = analysisAtomicRef(slot.version).load(std::memory_order_acquire);
        analysisAtomicRef(slot.version).store(version + 1u, std::memory_order_release);
        analysisAtomicRef(slot.active).store(0u, std::memory_order_release);
        slot.analysisDomainId = 0;
        slot.hostProcessId = 0;
        slot.creationTimeMs = 0;
        analysisAtomicRef(slot.version).store(version + 2u, std::memory_order_release);
        return;
    }
}

bool DomainRegistry::latestDomainForProcess(const std::uint64_t hostProcessId, DomainView& out) const noexcept {
    auto* state = domainState();
    if (state == nullptr)
        return false;

    bool found = false;
    for (int slotIndex = 0; slotIndex < static_cast<int>(state->slots.size()); ++slotIndex) {
        auto& slot = state->slots[static_cast<std::size_t>(slotIndex)];
        if (analysisAtomicRef(slot.active).load(std::memory_order_acquire) == 0u)
            continue;
        if (slot.hostProcessId != hostProcessId)
            continue;
        if (!found || slot.creationTimeMs > out.creationTimeMs) {
            out.active = true;
            out.slotIndex = slotIndex;
            out.analysisDomainId = slot.analysisDomainId;
            out.hostProcessId = slot.hostProcessId;
            out.creationTimeMs = slot.creationTimeMs;
            found = true;
        }
    }
    return found;
}

bool DomainRegistry::latestActiveDomain(DomainView& out) const noexcept {
    auto* state = domainState();
    if (state == nullptr)
        return false;

    bool found = false;
    for (int slotIndex = 0; slotIndex < static_cast<int>(state->slots.size()); ++slotIndex) {
        auto& slot = state->slots[static_cast<std::size_t>(slotIndex)];
        if (analysisAtomicRef(slot.active).load(std::memory_order_acquire) == 0u)
            continue;
        if (!found || slot.creationTimeMs > out.creationTimeMs) {
            out.active = true;
            out.slotIndex = slotIndex;
            out.analysisDomainId = slot.analysisDomainId;
            out.hostProcessId = slot.hostProcessId;
            out.creationTimeMs = slot.creationTimeMs;
            found = true;
        }
    }
    return found;
}

std::uint64_t DomainRegistry::currentProcessId() const noexcept {
    return osCurrentProcessId();
}

StageRegistry& StageRegistry::instance() noexcept {
    static StageRegistry registry;
    return registry;
}

int StageRegistry::registerStage(const ProductIdentity& identity,
                                 const std::uint64_t analysisDomainId,
                                 std::uint64_t& instanceIdOut,
                                 std::uint64_t& localOrderIdOut) noexcept {
    auto* state = analysisState();
    if (state == nullptr) {
        instanceIdOut = 0;
        localOrderIdOut = 0;
        return -1;
    }

    juce::InterProcessLock::ScopedLockType scoped(stageRegion().processLock());
    if (!scoped.isLocked()) {
        instanceIdOut = 0;
        localOrderIdOut = 0;
        return -1;
    }

    const auto nowMs = static_cast<std::uint64_t>(juce::Time::currentTimeMillis());
    const int domainIndex = domainIndexFor(analysisDomainId);
    auto allocateSlot = [&](SharedStageSlot& slot) {
        analysisAtomicRef(slot.version).store(1u, std::memory_order_release);
        instanceIdOut = state->nextInstanceId++;
        localOrderIdOut = state->nextLocalOrderIds[static_cast<std::size_t>(domainIndex)]++;
        slot.analysisDomainId = analysisDomainId;
        setStageIdentityFromProduct(slot.telemetry.identity, identity, instanceIdOut, localOrderIdOut);
        slot.telemetry.state.timestampMs = nowMs;
        slot.telemetry.state.isLive = true;
        slot.telemetry.state.isSilent = true;
        analysisAtomicRef(slot.active).store(1u, std::memory_order_release);
        analysisAtomicRef(slot.version).store(2u, std::memory_order_release);
    };

    for (int slotIndex = 0; slotIndex < static_cast<int>(state->slots.size()); ++slotIndex) {
        auto& slot = state->slots[static_cast<std::size_t>(slotIndex)];
        if (analysisAtomicRef(slot.active).load(std::memory_order_acquire) != 0u)
            continue;
        allocateSlot(slot);
        return slotIndex;
    }

    for (int slotIndex = 0; slotIndex < static_cast<int>(state->slots.size()); ++slotIndex) {
        auto& slot = state->slots[static_cast<std::size_t>(slotIndex)];
        if (analysisAtomicRef(slot.active).load(std::memory_order_acquire) == 0u)
            continue;
        if ((nowMs - slot.telemetry.state.timestampMs) < static_cast<std::uint64_t>(kStageSlotReuseMs))
            continue;
        allocateSlot(slot);
        return slotIndex;
    }

    instanceIdOut = 0;
    localOrderIdOut = 0;
    return -1;
}

void StageRegistry::unregisterStage(const int slotIndex, const std::uint64_t instanceId) noexcept {
    auto* slot = stageSlotAt(slotIndex);
    if (slot == nullptr)
        return;

    juce::InterProcessLock::ScopedLockType scoped(stageRegion().processLock());
    if (!scoped.isLocked())
        return;

    if (analysisAtomicRef(slot->active).load(std::memory_order_acquire) == 0u
        || slot->telemetry.identity.instanceId != instanceId) {
        return;
    }

    const auto version = analysisAtomicRef(slot->version).load(std::memory_order_acquire);
    analysisAtomicRef(slot->version).store(version + 1u, std::memory_order_release);
    analysisAtomicRef(slot->active).store(0u, std::memory_order_release);
    slot->analysisDomainId = 0;
    slot->telemetry = {};
    analysisAtomicRef(slot->version).store(version + 2u, std::memory_order_release);
}

bool StageRegistry::publish(const int slotIndex,
                            const std::uint64_t instanceId,
                            const std::uint64_t analysisDomainId,
                            const StageTelemetry& telemetry) noexcept {
    auto* slot = stageSlotAt(slotIndex);
    if (slot == nullptr)
        return false;
    if (analysisAtomicRef(slot->active).load(std::memory_order_acquire) == 0u
        || slot->telemetry.identity.instanceId != instanceId) {
        return false;
    }

    const auto version = analysisAtomicRef(slot->version).load(std::memory_order_acquire);
    analysisAtomicRef(slot->version).store(version + 1u, std::memory_order_release);
    slot->analysisDomainId = analysisDomainId;
    slot->telemetry = telemetry;
    analysisAtomicRef(slot->version).store(version + 2u, std::memory_order_release);
    return true;
}

bool StageRegistry::readStage(const int slotIndex, StageView& out) const noexcept {
    auto* slot = stageSlotAt(slotIndex);
    if (slot == nullptr)
        return false;

    for (int attempt = 0; attempt < 4; ++attempt) {
        const auto versionStart = analysisAtomicRef(slot->version).load(std::memory_order_acquire);
        if ((versionStart & 1u) != 0u)
            continue;
        if (analysisAtomicRef(slot->active).load(std::memory_order_acquire) == 0u)
            return false;

        out.active = true;
        out.slotIndex = slotIndex;
        out.analysisDomainId = slot->analysisDomainId;
        out.telemetry = slot->telemetry;

        const auto versionEnd = analysisAtomicRef(slot->version).load(std::memory_order_acquire);
        if (versionStart == versionEnd && (versionEnd & 1u) == 0u)
            return true;
    }

    return false;
}

StagePublisher::StagePublisher(const ProductIdentity& identity)
    : identityDescriptor(identity),
      inputAccumulator(std::make_unique<SummaryAccumulator>()),
      outputAccumulator(std::make_unique<SummaryAccumulator>()) {
    refreshDomainBinding(true);
    ensureRegistered();
}

StagePublisher::~StagePublisher() {
    StageRegistry::instance().unregisterStage(slotIndex, instanceIdValue);
}

void StagePublisher::prepare(const double sampleRate, const int maxBlockSize) noexcept {
    juce::ignoreUnused(maxBlockSize);
    currentSampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    publishIntervalSamples = std::max(256, static_cast<int>(currentSampleRate / static_cast<double>(kTargetPublishHz)));
    samplesUntilPublish = publishIntervalSamples;
    domainRefreshCountdown = kDomainRefreshSamples;
    inputAccumulator->prepare(currentSampleRate, publishIntervalSamples);
    outputAccumulator->prepare(currentSampleRate, publishIntervalSamples);
    refreshDomainBinding(true);
    ensureRegistered();
}

void StagePublisher::reset() noexcept {
    if (inputAccumulator != nullptr)
        inputAccumulator->reset();
    if (outputAccumulator != nullptr)
        outputAccumulator->reset();
    samplesUntilPublish = publishIntervalSamples;
    domainRefreshCountdown = kDomainRefreshSamples;
}

void StagePublisher::publish(const juce::AudioBuffer<float>& inputBuffer,
                             const juce::AudioBuffer<float>& outputBuffer,
                             const bool bypassed) noexcept {
    refreshDomainBinding();
    ensureRegistered();
    if (slotIndex < 0 || inputAccumulator == nullptr || outputAccumulator == nullptr)
        return;

    inputAccumulator->update(inputBuffer);
    outputAccumulator->update(outputBuffer);
    samplesUntilPublish -= std::min(inputBuffer.getNumSamples(), outputBuffer.getNumSamples());
    domainRefreshCountdown -= std::min(inputBuffer.getNumSamples(), outputBuffer.getNumSamples());
    maybePublish(bypassed, std::max(inputBuffer.getNumChannels(), outputBuffer.getNumChannels()));
}

void StagePublisher::publishBypassed(const juce::AudioBuffer<float>& buffer) noexcept {
    publish(buffer, buffer, true);
}

void StagePublisher::ensureRegistered() noexcept {
    if (slotIndex >= 0)
        return;

    slotIndex = StageRegistry::instance().registerStage(identityDescriptor,
                                                        analysisDomainIdValue,
                                                        instanceIdValue,
                                                        localOrderIdValue);
}

void StagePublisher::refreshDomainBinding(const bool force) noexcept {
    if (!force && domainRefreshCountdown > 0)
        return;

    domainRefreshCountdown = kDomainRefreshSamples;
    DomainView domain;
    const auto& registry = DomainRegistry::instance();
    const bool foundDomain =
        registry.latestDomainForProcess(registry.currentProcessId(), domain)
        || registry.latestActiveDomain(domain);
    const auto newDomainId = foundDomain ? domain.analysisDomainId : 0;

    if (!force && newDomainId == analysisDomainIdValue)
        return;

    if (slotIndex >= 0)
        StageRegistry::instance().unregisterStage(slotIndex, instanceIdValue);

    slotIndex = -1;
    instanceIdValue = 0;
    localOrderIdValue = 0;
    analysisDomainIdValue = newDomainId;
}

void StagePublisher::maybePublish(const bool bypassed, const int numChannels) noexcept {
    if (slotIndex < 0 || samplesUntilPublish > 0)
        return;

    StageTelemetry telemetry;
    setStageIdentityFromProduct(telemetry.identity, identityDescriptor, instanceIdValue, localOrderIdValue);
    telemetry.state.timestampMs = static_cast<std::uint64_t>(juce::Time::currentTimeMillis());
    telemetry.state.isLive = true;
    telemetry.state.isBypassed = bypassed;
    telemetry.state.detailLevel = DetailLevel::tier1;
    telemetry.state.sampleRate = static_cast<float>(currentSampleRate);
    telemetry.state.numChannels = static_cast<std::uint8_t>(juce::jlimit(0, 255, numChannels));
    telemetry.inputSummary = inputAccumulator->summary();
    telemetry.outputSummary = outputAccumulator->summary();
    telemetry.state.isSilent = telemetry.inputSummary.rms <= 1.0e-6f && telemetry.outputSummary.rms <= 1.0e-6f;

    if (!StageRegistry::instance().publish(slotIndex, instanceIdValue, analysisDomainIdValue, telemetry)) {
        slotIndex = -1;
        instanceIdValue = 0;
        localOrderIdValue = 0;
        return;
    }

    samplesUntilPublish = publishIntervalSamples;
}

void StagePublisher::copyLabel(std::string_view source, char* dest, const std::size_t destSize) noexcept {
    copyFixedLabel(source, dest, destSize);
}

} // namespace vxsuite::analysis
