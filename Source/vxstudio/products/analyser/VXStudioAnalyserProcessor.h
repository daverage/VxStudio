#pragma once

#include "../../framework/VxStudioProduct.h"
#include "../../framework/VxStudioSignalQuality.h"
#include "../../framework/VxStudioSpectrumTelemetry.h"

#include <atomic>
#include <mutex>
#include <juce_audio_processors/juce_audio_processors.h>

class VXStudioAnalyserAudioProcessor final : public juce::AudioProcessor {
public:
    VXStudioAnalyserAudioProcessor();
    ~VXStudioAnalyserAudioProcessor() override;

    const juce::String getName() const override { return "VX Studio Analyser"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    void updateTrackProperties(const TrackProperties& properties) override;

    [[nodiscard]] std::uint64_t analysisDomainId() const noexcept { return analysisDomainIdValue; }
    [[nodiscard]] std::uint64_t trackStableId()    const noexcept { return analyserTrackStableId.load(std::memory_order_relaxed); }
    [[nodiscard]] juce::String  trackDisplayName() const          { return analyserTrackName; }
    [[nodiscard]] const vxsuite::ProductTheme& theme() const noexcept { return identity.theme; }
    [[nodiscard]] const vxsuite::ProductIdentity& getProductIdentity() const noexcept { return identity; }
    [[nodiscard]] juce::String stageIdString() const { return juce::String(identity.stageId.data(), static_cast<int>(identity.stageId.size())); }
    [[nodiscard]] vxsuite::SignalQualitySnapshot getSignalQualitySnapshot() const noexcept;

    // Live input summary: captured in processBlock from the analyser's own audio input.
    [[nodiscard]] vxsuite::analysis::AnalysisSummary liveInputSummary() const noexcept;
    [[nodiscard]] bool liveInputSummaryValid() const noexcept;
    [[nodiscard]] bool isAnalyserActive() const noexcept { return !isBypassed.load(std::memory_order_relaxed); }

private:
    static vxsuite::ProductIdentity makeIdentity();
    void ensureAnalysisDomain() noexcept;
    void publishSignalQualitySnapshot() noexcept;
    void publishLiveInputSummary() noexcept;

    vxsuite::ProductIdentity identity;
    std::uint64_t analysisDomainIdValue = 0;
    vxsuite::SignalQualityState signalQualityState;
    std::atomic<float>         monoScore             { 0.0f };
    std::atomic<float>         compressionScore      { 0.0f };
    std::atomic<float>         tiltScore             { 0.0f };
    std::atomic<float>         separationConfidence  { 1.0f };
    std::atomic<std::uint64_t> analyserTrackStableId { 0 };
    std::atomic<bool>          isBypassed            { false };
    juce::String analyserTrackName;  // message thread only

    // Live input capture — accumulates spectrum/RMS on the audio thread.
    // Snapshot protected by a seqlock (liveInputVersion odd = writing, even = done).
    std::unique_ptr<vxsuite::analysis::SummaryAccumulator> liveAccumulator;
    std::atomic<uint32_t> liveInputVersion        { 0 };
    vxsuite::analysis::AnalysisSummary liveInputSnapshot;
    std::atomic<bool>     liveInputSnapshotValid  { false };
};
