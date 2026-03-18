#include "VxSuiteSpectrumTelemetry.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <memory>

#include <juce_core/juce_core.h>

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
    }
    samplesUntilPublish = publishIntervalSamples;
}

void SnapshotPublisher::publishSilence() noexcept {
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
