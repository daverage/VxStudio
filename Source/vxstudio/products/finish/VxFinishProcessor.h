#pragma once

#include "../../framework/VxStudioBlockSmoothing.h"
#include "../../framework/VxStudioBlockSmoothedControl.h"
#include "../../framework/VxStudioEditorBase.h"
#include "../../framework/VxStudioProcessorBase.h"
#include "dsp/VxFinishDsp.h"

class VXFinishAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXFinishAudioProcessor();
    ~VXFinishAudioProcessor() override = default;

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

    vxsuite::finish::Dsp polishChain;
    double currentSampleRateHz = 48000.0;
    vxsuite::BlockSmoothedControlTriple controls;
};
