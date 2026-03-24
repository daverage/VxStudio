#pragma once

#include "../../framework/VxSuiteBlockSmoothing.h"
#include "../../framework/VxSuiteEditorBase.h"
#include "../../framework/VxSuiteProcessorBase.h"
#include "../finish/dsp/VxFinishDsp.h"

class VXOptoCompAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXOptoCompAudioProcessor();
    ~VXOptoCompAudioProcessor() override = default;

    juce::String getStatusText() const override;
    int getActivityLightCount() const noexcept override;
    float getActivityLight(int index) const noexcept override;
    std::string_view getActivityLightLabel(int index) const noexcept override;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                            const juce::AudioBuffer<float>& inputBuffer) override;

private:
    static vxsuite::ProductIdentity makeIdentity();

    vxsuite::finish::Dsp optoDsp;
    double currentSampleRateHz = 48000.0;
    float smoothedPeakReduction = 0.0f;
    float smoothedBody = 0.5f;
    float smoothedGain = 0.5f;
    bool controlsPrimed = false;
};
