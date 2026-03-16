#pragma once

#include "../../framework/VxSuiteEditorBase.h"
#include "../../framework/VxSuiteProcessorBase.h"
#include "dsp/VxPolishDsp.h"

class VXPolishAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXPolishAudioProcessor();
    ~VXPolishAudioProcessor() override = default;

    const juce::String getName() const override;
    juce::String getStatusText() const override;
    float getLowShelfActivity()  const noexcept override;
    float getHighShelfActivity() const noexcept override;
    juce::AudioProcessorEditor* createEditor() override;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();
    void updateTonalAnalysis(const juce::AudioBuffer<float>& buffer, int numSamples);

    vxsuite::polish::Dsp polishChain;
    double currentSampleRateHz = 48000.0;
    float smoothedDetrouble = 0.0f;
    float smoothedBody = 0.0f;
    float smoothedFocus = 0.5f;
    float tonalLowLp = 0.0f;
    float tonalLowMidLp = 0.0f;
    float tonalPresenceLoLp = 0.0f;
    float tonalPresenceHiLp = 0.0f;
    float tonalAirLp = 0.0f;
    float tonalInputEnv = 0.0f;
    float tonalLowMidEnv = 0.0f;
    float tonalPresenceEnv = 0.0f;
    float tonalAirEnv = 0.0f;
    float tonalNoiseFloorDb = -80.0f;
    bool controlsPrimed = false;
};
