#pragma once

#include "../../framework/VxSuiteEditorBase.h"
#include "../../framework/VxSuiteProcessorBase.h"
#include "dsp/VxDeverbSpectralProcessor.h"

#include <vector>

class VXDeverbAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXDeverbAudioProcessor();
    ~VXDeverbAudioProcessor() override = default;

    const juce::String getName() const override;
    juce::String getStatusText() const override;
    juce::AudioProcessorEditor* createEditor() override;
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
    void renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                            const juce::AudioBuffer<float>& inputBuffer) override;

private:
    static vxsuite::ProductIdentity makeIdentity();
    static juce::AudioProcessorValueTreeState::ParameterLayout
           makeLayout(const vxsuite::ProductIdentity& identity);

    void ensureScratchCapacity(int channels, int samples);
    void ensureDelayCapacity(int channels, int samples);
    void fillAlignedDryScratch(const juce::AudioBuffer<float>& dryBuffer, int numSamples);
    void applyBodyRestore(const juce::AudioBuffer<float>& dryBuffer,
                          juce::AudioBuffer<float>& wetBuffer,
                          float bodyAmount,
                          bool isFirstBlock);

    vxsuite::deverb::SpectralProcessor deverbProcessor;
    juce::AudioBuffer<float> dryScratch;
    juce::AudioBuffer<float> alignedDryScratch;
    juce::AudioBuffer<float> wetScratch;
    std::vector<std::vector<float>> dryDelayLines;
    std::vector<int> dryDelayWritePos;
    std::vector<float> dryLowpassState;
    std::vector<float> wetLowpassState;
    float smoothedReduce = 0.45f;
    float smoothedBody   = 0.60f;
    double currentSampleRateHz = 48000.0;
    int    preparedBlockSize   = 0;
    bool   controlsPrimed      = false;
};
