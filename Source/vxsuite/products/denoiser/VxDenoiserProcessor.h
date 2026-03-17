#pragma once

#include "../../framework/VxSuiteBlockSmoothing.h"
#include "../../framework/VxSuiteEditorBase.h"
#include "../../framework/VxSuiteLatencyAlignedListen.h"
#include "../../framework/VxSuiteProcessorBase.h"
#include "dsp/VxDenoiserDsp.h"

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

    vxsuite::denoiser::DenoiserDsp denoiserDsp;
    vxsuite::LatencyAlignedListenBuffer latencyListen;

    double currentSampleRateHz = 48000.0;
    float  smoothedClean       = 0.0f;
    float  smoothedGuard       = 0.5f;
    bool   controlsPrimed      = false;
};
