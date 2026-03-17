#pragma once

#include "../../framework/VxSuiteBlockSmoothing.h"
#include "../../framework/VxSuiteEditorBase.h"
#include "../../framework/VxSuiteProcessorBase.h"
#include "../polish/VxPolishAnalysisEvidence.h"
#include "../polish/VxPolishTonalAnalysis.h"
#include "dsp/VxFinishDsp.h"

class VXFinishAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXFinishAudioProcessor();
    ~VXFinishAudioProcessor() override = default;

    const juce::String getName() const override;
    juce::String getStatusText() const override;
    int getActivityLightCount() const noexcept override;
    float getActivityLight(int index) const noexcept override;
    std::string_view getActivityLightLabel(int index) const noexcept override;
    juce::AudioProcessorEditor* createEditor() override;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                            const juce::AudioBuffer<float>& inputBuffer) override;

private:
    static vxsuite::ProductIdentity makeIdentity();

    vxsuite::finish::Dsp polishChain;
    vxsuite::polish::TonalAnalysisState tonalAnalysis;
    double currentSampleRateHz = 48000.0;
    float smoothedFinish = 0.0f;
    float smoothedBody = 0.5f;
    float smoothedGain = 0.5f;
    float smoothedTargetGainDb = 0.0f;
    bool controlsPrimed = false;
};
