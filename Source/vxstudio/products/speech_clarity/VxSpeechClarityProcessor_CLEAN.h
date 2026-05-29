#pragma once

#include "../../framework/VxStudioProcessorBase.h"
#include "dsp/VxDeEsserDsp.h"
#include "dsp/VxDePolosiveDsp.h"
#include "dsp/VxDeBreathDsp.h"

class VXSpeechClarityAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXSpeechClarityAudioProcessor();

protected:
    static vxsuite::ProductIdentity makeIdentity();
    juce::String getStatusText() const override;
    int getActivityLightCount() const noexcept override { return 3; }
    float getActivityLight(int index) const noexcept override;
    std::string_view getActivityLightLabel(int index) const noexcept override;
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;
    void renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                          const juce::AudioBuffer<float>& inputBuffer) override;

private:
    // Detection state (LED intensity feedback: 0-1)
    float sibilanceDetectionIntensity = 0.0f;
    float plosiveDetectionIntensity = 0.0f;
    float breathDetectionIntensity = 0.0f;

    // Adaptive thresholds (established during pre-analysis)
    float sibilanceThreshold = 0.1f;
    float plosiveThreshold = 0.1f;
    float breathThreshold = 0.1f;

    // Detection state for filters
    struct DetectorState {
        float sibilanceEnvelope = 0.0f;
        float plosiveEnvelope = 0.0f;
        float breathEnvelope = 0.0f;
        bool needsPreAnalysis = true;
    } detectorState;

    // DSP Components
    vxsuite::speech_clarity::DeEsserDsp deEsserDsp;
    vxsuite::speech_clarity::DePolosiveDsp dePolosiveDsp;
    vxsuite::speech_clarity::DeBreathDsp deBreathDsp;

    // Detection implementation
    void performPreAnalysis(const juce::AudioBuffer<float>& buffer);
    void detectAndUpdateIntensities(const juce::AudioBuffer<float>& buffer);

    double currentSampleRateHz = 48000.0;
};
