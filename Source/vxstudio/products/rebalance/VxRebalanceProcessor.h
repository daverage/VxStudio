#pragma once

#include "../../framework/VxStudioProcessorBase.h"
#include "dsp/VxRebalanceDsp.h"

#include <array>
#include <vector>

class VXRebalanceAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXRebalanceAudioProcessor();
    ~VXRebalanceAudioProcessor() override;
    juce::AudioProcessorEditor* createEditor() override;
    juce::String getStatusText() const override;
    vxsuite::rebalance::Dsp::DebugSnapshot getDebugSnapshot() const noexcept;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();
    void processNeutralWithLatency(juce::AudioBuffer<float>& buffer);

    vxsuite::rebalance::Dsp dsp;
    double currentSampleRateHz = 48000.0;
    int currentBlockSize = 0;
    std::vector<std::vector<float>> dryDelayLines;
    int dryDelayWritePos = 0;
    bool wasNeutral = false;
};
