#pragma once

#include "../../framework/VxStudioProcessorBase.h"
#include "../../framework/VxStudioArtifactDetectors.h"
#include "dsp/VxDeClickDsp.h"
#include "dsp/VxDeEsserDsp.h"
#include "dsp/VxDePolosiveDsp.h"
#include "dsp/VxDeBreathDsp.h"

class VXSpeechClarityAudioProcessor final : public vxsuite::ProcessorBase,
                                            private juce::AsyncUpdater {
public:
    VXSpeechClarityAudioProcessor();
    ~VXSpeechClarityAudioProcessor() override { cancelPendingUpdate(); }

protected:
    static vxsuite::ProductIdentity makeIdentity();
    juce::String getStatusText() const override;
    int getActivityLightCount() const noexcept override;
    float getActivityLight(int index) const noexcept override;
    std::string_view getActivityLightLabel(int index) const noexcept override;
    vxsuite::MeteringSnapshot getMeteringSnapshot() const noexcept override;
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override;
    void renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                          const juce::AudioBuffer<float>& inputBuffer) override;

private:
    // Detection state
    struct DetectionState {
        float clickIntensity    = 0.0f;
        float sibilanceIntensity = 0.0f;
        float plosiveIntensity  = 0.0f;
        float breathIntensity   = 0.0f;
    };

    // Pre-analysis metrics for adaptive thresholds
    struct PreAnalysisMetrics {
        float sibilanceThreshold = 0.1f;
        float plosiveThreshold = 0.1f;
        float breathThreshold = 0.1f;
        bool isValid = false;
    };

    void performPreAnalysis(const juce::AudioBuffer<float>& buffer);
    float detectSibilance(const juce::AudioBuffer<float>& buffer);
    float detectPlosive(const juce::AudioBuffer<float>& buffer);
    float detectBreath(const juce::AudioBuffer<float>& buffer);

    // DSP processors (order matches processing chain: click → plosive → breath → esser)
    vxsuite::speech_clarity::DeClickDsp   deClickDsp;
    vxsuite::speech_clarity::DePolosiveDsp dePlosiveDsp;
    vxsuite::speech_clarity::DeBreathDsp  deBreathDsp;
    vxsuite::speech_clarity::DeEsserDsp   deEsserDsp;

    // Detection filter states
    vxsuite::detectors::EnvelopeFollower sibilanceEnvFollower;
    vxsuite::detectors::EnvelopeFollower plosiveEnvFollower;
    vxsuite::detectors::EnvelopeFollower breathEnvFollower;

    vxsuite::detectors::BiquadFilter sibilanceBandFilter;
    vxsuite::detectors::BiquadFilter plosiveBandFilter;
    vxsuite::detectors::BiquadFilter breathBandFilter;

    vxsuite::detectors::OnsetDetector onsetDetector;

    // State
    DetectionState detectionState;
    PreAnalysisMetrics preAnalysisMetrics;
    bool needsPreAnalysis = true;
    vxsuite::Mode lastMode = vxsuite::Mode::vocal;

    // Latency changes triggered by mode switches on the audio thread are
    // reported to the host from the message thread (JUCE's latency
    // notification can allocate and call back into the host).
    void handleAsyncUpdate() override;
    std::atomic<int> pendingLatencySamples { -1 };

    double currentSampleRateHz = 48000.0;
};
