#pragma once

#include "../../framework/VxStudioBlockSmoothing.h"
#include "../../framework/VxStudioBlockSmoothedControl.h"
#include "../../framework/VxStudioEditorBase.h"
#include "../../framework/VxStudioProcessorBase.h"
#include "dsp/VxDeverbSpectralProcessor.h"

#include <vector>

class VXDeverbAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXDeverbAudioProcessor();
    ~VXDeverbAudioProcessor() override = default;

    juce::String getStatusText() const override;
    // Test/analysis control API — used by regression tests and development tools
    void setTestRt60PresetSeconds(float rt60Seconds);
    void clearTestRt60Preset();
    void setTestDeterministicReset(bool shouldUseDefaultRt60);
    float getTestTrackedRt60Seconds(int channel) const noexcept;
    void setTestOverSubtract(float overSubtract);
    float getTestOverSubtract() const noexcept;
    void setTestNoCepstral(bool shouldBypass);
    bool isTestNoCepstral() const noexcept;
    void setVoiceMode(bool enabled) noexcept;
    bool isVoiceMode() const noexcept;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();

    void ensureScratchCapacity(int channels, int samples);
    void applyLoudnessCompensation(juce::AudioBuffer<float>& wetBuffer,
                                   float dryRms,
                                   float reduceAmount,
                                   bool isFirstBlock,
                                   float wetRms,
                                   float wetPeak) noexcept;

    vxsuite::deverb::SpectralProcessor deverbProcessor;
    juce::AudioBuffer<float> wetScratch;
    vxsuite::BlockSmoothedControl controls;
    float smoothedCompensationGain = 1.0f;
    float smoothedBody = 0.0f;
    // Per-channel DF2 biquad state for the body low-shelf (max 2 channels)
    std::array<float, 2> bodyShelfZ1 {};
    std::array<float, 2> bodyShelfZ2 {};
    double currentSampleRateHz = 48000.0;
    int    preparedBlockSize   = 0;
    bool   firstBlockProcessed = false;
};
