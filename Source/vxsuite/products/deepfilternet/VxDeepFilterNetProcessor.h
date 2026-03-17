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

    const juce::String getName() const override;
    juce::String getStatusText() const override;
    juce::AudioProcessorEditor* createEditor() override;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();
    static juce::AudioProcessorValueTreeState::ParameterLayout
           makeLayout(const vxsuite::ProductIdentity& identity);

    using ModelVariant = vxsuite::deepfilternet::DeepFilterService::ModelVariant;

    void prepareEngineIfNeeded();
    ModelVariant selectedModelVariant() const noexcept;
    void timerCallback() override;

    vxsuite::deepfilternet::DeepFilterService engine;

    double currentSampleRateHz = 48000.0;
    int currentBlockSize = 0;
    float smoothedClean = 0.0f;
    float smoothedGuard = 0.5f;
    bool controlsPrimed = false;
};
