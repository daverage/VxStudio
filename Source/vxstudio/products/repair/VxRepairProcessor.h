#pragma once

#include "../../framework/VxStudioProcessorBase.h"
#include "../speech_clarity/dsp/VxDeClickDsp.h"
#include "../speech_clarity/dsp/VxDeEsserDsp.h"
#include "../speech_clarity/dsp/VxDePolosiveDsp.h"  // class is DePolosiveDsp (note spelling)
#include "../speech_clarity/dsp/VxDeBreathDsp.h"
#include "../denoiser/dsp/VxDenoiserDsp.h"
#include "../deverb/dsp/VxDeverbSpectralProcessor.h"
#include "../deepfilternet/dsp/VxDeepFilterNetService.h"
#include "VxRepairAnalysis.h"

#include <atomic>

class VXRepairAudioProcessor final : public vxsuite::ProcessorBase,
                                     private juce::Timer {
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

    enum class AnalysisPhase { Idle, Phase1, Phase2, Done };

    float getAnalysisProgress() const noexcept;
    bool  isAnalysisComplete()  const noexcept { return getAnalysisPhase() == AnalysisPhase::Done; }
    bool  isAnalysisRunning()   const noexcept;
    AnalysisPhase getAnalysisPhase() const noexcept {
        return static_cast<AnalysisPhase>(analysisPhase.load(std::memory_order_relaxed));
    }
    vxsuite::repair::RepairAssessment getAssessment() const noexcept { return analyser.getAssessment(); }
    void applyAssessmentToParams();

    // DeepFilter status for UI
    bool isDeepFilterReady() const noexcept { return dfService.isRealtimeReady(); }
    bool isDeepFilterPrepared() const noexcept { return dfService.hasRealtimeBackend(); }

    // Live display data  -  safe to poll from the UI thread.
    void getDisplayBands(std::array<float, vxsuite::repair::RepairAnalyser::kDisplayBands>& out) const noexcept {
        analyser.getDisplayBands(out);
    }
    struct ToolActivity { float noise = 0.f, clarity = 0.f, click = 0.f, reverb = 0.f; };
    ToolActivity getToolActivity() const noexcept {
        return { noiseActivity.load(std::memory_order_relaxed),
                 clarityActivity.load(std::memory_order_relaxed),
                 clickActivity.load(std::memory_order_relaxed),
                 reverbActivity.load(std::memory_order_relaxed) };
    }

    vxsuite::MeteringSnapshot getMeteringSnapshot() const noexcept override;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParams();

    // Message-thread tick: runs the analyser's deferred finalise pass so the
    // heavy scoring work never lands on the audio thread.
    void timerCallback() override;

    vxsuite::speech_clarity::DeClickDsp   deClickDsp;
    vxsuite::speech_clarity::DeEsserDsp   deEsserDsp;
    vxsuite::speech_clarity::DePolosiveDsp dePlosiveDsp;
    vxsuite::speech_clarity::DeBreathDsp  deBreathDsp;
    vxsuite::denoiser::DenoiserDsp        denoiserDsp;
    vxsuite::denoiser::DenoiserDsp        analyserDenoiserDsp;  // used only for Phase 2 analysis
    vxsuite::deverb::SpectralProcessor    deverbDsp;
    vxsuite::deepfilternet::DeepFilterService dfService;
    vxsuite::repair::RepairAnalyser  analyser;

    // Per-tool dry delay buffers for latency-compensated listen (dry − wet).
    // Sized to each DSP's algorithmic latency after prepareSuite().
    juce::AudioBuffer<float> noiseDryDelay;
    juce::AudioBuffer<float> reverbDryDelay;
    int noiseDryDelayPos  = 0;
    int reverbDryDelayPos = 0;

    // Scratch buffers used to advance idle STFT stage state during listen mode
    // without disturbing the main audio buffer.  Sized to max block in prepareSuite().
    juce::AudioBuffer<float> stftStateScratch;
    juce::AudioBuffer<float> stftStateScratch2;

    // Latency-correct pass-through delay lines for when STFT stages are off.
    // Ring buffers sized to each stage's latency; advanced on every block so the
    // delayed output is always available as a true pass-through with correct timing.
    juce::AudioBuffer<float> noisePassthroughDelay;
    int                      noisePassthroughPos = 0;
    juce::AudioBuffer<float> reverbPassthroughDelay;
    int                      reverbPassthroughPos = 0;

    double currentSampleRate = 48000.0;
    int    currentBlockSize  = 0;
    std::atomic<bool> triggerPending { false };

    // Two-phase analysis
    std::atomic<int> analysisPhase { 0 };  // AnalysisPhase enum stored as int

    // Preallocated copy buffer for the Phase 2 denoised analysis feed.
    juce::AudioBuffer<float> phase2AnalysisBuf;

    // Speech clarity real-time artifact detection
    void detectClarityIntensities(const juce::AudioBuffer<float>&, int numSamples) noexcept;
    float smoothedPeak = 0.01f;                         // adaptive level tracker
    std::atomic<float> sibilanceIntensity { 0.0f };
    std::atomic<float> plosiveIntensity   { 0.0f };
    std::atomic<float> breathIntensity    { 0.0f };
    std::atomic<float> clickIntensity     { 0.0f };

    // Deverb loudness compensation
    float smoothedReverbMakeup = 1.0f;

    // Click dry-delay for latency-compensated listen (click has lookahead latency)
    juce::AudioBuffer<float> clickDryDelay;
    int                      clickDryDelayPos = 0;

    // Per-tool processing activity for metering (0=idle, 1=full GR)
    std::atomic<float> noiseActivity   { 0.0f };
    std::atomic<float> clarityActivity { 0.0f };
    std::atomic<float> clickActivity   { 0.0f };
    std::atomic<float> reverbActivity  { 0.0f };

    // Stereo I/O peak metering
    std::atomic<float> inputPeakL  { 0.0f };
    std::atomic<float> inputPeakR  { 0.0f };
    std::atomic<float> outputPeakL { 0.0f };
    std::atomic<float> outputPeakR { 0.0f };
};
