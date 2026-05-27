#pragma once

#include "../../framework/VxStudioBlockSmoothedControl.h"
#include "../../framework/VxStudioBlockSmoothing.h"
#include "../../framework/VxStudioEditorBase.h"
#include "../../framework/VxStudioOutputTrimmer.h"
#include "../../framework/VxStudioProcessorBase.h"
#include "dsp/VxProximityDsp.h"

class VXProximityAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXProximityAudioProcessor();
    ~VXProximityAudioProcessor() override = default;

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

    vxsuite::proximity::ProximityDsp proximityDsp;
    vxsuite::BlockSmoothedControlTriple controls;
    vxsuite::OutputTrimmer outputTrimmer;
    double currentSampleRateHz = 48000.0;
};
