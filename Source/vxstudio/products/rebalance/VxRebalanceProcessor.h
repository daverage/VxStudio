#pragma once

#include "../../framework/VxStudioOutputTrimmer.h"
#include "../../framework/VxStudioSilenceGuard.h"
#include "../../framework/VxStudioProcessorBase.h"
#include "dsp/VxRebalanceDsp.h"

#if VXSTUDIO_REBALANCE_AI_VARIANT
#include "ai/VxRealtimeStemSplitter.h"
#endif

#include <array>
#include <vector>

class VXRebalanceAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXRebalanceAudioProcessor();
    ~VXRebalanceAudioProcessor() override;
    juce::AudioProcessorEditor* createEditor() override;
    juce::String getStatusText() const override;
    vxsuite::rebalance::Dsp::DebugSnapshot getDebugSnapshot() const noexcept;
#if VXSTUDIO_REBALANCE_AI_VARIANT
    vxsuite::rebalance::ai::RealtimeStemSplitter::DebugSnapshot getAiDebugSnapshot() const noexcept;
    juce::String getAiStatusText() const;
#endif
    float getLocalOutputTrimMaxReductionDb() const noexcept { return outputTrimmer.getMaxObservedReductionDb(); }

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameterLayout();
    void processNeutralWithLatency(juce::AudioBuffer<float>& buffer);

    vxsuite::rebalance::Dsp dsp;
#if VXSTUDIO_REBALANCE_AI_VARIANT
    vxsuite::rebalance::ai::RealtimeStemSplitter realtimeSplitter;
#endif
    vxsuite::OutputTrimmer outputTrimmer;
    vxsuite::SilenceGuard silenceGuard;
    double currentSampleRateHz = 48000.0;
    int currentBlockSize = 0;
    std::vector<std::vector<float>> dryDelayLines;
    int dryDelayWritePos = 0;
    bool wasNeutral = false;
    float smoothedOutputTrimDb = 0.0f;
    bool outputTrimPrimed = false;
};
