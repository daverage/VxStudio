#pragma once

#include "../../framework/VxSuiteBlockSmoothing.h"
#include "../../framework/VxSuiteEditorBase.h"
#include "../../framework/VxSuiteProcessorBase.h"
#include "../../framework/VxSuiteStageChain.h"
#include "dsp/VxSubtractDsp.h"

#include <atomic>
#include <array>
#include <vector>

class VXSubtractAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXSubtractAudioProcessor();
    ~VXSubtractAudioProcessor() override = default;

    juce::String getStatusText() const override;
    float getLearnProgress() const noexcept override { return learnProgress.load(std::memory_order_relaxed); }
    float getLearnConfidence() const noexcept override { return learnConfidence.load(std::memory_order_relaxed); }
    float getLearnObservedSeconds() const noexcept override { return learnObservedSeconds.load(std::memory_order_relaxed); }
    bool isLearnActive() const noexcept override { return learnActive.load(std::memory_order_relaxed); }
    bool isLearnReady() const noexcept override { return learnReady.load(std::memory_order_relaxed); }

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();
    void updateLearnTelemetry(const int numChannels);
    void applySavedProfiles();

    vxsuite::subtract::SubtractDsp subtractDspMono;
    vxsuite::subtract::SubtractDsp subtractDspLeft;
    vxsuite::subtract::SubtractDsp subtractDspRight;

    double currentSampleRateHz = 48000.0;
    float smoothedSubtract = 0.0f;
    float smoothedProtect = 0.5f;
    float smoothedMakeupGain = 1.0f;
    bool controlsPrimed = false;
    bool learnToggleLatched = false;
    juce::AudioBuffer<float> leftScratch;
    juce::AudioBuffer<float> rightScratch;
    std::vector<float> savedLearnProfile;
    std::array<std::vector<float>, 2> savedStereoLearnProfiles;
    float savedLearnConfidence = 0.0f;
    std::array<float, 2> savedStereoLearnConfidence { 0.0f, 0.0f };
    double savedLearnProfileSampleRate = 0.0;
    int savedLearnProfileFftSize = 0;
    int savedLearnProfileHopSize = 0;
    std::atomic<float> learnProgress { 0.0f };
    std::atomic<float> learnConfidence { 0.0f };
    std::atomic<float> learnObservedSeconds { 0.0f };
    std::atomic<bool> learnActive { false };
    std::atomic<bool> learnReady { false };
};
