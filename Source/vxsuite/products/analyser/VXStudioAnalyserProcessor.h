#pragma once

#include "../../framework/VxSuiteProduct.h"
#include "../../framework/VxSuiteSignalQuality.h"
#include "../../framework/VxSuiteSpectrumTelemetry.h"

#include <atomic>
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

    [[nodiscard]] std::uint64_t analysisDomainId() const noexcept { return analysisDomainIdValue; }
    [[nodiscard]] const vxsuite::ProductTheme& theme() const noexcept { return identity.theme; }
    [[nodiscard]] const vxsuite::ProductIdentity& getProductIdentity() const noexcept { return identity; }
    [[nodiscard]] juce::String stageIdString() const { return juce::String(identity.stageId.data(), static_cast<int>(identity.stageId.size())); }
    [[nodiscard]] vxsuite::SignalQualitySnapshot getSignalQualitySnapshot() const noexcept;

private:
    static vxsuite::ProductIdentity makeIdentity();
    void publishSignalQualitySnapshot() noexcept;

    vxsuite::ProductIdentity identity;
    std::uint64_t analysisDomainIdValue = 0;
    vxsuite::analysis::StagePublisher stagePublisher;
    vxsuite::SignalQualityState signalQualityState;
    std::atomic<float> monoScore { 0.0f };
    std::atomic<float> compressionScore { 0.0f };
    std::atomic<float> tiltScore { 0.0f };
    std::atomic<float> separationConfidence { 1.0f };
};
