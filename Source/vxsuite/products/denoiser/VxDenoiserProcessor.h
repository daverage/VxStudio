#pragma once

#include "../../framework/VxSuiteBlockSmoothing.h"
#include "../../framework/VxSuiteBlockSmoothedControl.h"
#include "../../framework/VxSuiteEditorBase.h"
#include "../../framework/VxSuiteOutputTrimmer.h"
#include "../../framework/VxSuiteProcessorBase.h"
#include "dsp/VxDenoiserDsp.h"

#include <array>

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
    float aggregatedSignalPresence(int numChannels) const noexcept;

    vxsuite::denoiser::DenoiserDsp denoiserDspMono;
    vxsuite::denoiser::DenoiserDsp denoiserDspLeft;
    vxsuite::denoiser::DenoiserDsp denoiserDspRight;
    vxsuite::OutputTrimmer outputTrimmer;
    juce::AudioBuffer<float> leftScratch;
    juce::AudioBuffer<float> rightScratch;

    double currentSampleRateHz = 48000.0;
    vxsuite::BlockSmoothedControlPair controls;
    float  smoothedMakeupGain  = 1.0f;
    std::array<float, 2> smoothedStereoMakeupGain { 1.0f, 1.0f };
};
