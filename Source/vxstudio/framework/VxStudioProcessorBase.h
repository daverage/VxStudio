#pragma once

#include "VxStudioMeteringSnapshot.h"
#include "VxStudioParameters.h"
#include "VxStudioProcessCoordinator.h"
#include "VxStudioOutputTrimmer.h"
#include "VxStudioSignalQuality.h"
#include "VxStudioSpectrumTelemetry.h"
#include "VxStudioVoiceAnalysis.h"
#include "VxStudioVoiceContext.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace vxsuite {

class ProcessorBase : public juce::AudioProcessor {
public:
    explicit ProcessorBase(ProductIdentity identity);
    ProcessorBase(ProductIdentity identity,
                  juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout);
    ProcessorBase(ProductIdentity identity,
                  juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout,
                  juce::AudioProcessor::BusesProperties busesProperties);
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
    virtual bool shouldShowLearnUi() const noexcept { return productIdentity.supportsLearnButton(); }
    virtual bool supportsModelDownloadUi() const noexcept { return false; }
    virtual bool isModelReadyForUi() const noexcept { return true; }
    virtual bool isModelDownloadInProgress() const noexcept { return false; }
    virtual float getModelDownloadProgress() const noexcept { return 0.0f; }
    virtual bool shouldPromptForModelDownload() const noexcept { return false; }
    virtual juce::String getModelDownloadButtonText() const { return "Download Model"; }
    virtual juce::String getModelDownloadPromptTitle() const { return {}; }
    virtual juce::String getModelDownloadPromptBody() const { return {}; }
    virtual void requestModelDownload() {}
    virtual void declineModelDownloadPrompt() {}
    virtual bool hasSidechainActive() const noexcept { return false; }
    virtual MeteringSnapshot getMeteringSnapshot() const noexcept { return {}; }
    // Optional gain trajectory (e.g. a rider's fader, gain reduction) drawn as
    // a thin overlay in the shared level-trace view. Return the current gain
    // in dB; must be safe to call from the UI thread (store atomically).
    virtual bool hasGainTrace() const noexcept { return false; }
    virtual float getGainTraceDb() const noexcept { return 0.0f; }
    // Openness of the gain trace's gate/hold state [0,1]; segments below 0.5
    // are drawn dimmed (fader parked). Products without a gate return 1.
    virtual float getGainTraceOpenness() const noexcept { return 1.0f; }
    // Optional target/reference level (dBFS) drawn as a faint line on the
    // level trace - shows what the processing is steering toward.
    virtual bool hasReferenceTrace() const noexcept { return false; }
    virtual float getReferenceTraceDb() const noexcept { return -100.0f; }
    // Pitch trace feed (identity.showPitchTrace): detected and corrected
    // pitch in cents vs A440, one sample per UI tick. Confidence <= 0 marks
    // unvoiced. Must be UI-thread safe (store atomically).
    virtual float getPitchTraceDetectedCents() const noexcept { return 0.0f; }
    virtual float getPitchTraceCorrectedCents() const noexcept { return 0.0f; }
    virtual float getPitchTraceConfidence() const noexcept { return 0.0f; }
    float getOutputSafetyTrimReductionDb() const noexcept { return outputSafetyTrimmer.getCurrentReductionDb(); }
    float getOutputSafetyTrimMaxReductionDb() const noexcept { return outputSafetyTrimmer.getMaxObservedReductionDb(); }
    float getOutputSafetyTrimActivity() const noexcept { return outputSafetyTrimmer.getCurrentActivity01(); }
    float getOutputSafetyTrimMaxActivity() const noexcept { return outputSafetyTrimmer.getMaxObservedActivity01(); }
    VoiceAnalysisSnapshot getVoiceAnalysisSnapshot() const noexcept { return voiceAnalysis.snapshot(); }
    VoiceContextSnapshot getVoiceContextSnapshot() const noexcept { return voiceContext.snapshot(); }
    SignalQualitySnapshot getSignalQualitySnapshot() const noexcept { return signalQuality.snapshot(); }
    bool getSpectrumSnapshotView(spectrum::SnapshotView& out) const noexcept;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void reset() override;
    void releaseResources() override;
    void updateTrackProperties(const TrackProperties& props) override;
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
    // Sidechain input for this block (nullptr when the product doesn't
    // request one, the host hasn't enabled/connected the bus, or outside
    // processProduct). Detection-only; never routed to the output.
    const juce::AudioBuffer<float>* getSidechainBuffer() const noexcept {
        return activeSidechain;
    }

    static juce::AudioProcessor::BusesProperties makeDefaultBuses(bool wantsSidechain);

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
    void resetOutputSafetyTrimmer() noexcept;

    ProductIdentity productIdentity;
    juce::AudioProcessorValueTreeState parameters;
    VoiceAnalysisState voiceAnalysis;
    VoiceContextState voiceContext;
    SignalQualityState signalQuality;
    spectrum::SnapshotPublisher spectrumPublisher;
    analysis::StagePublisher stagePublisher;

private:
    void processPreparedBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void processPreparedBypassedBlock(juce::AudioBuffer<float>& buffer) noexcept;
    juce::AudioBuffer<float> listenInputScratch;
    juce::AudioBuffer<float> sidechainView;   // non-owning per-chunk view
    const juce::AudioBuffer<float>* activeSidechain = nullptr;
    ProcessCoordinator processCoordinator;
    OutputTrimmer outputSafetyTrimmer;
    double currentSampleRateHz = 48000.0;
    double tailLengthSeconds = 0.0;
};

} // namespace vxsuite
