#pragma once

#include "VxSuiteProduct.h"

#include <array>
#include <cstdint>
#include <string_view>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace vxsuite::spectrum {

constexpr int kMaxTelemetrySlots = 24;
constexpr int kWaveformSamples = 512;
constexpr int kHistorySamples = 2048;

struct SnapshotView {
    bool active = false;
    bool silent = false;
    bool showTrace = false;
    int slotIndex = -1;
    int order = 0;
    std::uint64_t instanceId = 0;
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
                               float dryRms,
                               float wetRms,
                               bool silent,
                               const std::array<float, kWaveformSamples>& dryWaveform,
                               const std::array<float, kWaveformSamples>& wetWaveform) noexcept;

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
    std::array<float, kHistorySamples> dryHistory {};
    std::array<float, kHistorySamples> wetHistory {};
    std::array<float, kWaveformSamples> dryWaveform {};
    std::array<float, kWaveformSamples> wetWaveform {};
};

} // namespace vxsuite::spectrum
