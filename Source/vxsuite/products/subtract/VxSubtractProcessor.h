#pragma once

#include "../../framework/VxSuiteEditorBase.h"
#include "../../framework/VxSuiteProcessorBase.h"
#include "../../../dsp/HandmadePrimary.h"

#include <atomic>
#include <vector>

class VXSubtractAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXSubtractAudioProcessor();
    ~VXSubtractAudioProcessor() override = default;

    const juce::String getName() const override;
    juce::String getStatusText() const override;
    float getLearnProgress() const noexcept override { return learnProgress.load(std::memory_order_relaxed); }
    float getLearnConfidence() const noexcept override { return learnConfidence.load(std::memory_order_relaxed); }
    float getLearnObservedSeconds() const noexcept override { return learnObservedSeconds.load(std::memory_order_relaxed); }
    bool isLearnActive() const noexcept override { return learnActive.load(std::memory_order_relaxed); }
    bool isLearnReady() const noexcept override { return learnReady.load(std::memory_order_relaxed); }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

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
    void fillAlignedDryScratch(const juce::AudioBuffer<float>& dryBuffer, int numSamples);

    vxcleaner::dsp::HandmadePrimary subtractDsp;
    juce::AudioBuffer<float> dryScratch;
    juce::AudioBuffer<float> alignedDryScratch;
    std::vector<std::vector<float>> dryDelayLines;
    std::vector<int> dryDelayWritePos;

    double currentSampleRateHz = 48000.0;
    float smoothedSubtract = 0.0f;
    float smoothedProtect = 0.5f;
    bool controlsPrimed = false;
    bool learnToggleLatched = false;
    float learnSilentSeconds = 0.0f;
    std::vector<float> savedLearnProfile;
    float savedLearnConfidence = 0.0f;
    std::atomic<float> learnProgress { 0.0f };
    std::atomic<float> learnConfidence { 0.0f };
    std::atomic<float> learnObservedSeconds { 0.0f };
    std::atomic<bool> learnActive { false };
    std::atomic<bool> learnReady { false };
};
