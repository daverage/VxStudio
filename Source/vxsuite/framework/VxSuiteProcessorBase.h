#pragma once

#include "VxSuiteParameters.h"
#include "VxSuiteProcessCoordinator.h"
#include "VxSuiteOutputTrimmer.h"
#include "VxSuiteSpectrumTelemetry.h"
#include "VxSuiteVoiceAnalysis.h"
#include "VxSuiteVoiceContext.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace vxsuite {

class ProcessorBase : public juce::AudioProcessor {
public:
    explicit ProcessorBase(ProductIdentity identity);
    ProcessorBase(ProductIdentity identity,
                  juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout);
    ~ProcessorBase() override;

    const ProductIdentity& getProductIdentity() const noexcept { return productIdentity; }
    const juce::String getName() const override {
        return "VX " + toJuceString(productIdentity.productName);
    }
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return parameters; }
    const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept { return parameters; }
    virtual juce::String getStatusText() const { return {}; }
    virtual float getLowShelfActivity() const noexcept { return 0.0f; }
    virtual float getHighShelfActivity() const noexcept { return 0.0f; }
    virtual int getActivityLightCount() const noexcept { return 0; }
    virtual float getActivityLight(int) const noexcept { return 0.0f; }
    virtual std::string_view getActivityLightLabel(int) const noexcept { return {}; }
    virtual float getLearnProgress() const noexcept { return 0.0f; }
    virtual float getLearnConfidence() const noexcept { return 0.0f; }
    virtual float getLearnObservedSeconds() const noexcept { return 0.0f; }
    virtual bool isLearnActive() const noexcept { return false; }
    virtual bool isLearnReady() const noexcept { return false; }
    VoiceAnalysisSnapshot getVoiceAnalysisSnapshot() const noexcept { return voiceAnalysis.snapshot(); }
    VoiceContextSnapshot getVoiceContextSnapshot() const noexcept { return voiceContext.snapshot(); }
    bool getSpectrumSnapshotView(spectrum::SnapshotView& out) const noexcept;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void reset() override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) final;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    double getTailLengthSeconds() const override { return tailLengthSeconds; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

protected:
    virtual void prepareSuite(double sampleRate, int samplesPerBlock) = 0;
    virtual void resetSuite() = 0;
    virtual void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) = 0;
    virtual void renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                    const juce::AudioBuffer<float>& inputBuffer);
    void renderAddedDeltaOutput(juce::AudioBuffer<float>& outputBuffer,
                                const juce::AudioBuffer<float>& inputBuffer) const noexcept;

    const ModePolicy& currentModePolicy() const noexcept;
    bool isListenEnabled() const noexcept;
    void prepareProcessCoordinator(int maxBlockSize);
    void resetProcessCoordinator();
    void releaseProcessCoordinator();
    void setReportedLatencySamples(int latencySamples);
    void setReportedTailLengthSeconds(double seconds) noexcept;
    template <typename... Stages>
    void setReportedLatencyFromStages(const Stages&... stages) {
        processCoordinator.setLatencyFromStages(stages...);
        juce::AudioProcessor::setLatencySamples(processCoordinator.latencySamples());
    }
    void ensureLatencyAlignedListenDry(int numSamples);
    const juce::AudioBuffer<float>& getLatencyAlignedListenDryBuffer() const noexcept;

    ProductIdentity productIdentity;
    juce::AudioProcessorValueTreeState parameters;
    VoiceAnalysisState voiceAnalysis;
    VoiceContextState voiceContext;
    spectrum::SnapshotPublisher spectrumPublisher;
    analysis::StagePublisher stagePublisher;

private:
    void processPreparedBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void processPreparedBypassedBlock(juce::AudioBuffer<float>& buffer) noexcept;
    juce::AudioBuffer<float> listenInputScratch;
    ProcessCoordinator processCoordinator;
    OutputTrimmer outputSafetyTrimmer;
    double currentSampleRateHz = 48000.0;
    double tailLengthSeconds = 0.0;
};

} // namespace vxsuite
