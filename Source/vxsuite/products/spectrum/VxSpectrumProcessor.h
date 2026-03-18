#pragma once

#include "../../framework/VxSuiteProduct.h"
#include "../../framework/VxSuiteSpectrumTelemetry.h"

#include <juce_audio_processors/juce_audio_processors.h>

class VXSpectrumAudioProcessor final : public juce::AudioProcessor {
public:
    VXSpectrumAudioProcessor();
    ~VXSpectrumAudioProcessor() override = default;

    const juce::String getName() const override { return "VX Spectrum"; }
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

    [[nodiscard]] std::uint64_t telemetryInstanceId() const noexcept { return telemetryPublisher.instanceId(); }
    [[nodiscard]] const vxsuite::ProductTheme& theme() const noexcept { return identity.theme; }

private:
    static vxsuite::ProductIdentity makeIdentity();

    vxsuite::ProductIdentity identity;
    vxsuite::spectrum::SnapshotPublisher telemetryPublisher;
    juce::AudioBuffer<float> dryScratch;
};
