#pragma once

#include "../../framework/VxStudioProcessorBase.h"
#include "../../framework/VxStudioDspCommon.h"
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
    float plosiveDetectionIntensity   = 0.0f;
    float breathDetectionIntensity    = 0.0f;

    // Running peak hold for breath detection (slow-decay level reference)
    float smoothedPeak = 0.01f;

    // HPF and hi-shelf biquad state (per-channel, sized in prepareSuite)
    float hpfB0 = 1.f, hpfB1 = 0.f, hpfB2 = 0.f, hpfA1 = 0.f, hpfA2 = 0.f;
    std::vector<float> hpfZ1, hpfZ2;
    vxsuite::corrective::detail::BiquadCoeffs hiShelfCoeffs {};
    std::vector<float> hiShelfZ1, hiShelfZ2;

    // DSP Components
    vxsuite::speech_clarity::DeEsserDsp    deEsserDsp;
    vxsuite::speech_clarity::DePolosiveDsp dePolosiveDsp;
    vxsuite::speech_clarity::DeBreathDsp   deBreathDsp;

    void detectAndUpdateIntensities(const juce::AudioBuffer<float>& buffer);

    double currentSampleRateHz = 48000.0;
};
