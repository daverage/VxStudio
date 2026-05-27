#pragma once

#include "../../framework/VxStudioBlockSmoothedControl.h"
#include "../../framework/VxStudioBlockSmoothing.h"
#include "../../framework/VxStudioEditorBase.h"
#include "../../framework/VxStudioOutputTrimmer.h"
#include "../../framework/VxStudioProcessorBase.h"
#include "dsp/VxProximity2Dsp.h"

class VXProximity2AudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXProximity2AudioProcessor();
    ~VXProximity2AudioProcessor() override = default;

    juce::String getStatusText() const override;
    float getLocalOutputTrimMaxReductionDb() const noexcept { return outputTrimmer.getMaxObservedReductionDb(); }

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                            const juce::AudioBuffer<float>& inputBuffer) override;

private:
    static vxsuite::ProductIdentity makeIdentity();

    vxsuite::proximity2::Proximity2Dsp proximityDsp;
    vxsuite::BlockSmoothedControlPair controls;
    vxsuite::OutputTrimmer outputTrimmer;
    double currentSampleRateHz = 48000.0;
};
