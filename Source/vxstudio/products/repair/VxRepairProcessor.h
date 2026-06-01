#pragma once

#include "../../framework/VxStudioProcessorBase.h"
#include "../speech_clarity/dsp/VxDeEsserDsp.h"
#include "../speech_clarity/dsp/VxDePolosiveDsp.h"  // class is DePolosiveDsp (note spelling)
#include "../speech_clarity/dsp/VxDeBreathDsp.h"
#include "../denoiser/dsp/VxDenoiserDsp.h"
#include "../deverb/dsp/VxDeverbSpectralProcessor.h"
#include "VxRepairAnalysis.h"

#include <atomic>

class VXRepairAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXRepairAudioProcessor();
    ~VXRepairAudioProcessor() override = default;

    juce::String getStatusText() const override;
    juce::AudioProcessorEditor* createEditor() override;

    // UI thread API
    void triggerAnalysis();
    void resetAnalysis();

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    float getAnalysisProgress() const noexcept { return analyser.getProgress(); }
    bool  isAnalysisComplete()  const noexcept { return analyser.isComplete(); }
    bool  isAnalysisRunning()   const noexcept { return analyser.isCollecting(); }
    vxsuite::repair::RepairAssessment getAssessment() const noexcept { return analyser.getAssessment(); }
    void applyAssessmentToParams();

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParams();

    vxsuite::speech_clarity::DeEsserDsp   deEsserDsp;
    vxsuite::speech_clarity::DePolosiveDsp dePlosiveDsp;
    vxsuite::speech_clarity::DeBreathDsp  deBreathDsp;
    vxsuite::denoiser::DenoiserDsp        denoiserDsp;
    vxsuite::deverb::SpectralProcessor    deverbDsp;
    vxsuite::repair::RepairAnalyser  analyser;

    // Per-tool dry delay buffers for latency-compensated listen (dry − wet).
    // Sized to each DSP's algorithmic latency after prepareSuite().
    juce::AudioBuffer<float> noiseDryDelay;
    juce::AudioBuffer<float> reverbDryDelay;
    int noiseDryDelayPos  = 0;
    int reverbDryDelayPos = 0;

    double currentSampleRate = 48000.0;
    int    currentBlockSize  = 0;
    std::atomic<bool> triggerPending { false };
};
