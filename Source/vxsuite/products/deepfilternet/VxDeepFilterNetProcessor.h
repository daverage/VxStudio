#pragma once

#include "../../framework/VxSuiteBlockSmoothing.h"
#include "../../framework/VxSuiteEditorBase.h"
#include "../../framework/VxSuiteProcessorBase.h"
#include "dsp/VxDeepFilterNetService.h"

class VXDeepFilterNetAudioProcessor final : public vxsuite::ProcessorBase,
                                            private juce::Timer {
public:
    VXDeepFilterNetAudioProcessor();
    ~VXDeepFilterNetAudioProcessor() override;

    juce::String getStatusText() const override;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();

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
