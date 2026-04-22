#pragma once

#include "../../framework/VxStudioBlockSmoothedControl.h"
#include "../../framework/VxStudioProcessorBase.h"
#include "dsp/VxClarityDsp.h"

class VXClarityAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXClarityAudioProcessor();
    ~VXClarityAudioProcessor() override = default;

    juce::String getStatusText() const override;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

private:
    static vxsuite::ProductIdentity makeIdentity();
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();
    static juce::AudioProcessor::BusesProperties makeBusesProperties();

    vxsuite::BlockSmoothedControlPair controls;
    vxsuite::clarity::ClarityDsp dsp;
    double currentSampleRateHz = 48000.0;
};
