#pragma once

#include "../../framework/VxStudioBlockSmoothedControl.h"
#include "../../framework/VxStudioProcessorBase.h"
#include "dsp/VxDenoiserDsp.h"

class VXDenoiserAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXDenoiserAudioProcessor();
    ~VXDenoiserAudioProcessor() override = default;

    juce::String getStatusText() const override;
    int getActivityLightCount() const noexcept override { return 1; }
    float getActivityLight(int) const noexcept override;
    std::string_view getActivityLightLabel(int) const noexcept override { return "GR"; }

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();

    vxsuite::denoiser::DenoiserDsp denoiserDsp;
    double currentSampleRateHz = 48000.0;
    vxsuite::BlockSmoothedControlPair controls;
    float  smoothedMakeupGain  = 1.0f;
    bool prevPhraseActive = true;
};
