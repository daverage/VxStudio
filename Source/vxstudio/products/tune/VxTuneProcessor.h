#pragma once

#include "../../framework/VxStudioProcessorBase.h"
#include "dsp/VxTuneCorrectionEngine.h"
#include "dsp/VxTuneDecomposition.h"
#include "dsp/VxTunePitchDetector.h"
#include "dsp/VxTunePsolaShifter.h"
#include "dsp/VxTuneTimeline.h"

#include <array>
#include <atomic>
#include <vector>

// VX Tune — intelligent vocal pitch correction.
// Phase 1 milestone 1: ANALYSIS ONLY. Audio passes through untouched while
// the pitch detector + performance decomposition run on a mono analysis mix;
// the status line reports what the engine hears. No shifting, no latency.
// Reference: docs/Task Based/VXTUNE_VISION_ARCHITECTURE.md (v2).
//
// Parameter contract (stable IDs): "amount", "natural" exist from v1 so the
// state schema never breaks; key/scale parameters arrive with target
// estimation in a later milestone (additive, therefore safe).
class VXTuneAudioProcessor final : public vxsuite::ProcessorBase {
public:
    VXTuneAudioProcessor();
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

    vxsuite::tune::PitchDetector detector;
    vxsuite::tune::PerformanceDecomposition decomposition;
    vxsuite::tune::CorrectionEngine correctionEngine;
    vxsuite::tune::PsolaShifter shifter;   // one instance, shared grain schedule -> stable stereo image

    std::vector<float> monoScratch;
    std::array<vxsuite::tune::PitchObservation, 64> observationScratch {};

    // Published to the message thread (status text) without locks.
    std::atomic<float> lastF0Hz { 0.0f };
    std::atomic<float> lastConfidence { 0.0f };
    std::atomic<float> lastCentreCents { 0.0f };
    std::atomic<float> lastResidualCents { 0.0f };
    std::atomic<int>   lastReason { 0 };
    std::atomic<float> lastCorrectionCents { 0.0f };
};
