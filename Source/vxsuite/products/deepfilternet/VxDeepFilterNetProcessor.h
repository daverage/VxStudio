#pragma once

#include "../../framework/VxSuiteEditorBase.h"
#include "../../framework/VxSuiteProcessorBase.h"
#include "dsp/VxDeepFilterNetService.h"

#include <vector>

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
    void renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                            const juce::AudioBuffer<float>& inputBuffer) override;

private:
    static vxsuite::ProductIdentity makeIdentity();
    static juce::AudioProcessorValueTreeState::ParameterLayout
           makeLayout(const vxsuite::ProductIdentity& identity);

    using ModelVariant = vxsuite::deepfilternet::DeepFilterService::ModelVariant;

    void ensureScratchCapacity(int channels, int samples);
    void fillAlignedDryScratch(const juce::AudioBuffer<float>& dryBuffer, int numSamples);
    void prepareEngineIfNeeded();
    ModelVariant selectedModelVariant() const noexcept;
    void timerCallback() override;

    vxsuite::deepfilternet::DeepFilterService engine;

    juce::AudioBuffer<float> dryScratch;
    juce::AudioBuffer<float> alignedDryScratch;
    std::vector<std::vector<float>> dryDelayLines;
    std::vector<int> dryDelayWritePos;

    double currentSampleRateHz = 48000.0;
    int currentBlockSize = 0;
    float smoothedClean = 0.0f;
    float smoothedGuard = 0.5f;
    bool controlsPrimed = false;
};
