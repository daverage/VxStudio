#pragma once

#include "../../framework/VxSuiteBlockSmoothing.h"
#include "../../framework/VxSuiteEditorBase.h"
#include "../../framework/VxSuiteProcessorBase.h"
#include "dsp/VxDeverbSpectralProcessor.h"

#include <vector>

class VXDeverbAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXDeverbAudioProcessor();
    ~VXDeverbAudioProcessor() override = default;

    juce::String getStatusText() const override;
    void setDebugRt60PresetSeconds(float rt60Seconds);
    void clearDebugRt60Preset();
    void setDebugDeterministicReset(bool shouldUseDefaultRt60);
    float getDebugTrackedRt60Seconds(int channel) const noexcept;
    void setDebugOverSubtract(float overSubtract);
    float getDebugOverSubtract() const noexcept;
    void setDebugNoCepstral(bool shouldBypass);
    bool isDebugNoCepstral() const noexcept;
    void setVoiceMode(bool enabled) noexcept;
    bool isVoiceMode() const noexcept;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();

    void ensureScratchCapacity(int channels, int samples);
    void applyBodyRestore(const juce::AudioBuffer<float>& dryBuffer,
                          juce::AudioBuffer<float>& wetBuffer,
                          float bodyAmount,
                          bool isFirstBlock);
    void applyLoudnessCompensation(juce::AudioBuffer<float>& wetBuffer,
                                   float dryRms,
                                   float reduceAmount,
                                   bool isFirstBlock) noexcept;

    vxsuite::deverb::SpectralProcessor deverbProcessor;
    juce::AudioBuffer<float> wetScratch;
    std::vector<float> dryLowpassState;
    std::vector<float> wetLowpassState;
    std::vector<float> bodySpeechState;
    float smoothedReduce = 0.45f;
    float smoothedBody   = 0.60f;
    float smoothedCompensationGain = 1.0f;
    double currentSampleRateHz = 48000.0;
    int    preparedBlockSize   = 0;
    bool   controlsPrimed      = false;
};
