#pragma once

#include "../../framework/VxSuiteBlockSmoothing.h"
#include "../../framework/VxSuiteEditorBase.h"
#include "../../framework/VxSuiteProcessorBase.h"
#include "../../framework/VxSuiteStageChain.h"
#include "dsp/VxDenoiserDsp.h"

class VXDenoiserAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXDenoiserAudioProcessor();
    ~VXDenoiserAudioProcessor() override = default;

    juce::String getStatusText() const override;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();

    vxsuite::denoiser::DenoiserDsp denoiserDsp;
    vxsuite::StageChain<1> stageChain { denoiserDsp };

    double currentSampleRateHz = 48000.0;
    float  smoothedClean       = 0.0f;
    float  smoothedGuard       = 0.5f;
    bool   controlsPrimed      = false;
};
