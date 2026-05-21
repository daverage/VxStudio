#pragma once

#include "../../framework/VxStudioBlockSmoothing.h"
#include "../../framework/VxStudioEditorBase.h"
#include "../../framework/VxStudioModelAssets.h"
#include "../../framework/VxStudioProcessorBase.h"
#include "../../framework/VxStudioReadabilityGuard.h"
#include "../../framework/VxStudioTonalAnalysis.h"
#include "dsp/VxDeepFilterNetService.h"

#include <mutex>

class VXDeepFilterNetAudioProcessor final : public vxsuite::ProcessorBase,
                                            private juce::Timer {
public:
    VXDeepFilterNetAudioProcessor();
    ~VXDeepFilterNetAudioProcessor() override;

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
    void setNonRealtime(bool isNonRealtime) noexcept override;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();
    [[nodiscard]] vxsuite::ModelPackage currentModelPackage() const;

    using ModelVariant = vxsuite::deepfilternet::DeepFilterService::ModelVariant;

    void ensureAnalysisScratch(int channels, int samples);
    float estimateArtifactRisk(const juce::AudioBuffer<float>& dry,
                               const juce::AudioBuffer<float>& wet,
                               int channels,
                               int samples) const noexcept;
    void prepareEngineIfNeeded();
    ModelVariant selectedModelVariant() const noexcept;
    void timerCallback() override;
    void blendProcessedWithDry(juce::AudioBuffer<float>& buffer, float wetMix);

    vxsuite::deepfilternet::DeepFilterService engine;

    double currentSampleRateHz = 48000.0;
    int currentBlockSize = 0;
    float smoothedClean = 0.0f;
    float smoothedGuard = 0.5f;
    float startupWetRamp = 0.0f;
    float lastArtifactRisk = 0.0f;
    bool controlsPrimed = false;
    juce::AudioBuffer<float> analysisScratch;
    vxsuite::corrective::TonalAnalysisState tonalAnalysis;  // For ReadabilityGuard post-pass
    std::mutex enginePrepareMutex;
};
