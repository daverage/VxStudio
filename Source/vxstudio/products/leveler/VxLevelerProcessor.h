#pragma once

#include "../../framework/VxStudioBlockSmoothedControl.h"
#include "../../framework/VxStudioProcessorBase.h"
#include "dsp/VxLevelerDetector.h"
#include "dsp/VxLevelerDsp.h"
#include "dsp/VxLevelerOfflineAnalyzer.h"

class VXLevelerAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXLevelerAudioProcessor();
    ~VXLevelerAudioProcessor() override = default;

    juce::String getStatusText() const override;
    int getActivityLightCount() const noexcept override;
    float getActivityLight(int index) const noexcept override;
    std::string_view getActivityLightLabel(int index) const noexcept override;
    float getLearnProgress() const noexcept override { return analysisProgress; }
    float getLearnConfidence() const noexcept override { return analysisConfidence; }
    float getLearnObservedSeconds() const noexcept override { return analysisObservedSeconds; }
    bool isLearnActive() const noexcept override { return analysisActive; }
    bool isLearnReady() const noexcept override { return analysisReady; }
    bool shouldShowLearnUi() const noexcept override;
    void setDebugTuning(const vxsuite::leveler::Dsp::Tuning& tuning) noexcept;
    void setOfflineAnalysis(vxsuite::leveler::OfflineAnalysisResult analysis);
    void clearOfflineAnalysis() noexcept;
    [[nodiscard]] vxsuite::leveler::Dsp::DebugSnapshot getDebugSnapshot() const noexcept;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    void prepareAnalysisCapture(int maxBlockSize);
    void resetAnalysisCapture(bool keepOfflineMap = true) noexcept;
    void startAnalysisCapture();
    void stopAnalysisCapture();
    void captureAnalysisAudio(const juce::AudioBuffer<float>& buffer) noexcept;
    static vxsuite::ProductIdentity makeIdentity();

    vxsuite::leveler::Detector detector;
    vxsuite::leveler::Dsp dsp;
    double currentSampleRateHz = 48000.0;
    int preparedBlockSize = 256;
    vxsuite::BlockSmoothedControlPair controls;
    bool analyzeToggleLatched = false;
    bool analysisActive = false;
    bool analysisReady = false;
    float analysisProgress = 0.0f;
    float analysisConfidence = 0.0f;
    float analysisObservedSeconds = 0.0f;
    std::vector<float> analysisBlockDb;
    int analysisCapturedBlocks = 0;
    int analysisMaxBlocks = 0;
    int analysisFrameCursor = 0;
    double analysisEnergy = 0.0;
};
