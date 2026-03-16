#pragma once

#include "../../framework/VxSuiteEditorBase.h"
#include "../../framework/VxSuiteProcessorBase.h"
#include "dsp/VxProximityDsp.h"

class VXProximityAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXProximityAudioProcessor();
    ~VXProximityAudioProcessor() override = default;

    const juce::String getName() const override;
    juce::String getStatusText() const override;
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

    vxsuite::proximity::ProximityDsp proximityDsp;

    float  smoothedCloser      = 0.f;
    float  smoothedAir         = 0.f;
    double currentSampleRateHz = 48000.0;
    bool   controlsPrimed      = false;
};
