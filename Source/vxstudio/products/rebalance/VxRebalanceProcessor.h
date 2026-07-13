#pragma once

#include "../../framework/VxStudioOutputTrimmer.h"
#include "../../framework/VxStudioSilenceGuard.h"
#include "../../framework/VxStudioProcessorBase.h"
#include "VxRebalanceTypes.h"

#if VXSTUDIO_REBALANCE_AI_VARIANT
#include "ai/VxAiStemRebalanceDsp.h"
#include "ai/VxRealtimeStemSplitter.h"
#else
#include "dsp/VxRebalanceDsp.h"
#endif

#include <array>
#include <vector>

class VXRebalanceAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXRebalanceAudioProcessor();
    ~VXRebalanceAudioProcessor() override;
    juce::AudioProcessorEditor* createEditor() override;
    juce::String getStatusText() const override;
    vxsuite::rebalance::DebugSnapshot getDebugSnapshot() const noexcept;
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

#if VXSTUDIO_REBALANCE_AI_VARIANT
    vxsuite::rebalance::ai::StemRebalanceDsp aiDsp;
    vxsuite::rebalance::ai::RealtimeStemSplitter realtimeSplitter;
#else
    vxsuite::rebalance::Dsp dsp;
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
