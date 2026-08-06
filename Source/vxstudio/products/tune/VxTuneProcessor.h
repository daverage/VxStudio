#pragma once

#include "../../framework/VxStudioProcessorBase.h"
#include "dsp/VxTuneCorrectionEngine.h"
#include "dsp/VxTuneDecomposition.h"
#include "dsp/VxTuneHarmonicContext.h"
#include "dsp/VxTuneKeyProfiles.h"
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

    struct AutoKeyStateForTests {
        int root = 0;
        int scale = 2;
        float confidence = 0.0f;
        int frames = 0;
    };
    AutoKeyStateForTests autoKeyStateForTests() const noexcept {
        return {
            lastAutoKeyRoot.load(std::memory_order_relaxed),
            lastAutoKeyScale.load(std::memory_order_relaxed),
            lastAutoKeyConfidence.load(std::memory_order_relaxed),
            lastAutoKeyFrames.load(std::memory_order_relaxed),
        };
    }

    // True if the best-correlated mode for a user-pinned root is major.
    bool bestModeForFixedRootForTests(const int root) noexcept { return bestModeForFixedRoot(root); }
    // Best-correlated root for a user-pinned mode.
    int bestRootForFixedModeForTests(const bool isMajor) noexcept { return bestRootForFixedMode(isMajor); }
    std::uint16_t appliedScaleMaskForTests() const noexcept { return appliedScaleMask; }
    bool hasSidechainActive() const noexcept override { return sidechainActive; }

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
    void resetAutoKeyState();
    void updateAutoKeyEstimate(const vxsuite::tune::PitchFrame& frame,
                               const vxsuite::tune::NoteSegment& segment) noexcept;
    // Feeds the sidechain's chroma into the SAME histogram/profile-match
    // pipeline updateAutoKeyEstimate() uses - a richer, full-chord evidence
    // source for the song's key, not a separate correction mechanism. See
    // VxTuneHarmonicContext.h for why this reuses the existing auto-key
    // machinery rather than adding a new fusion path into the estimator.
    void updateAutoKeyFromSidechain() noexcept;
    // Shared tail of updateAutoKeyEstimate()/updateAutoKeyFromSidechain():
    // profile-correlates the (now-updated) histogram and applies the
    // held-candidate hysteresis. Both callers only differ in how they add
    // evidence to autoKeyHistogram beforehand.
    void finalizeAutoKeyEstimate() noexcept;
    std::uint16_t currentScaleMask(int keyChoice, int scaleChoice) noexcept;
    // Best-correlated mode (major=true) for a user-pinned root, and
    // best-correlated root for a user-pinned mode. The free joint pick
    // (autoDetectedRoot, autoDetectedScale) is only ever validated as a
    // pair for the histogram's own preferred root - reusing one half of it
    // against a root/mode the user chose independently pairs data the
    // algorithm never actually evaluated together.
    bool bestModeForFixedRoot(int root) noexcept;
    int bestRootForFixedMode(bool isMajor) noexcept;
    juce::String autoKeyStatusText() const;

    vxsuite::tune::PitchDetector detector;
    vxsuite::tune::PerformanceDecomposition decomposition;
    vxsuite::tune::CorrectionEngine correctionEngine;
    std::unique_ptr<vxsuite::tune::VxTunePitchRenderer> pitchRenderer;
    vxsuite::tune::HarmonicContextAnalyzer harmonicContext;

    std::vector<float> monoScratch;
    std::vector<float> sidechainMonoScratch;
    bool sidechainActive = false;
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
    // Persistent major/minor mode decision with hysteresis (see
    // updateAutoKeyEstimate): the winning 7-note collection is stable, but
    // the major-vs-relative-minor tie-break sits near 50/50 for long
    // stretches and flips on noise without a hysteresis band.
    bool autoKeyIsMajor = true;
    // Sample-rate-independent, derived in prepareSuite() from the actual
    // analysis frame rate (hop size is fixed in samples, so frame rate -
    // and therefore a fixed per-frame decay/count - scales with sample
    // rate otherwise).
    float autoKeyDecayPerFrame = 0.9995f;
    int autoKeyMinFrames = 96;
    // Hysteresis state for the constrained sub-problems in
    // bestModeForFixedRoot()/bestRootForFixedMode() (only one root or one
    // mode is free, the other is pinned by the user) - kept separate from
    // the free joint pick (autoDetectedRoot/autoKeyIsMajor above) since a
    // pinned choice can legitimately want a different mode/root than the
    // unconstrained estimate would pick on its own.
    int autoModeForFixedRootHeldRoot = -1;
    bool autoModeForFixedRootIsMajor = true;
    bool autoRootForFixedModeHeldIsMajor = true;
    int autoRootForFixedMode = 0;
    // The mask actually fed to CorrectionEngine. Deliberately only updated
    // between notes (silence/octave-suspect frames), not every frame:
    // TargetEstimator holds its target as accumulated evidence, and
    // narrowing/widening the valid note set mid-sustain would make the
    // currently-held target stop being reinforced while an alternative
    // starts accumulating - a spurious retarget driven by the key updating
    // rather than by the singer's pitch actually moving.
    std::uint16_t appliedScaleMask = vxsuite::tune::CorrectionEngine::kChromaticMask;

    // Published to the message thread (status text) without locks.
    std::atomic<float> lastF0Hz { 0.0f };
    std::atomic<float> lastConfidence { 0.0f };
    std::atomic<float> lastCentreCents { 0.0f };
    std::atomic<float> lastResidualCents { 0.0f };
    std::atomic<int>   lastReason { 0 };
    std::atomic<float> lastCorrectionCents { 0.0f };
    std::atomic<int> lastAutoKeyRoot { 0 };
    std::atomic<int> lastAutoKeyScale { 2 };
    std::atomic<float> lastAutoKeyConfidence { 0.0f };
    std::atomic<int> lastAutoKeyFrames { 0 };
};
