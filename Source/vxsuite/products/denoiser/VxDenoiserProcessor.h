#pragma once

#include "../../framework/VxSuiteEditorBase.h"
#include "../../framework/VxSuiteProcessorBase.h"
#include "dsp/VxDenoiserDsp.h"

#include <vector>

class VXDenoiserAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXDenoiserAudioProcessor();
    ~VXDenoiserAudioProcessor() override = default;

    const juce::String getName() const override;
    juce::String       getStatusText() const override;
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

    void ensureScratchCapacity(int channels, int samples);
    void fillAlignedDryScratch(const juce::AudioBuffer<float>& dryBuffer, int numSamples);

    vxsuite::denoiser::DenoiserDsp denoiserDsp;

    // Latency-aligned dry scratch — needed for listen mode and future wet blend
    juce::AudioBuffer<float> dryScratch;
    juce::AudioBuffer<float> alignedDryScratch;
    std::vector<std::vector<float>> dryDelayLines;
    std::vector<int>                dryDelayWritePos;

    double currentSampleRateHz = 48000.0;
    float  smoothedClean       = 0.0f;
    float  smoothedGuard       = 0.5f;
    bool   controlsPrimed      = false;
};
