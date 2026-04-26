#pragma once

#include "../../framework/VxStudioBlockSmoothing.h"
#include "../../framework/VxStudioBlockSmoothedControl.h"
#include "../../framework/VxStudioFft.h"
#include "../../framework/VxStudioOutputTrimmer.h"
#include "../../framework/VxStudioSpectralHelpers.h"
#include "../../framework/VxStudioEditorBase.h"
#include "../../framework/VxStudioProcessorBase.h"
#include "../../framework/VxStudioAnalysisEvidence.h"
#include "../../framework/VxStudioReadabilityGuard.h"
#include "../../framework/VxStudioTonalAnalysis.h"
#include "dsp/VxClarityDsp.h"
#include "dsp/VxCleanupDsp.h"

#include <vector>

class VXCleanupAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXCleanupAudioProcessor();
    ~VXCleanupAudioProcessor() override = default;

    juce::String getStatusText() const override;
    float getLowShelfActivity() const noexcept override;
    float getHighShelfActivity() const noexcept override;
    int getActivityLightCount() const noexcept override;
    float getActivityLight(int index) const noexcept override;
    std::string_view getActivityLightLabel(int index) const noexcept override;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();

    vxsuite::cleanup::Dsp correctiveChain;
    vxsuite::clarity::ClarityDsp persistentCleanupStage;
    vxsuite::corrective::TonalAnalysisState tonalAnalysis;
    vxsuite::RealFft spectralFft;
    std::vector<float> spectralFifo;
    std::vector<float> spectralWindow;
    std::vector<float> spectralFrame;
    int spectralOrder = 0;
    int spectralSize = 0;
    int spectralWritePos = 0;
    int spectralSamplesReady = 0;
    double currentSampleRateHz = 48000.0;
    vxsuite::BlockSmoothedControlTriple controls;
    float spectralFlatness = 0.0f;
    float harmonicity = 0.0f;
    float highFreqRatio = 0.0f;
    float breathEnv = 0.0f;
    float sibilanceEnv = 0.0f;
    float plosiveEnv = 0.0f;
    float tonalMudEnv = 0.0f;
    float harshnessEnv = 0.0f;
    float persistentLowMidDensity = 0.0f;
    float shortLowMidDensity = 0.0f;
    float persistentPresenceDensity = 0.0f;
    float shortPresenceDensity = 0.0f;
    std::vector<float> assertiveLowState;
    std::vector<float> assertivePresenceState;
    vxsuite::OutputTrimmer outputTrimmer;
    float smoothedMakeupGain = 1.0f;
    bool classifiersPrimed = false;
};
