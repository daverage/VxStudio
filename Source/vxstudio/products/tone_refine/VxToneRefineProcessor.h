#pragma once

#include "../../framework/VxStudioProcessorBase.h"
#include "../../framework/VxStudioDspCommon.h"
#include "dsp/VxDeMudDsp.h"
#include "dsp/VxDeHarshnessDsp.h"
#include "dsp/VxIntelligentSmoothDsp.h"

class VXToneRefineAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXToneRefineAudioProcessor();

protected:
    static vxsuite::ProductIdentity makeIdentity();
    juce::String getStatusText() const override;
    int getActivityLightCount() const noexcept override { return 3; }
    float getActivityLight(int index) const noexcept override;
    std::string_view getActivityLightLabel(int index) const noexcept override;
    vxsuite::MeteringSnapshot getMeteringSnapshot() const noexcept override;
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;
    void renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                          const juce::AudioBuffer<float>& inputBuffer) override;

private:
    // Detection state (LED intensity feedback: 0-1)
    float mudDetectionIntensity = 0.0f;
    float harshnessDetectionIntensity = 0.0f;
    float roughnessDetectionIntensity = 0.0f;

    // Adaptive thresholds (established during pre-analysis)
    float mudThreshold = 0.1f;
    float harshnessThreshold = 0.1f;
    float roughnessThreshold = 0.1f;

    // Detection state for analysis
    struct DetectorState {
        float mudEnvelope = 0.0f;
        float harshnessEnvelope = 0.0f;
        float roughnessMetric = 0.0f;
        bool needsPreAnalysis = true;
    } detectorState;

    // DSP Components
    vxsuite::tone_refine::DeMudDsp deMudDsp;
    vxsuite::tone_refine::DeHarshnessDsp deHarshnessDsp;
    vxsuite::tone_refine::IntelligentSmoothDsp intelligentSmoothDsp;

    // HPF state (2nd-order Butterworth, coefficients recomputed on mode/toggle change)
    float hpfB0 = 1.0f, hpfB1 = 0.0f, hpfB2 = 0.0f, hpfA1 = 0.0f, hpfA2 = 0.0f;
    std::vector<float> hpfZ1, hpfZ2;

    // Hi-shelf state (gentle air cut)
    vxsuite::corrective::detail::BiquadCoeffs hiShelfCoeffs;
    std::vector<float> hiShelfZ1, hiShelfZ2;

    bool lastHpfOn = false;
    bool lastHiShelfOn = false;

    void rebuildFilters(bool hpfOn, bool hiShelfOn, bool voiceMode);

    // Detection implementation
    void performPreAnalysis(const juce::AudioBuffer<float>& buffer);
    void detectAndUpdateIntensities(const juce::AudioBuffer<float>& buffer);

    double currentSampleRateHz = 48000.0;
};
