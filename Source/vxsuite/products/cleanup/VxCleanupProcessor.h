#pragma once

#include "../../framework/VxSuiteBlockSmoothing.h"
#include "../../framework/VxSuiteFft.h"
#include "../../framework/VxSuiteSpectralHelpers.h"
#include "../../framework/VxSuiteEditorBase.h"
#include "../../framework/VxSuiteProcessorBase.h"
#include "../polish/VxPolishAnalysisEvidence.h"
#include "../polish/VxPolishTonalAnalysis.h"
#include "dsp/VxCleanupDsp.h"

#include <vector>

class VXCleanupAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXCleanupAudioProcessor();
    ~VXCleanupAudioProcessor() override = default;

    const juce::String getName() const override;
    juce::String getStatusText() const override;
    float getLowShelfActivity() const noexcept override;
    float getHighShelfActivity() const noexcept override;
    int getActivityLightCount() const noexcept override;
    float getActivityLight(int index) const noexcept override;
    std::string_view getActivityLightLabel(int index) const noexcept override;
    juce::AudioProcessorEditor* createEditor() override;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();

    vxsuite::cleanup::Dsp polishChain;
    vxsuite::polish::TonalAnalysisState tonalAnalysis;
    vxsuite::RealFft spectralFft;
    std::vector<float> spectralFifo;
    std::vector<float> spectralWindow;
    std::vector<float> spectralFrame;
    int spectralOrder = 0;
    int spectralSize = 0;
    int spectralWritePos = 0;
    int spectralSamplesReady = 0;
    double currentSampleRateHz = 48000.0;
    float smoothedCleanup = 0.0f;
    float smoothedBody = 0.5f;
    float smoothedFocus = 0.5f;
    float spectralFlatness = 0.0f;
    float harmonicity = 0.0f;
    float highFreqRatio = 0.0f;
    float breathEnv = 0.0f;
    float sibilanceEnv = 0.0f;
    float plosiveEnv = 0.0f;
    float tonalMudEnv = 0.0f;
    float harshnessEnv = 0.0f;
    bool classifiersPrimed = false;
    bool controlsPrimed = false;
};
