#pragma once

#include "../../framework/VxStudioProcessorBase.h"
#include "dsp/VxTuneCorrectionEngine.h"
#include "dsp/VxTuneDecomposition.h"
#include "dsp/VxTunePitchDetector.h"
#include "dsp/VxTunePitchRenderer.h"
#include "dsp/VxTuneTimeline.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// VX Tune — intelligent vocal pitch correction.
// Phase 1 milestone 1: ANALYSIS ONLY. Audio passes through untouched while
// the pitch detector + performance decomposition run on a mono analysis mix;
// the status line reports what the engine hears. No shifting, no latency.
// Reference: docs/Task Based/VXTUNE_VISION_ARCHITECTURE.md (v2).
//
class VXTuneAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXTuneAudioProcessor();
    ~VXTuneAudioProcessor() override;
    juce::String getStatusText() const override;

    float getPitchTraceDetectedCents() const noexcept override {
        return lastCentreCents.load(std::memory_order_relaxed)
             + lastResidualCents.load(std::memory_order_relaxed);
    }
    float getPitchTraceCorrectedCents() const noexcept override {
        return getPitchTraceDetectedCents()
             + lastCorrectionCents.load(std::memory_order_relaxed);
    }
    float getPitchTraceConfidence() const noexcept override {
        return lastF0Hz.load(std::memory_order_relaxed) > 0.0f
             ? lastConfidence.load(std::memory_order_relaxed) : 0.0f;
    }

    // Latest analysis frame, for tests and the future dev overlay.
    vxsuite::tune::PitchFrame latestFrameForTests() const noexcept;

protected:
    void prepareSuite(double sampleRate, int samplesPerBlock) override;
    void resetSuite() override;
    void processProduct(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

private:
    static vxsuite::ProductIdentity makeIdentity();
    void configureDebugTrace(double sampleRate);
    void appendDebugTraceFrame(const vxsuite::tune::PitchFrame& frame,
                               float amount, float natural, float speed, float focus,
                               std::uint16_t scaleMask,
                               float correction);
    void flushDebugTrace();
    void updateAutoKeyEstimate(const vxsuite::tune::PitchFrame& frame) noexcept;
    std::uint16_t currentScaleMask(int keyChoice, int scaleChoice) const noexcept;

    vxsuite::tune::PitchDetector detector;
    vxsuite::tune::PerformanceDecomposition decomposition;
    vxsuite::tune::CorrectionEngine correctionEngine;
    std::unique_ptr<vxsuite::tune::VxTunePitchRenderer> pitchRenderer;

    std::vector<float> monoScratch;
    std::array<vxsuite::tune::PitchObservation, 64> observationScratch {};

    struct DebugTraceRow {
        std::int64_t sample = 0;
        double seconds = 0.0;
        float f0Hz = 0.0f;
        float confidence = 0.0f;
        int reason = 0;
        float centreCents = 0.0f;
        float residualCents = 0.0f;
        float decompConfidence = 0.0f;
        float correctionCents = 0.0f;
        float correctedCents = 0.0f;
        float musicalAuthority = 0.0f;
        float authorityConfidence = 0.0f;
        float authorityPassing = 0.0f;
        float authorityOnset = 0.0f;
        float authorityTransition = 0.0f;
        float authorityTarget = 0.0f;
        float authorityStableBoost = 0.0f;
        float authorityNearCorrect = 1.0f;
        int targetMidiNote = -1;
        int runnerUpMidiNote = -1;
        float targetErrorCents = 0.0f;
        float targetMarginLog = 0.0f;
        float shifterTargetCents = 0.0f;
        float shifterSmoothedCents = 0.0f;
        float shifterMix = 0.0f;
        float shifterHintPeriod = 0.0f;
        float shifterHintConfidence = 0.0f;
        int shifterVoicedActive = 0;
        int shifterParkedNow = 0;
        int shifterParkedRun = 0;
        int shifterEpochCount = 0;
        const char* rendererBackend = "";
        const char* rendererProfile = "";
        int rendererLatencySamples = 0;
        int rendererBlockSamples = 0;
        int rendererIntervalSamples = 0;
        int rendererSplitComputation = 0;
        float amount = 0.0f;
        float natural = 0.0f;
        float speed = 0.0f;
        float focus = 0.0f;
        std::uint16_t scaleMask = 0;
        int autoKeyRoot = 0;
        int autoKeyScale = 0;
        float autoKeyConfidence = 0.0f;
        int autoKeyFrames = 0;
        float behaviour[static_cast<int>(vxsuite::tune::Behaviour::count)] {};
    };
    std::vector<DebugTraceRow> debugTraceRows;
    std::string debugTracePath;
    double debugTraceSampleRate = 48000.0;
    bool debugTraceEnabled = false;

    float renderTargetCents = 0.0f;
    float renderProcessCents = 0.0f;
    int renderGateMisses = 0;
    std::array<float, 12> autoKeyHistogram {};
    int autoKeyFrameCount = 0;
    int autoDetectedRoot = 0;
    int autoDetectedScale = 2;
    float autoKeyConfidence = 0.0f;

    // Published to the message thread (status text) without locks.
    std::atomic<float> lastF0Hz { 0.0f };
    std::atomic<float> lastConfidence { 0.0f };
    std::atomic<float> lastCentreCents { 0.0f };
    std::atomic<float> lastResidualCents { 0.0f };
    std::atomic<int>   lastReason { 0 };
    std::atomic<float> lastCorrectionCents { 0.0f };
};
