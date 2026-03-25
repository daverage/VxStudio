#pragma once

#include "../../framework/VxSuiteProcessorBase.h"
#include "../../framework/VxSuiteModelAssets.h"
#include "dsp/VxRebalanceDsp.h"
#include "ml/VxRebalanceModelRunner.h"

#include <array>
#include <vector>

class VXRebalanceAudioProcessor final : public vxsuite::ProcessorBase,
                                        private juce::Timer {
public:
    VXRebalanceAudioProcessor();
    ~VXRebalanceAudioProcessor() override;
    juce::String getStatusText() const override;
    bool supportsModelDownloadUi() const noexcept override { return true; }
    bool isModelReadyForUi() const noexcept override;
    bool isModelDownloadInProgress() const noexcept override;
    float getModelDownloadProgress() const noexcept override;
    bool shouldPromptForModelDownload() const noexcept override;
    juce::String getModelDownloadButtonText() const override;
    juce::String getModelDownloadPromptTitle() const override;
    juce::String getModelDownloadPromptBody() const override;
    void requestModelDownload() override;
    void declineModelDownloadPrompt() override;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();
    [[nodiscard]] static vxsuite::ModelPackage modelPackage();
    void processNeutralWithLatency(juce::AudioBuffer<float>& buffer);
    void timerCallback() override;

    vxsuite::rebalance::Dsp dsp;
    vxsuite::rebalance::ml::ModelRunner modelRunner;
    std::array<float, vxsuite::rebalance::Dsp::kControlCount> smoothedControls {};
    bool controlsPrimed = false;
    double currentSampleRateHz = 48000.0;
    int currentBlockSize = 0;
    bool observedModelReady = false;
    std::vector<std::vector<float>> dryDelayLines;
    int dryDelayWritePos = 0;
};
