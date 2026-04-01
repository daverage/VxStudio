#pragma once

#include "../../framework/VxStudioBlockSmoothing.h"
#include "../../framework/VxStudioEditorBase.h"
#include "../../framework/VxStudioModelAssets.h"
#include "../../framework/VxStudioProcessorBase.h"
#include "dsp/VxDeepFilterNetService.h"

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

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();
    [[nodiscard]] vxsuite::ModelPackage currentModelPackage() const;

    using ModelVariant = vxsuite::deepfilternet::DeepFilterService::ModelVariant;

    void prepareEngineIfNeeded();
    ModelVariant selectedModelVariant() const noexcept;
    void timerCallback() override;
    void blendProcessedWithDry(juce::AudioBuffer<float>& buffer, float wetMix);

    vxsuite::deepfilternet::DeepFilterService engine;

    double currentSampleRateHz = 48000.0;
    int currentBlockSize = 0;
    float smoothedClean = 0.0f;
    float smoothedGuard = 0.5f;
    bool controlsPrimed = false;
};
