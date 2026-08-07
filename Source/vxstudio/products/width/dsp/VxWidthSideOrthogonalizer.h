#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include "VxWidthAdtVoice.h"

#include <array>
#include <cmath>
#include <vector>

// Sparse adaptive Mid->generatedSide orthogonaliser (2026-08-07 imbalance
// investigation, phase 2 prototype). ContentPredictabilityRestraint (see
// VxWidthLoudnessCompensator.h) can only turn the WHOLE generated layer down
// when it reads too correlated with Mid - a blunt instrument that throws
// away useful spatial information along with the problematic part. This
// predicts and removes ONLY the Mid-derived component of the generated Side,
// sample by sample, via a small NLMS adaptive filter over a handful of
// representative lags (not a dense filter spanning the ADT engine's full
// 45ms delay range - that would need ~2000+ taps at high sample rates, far
// too expensive; a sparse predictor at the lags where correlated energy
// actually concentrates is the same principle the lag-aware restraint
// already uses, just turned into active cancellation instead of a
// scale-down decision).
//
// Runs BEFORE ContentPredictabilityRestraint in the signal chain (see
// VxWidthProcessor.cpp) - the restraint becomes a secondary/emergency
// guardrail for whatever correlation this doesn't catch, not the primary
// mechanism.
namespace vxsuite::width {

class SideOrthogonalizer {
public:
    static constexpr int kNumFixedLags = 8; // 0,1,2,4,8,16,24,32 ms
    static constexpr int kNumDynamicTaps = 2; // tracks current ADT voice A/B delay
    static constexpr int kNumTaps = kNumFixedLags + kNumDynamicTaps;

    void prepare(const double sampleRate) noexcept {
        sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
        // Sized from AdtVoice's own declared max delay (45ms), not a
        // smaller guessed constant - a Loose-band voice at 40-45ms must
        // still be trackable by the dynamic taps below, or they silently
        // clamp to whatever this buffer covers instead of "wherever the
        // ADT voices currently are" as documented.
        const float maxLagMs = AdtVoice::kMaxPossibleDelayMs;
        historySize = static_cast<int>(std::ceil(maxLagMs * 0.001 * sr)) + 2;
        midHistory.assign(static_cast<size_t>(historySize), 0.0f);
        constexpr float lagsMs[kNumFixedLags] = { 0.0f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 24.0f, 32.0f };
        for (size_t k = 0; k < kNumFixedLags; ++k)
            fixedLagSamples[k] = static_cast<int>(std::round(lagsMs[k] * 0.001 * sr));
        reset();
    }

    void reset() noexcept {
        std::fill(midHistory.begin(), midHistory.end(), 0.0f);
        writePos = 0;
        weights.fill(0.0f);
        smoothedMidEnergy = 0.0f;
        smoothedSideEnergy = 0.0f;
        smoothedPowerBefore = 0.0f;
        smoothedPowerAfter = 0.0f;
    }

    void setBypassed(const bool b) noexcept { bypassed = b; }
    [[nodiscard]] bool isBypassed() const noexcept { return bypassed; }
    // Constrains cancellation to the same polarity as the generated sample
    // (only ever REMOVES magnitude, never adds it back with the opposite
    // sign) - default on. Exposed for A/B comparison against unconstrained
    // bipolar cancellation.
    void setSamePolarityOnly(const bool b) noexcept { samePolarityOnly = b; }
    [[nodiscard]] bool isSamePolarityOnly() const noexcept { return samePolarityOnly; }

    // midSample: the same Mid signal the generators (decorrelator/ADT) read.
    // generatedSideSample: RAW generated Side (decorSideRaw*decorBlend +
    // doubleSide*doubleSideAmount), BEFORE ContentPredictabilityRestraint.
    // dynamicDelayMsA/B: current ADT voice A/B slewed delay (ms) - the two
    // dynamic taps track wherever the ADT voices currently are, since that's
    // exactly where ADT's own Mid-correlated energy concentrates.
    // transientRisk: 0..1, slows adaptation during onsets so the filter
    // doesn't chase transient-driven statistics it will immediately need to
    // unlearn.
    float process(const float midSample, const float generatedSideSample,
                  const float dynamicDelayMsA, const float dynamicDelayMsB,
                  const float transientRisk) noexcept {
        if (historySize <= 0)
            return generatedSideSample;

        midHistory[static_cast<size_t>(writePos)] = midSample;

        std::array<float, kNumTaps> x {};
        for (size_t k = 0; k < kNumFixedLags; ++k) {
            int idx = writePos - fixedLagSamples[k];
            if (idx < 0) idx += historySize;
            x[k] = midHistory[static_cast<size_t>(idx)];
        }
        const int dynA = juce::jlimit(0, historySize - 1, static_cast<int>(std::round(dynamicDelayMsA * 0.001 * sr)));
        const int dynB = juce::jlimit(0, historySize - 1, static_cast<int>(std::round(dynamicDelayMsB * 0.001 * sr)));
        int idxA = writePos - dynA; if (idxA < 0) idxA += historySize;
        int idxB = writePos - dynB; if (idxB < 0) idxB += historySize;
        x[kNumFixedLags]     = midHistory[static_cast<size_t>(idxA)];
        x[kNumFixedLags + 1] = midHistory[static_cast<size_t>(idxB)];

        float predicted = 0.0f;
        for (size_t k = 0; k < kNumTaps; ++k)
            predicted += weights[k] * x[k];

        // rawPredictionError: the TRUE model residual, used ONLY to adapt
        // the filter - the predictor must learn the real Mid<->generatedSide
        // relationship, uncorrupted by whatever the output-safety policy
        // below decides is safe to actually apply. Using the CAPPED result
        // as the learning error instead (an earlier version of this code
        // did) means once the predictor wants to cancel more than the cap
        // allows, it sees a residual it can never close and keeps pushing
        // coefficients against their bound indefinitely - unnecessary
        // adaptation activity, more sensitivity to signal changes, slower
        // recovery afterwards. Predictor learns; output policy decides how
        // much of that learning is safe to apply - two different jobs.
        const float rawPredictionError = generatedSideSample - predicted;

        // Output safety policy: cancellation is (a) same-polarity-only by
        // default - only ever REMOVES magnitude, never flips a +1 sample
        // into +1.35 by subtracting a negative prediction (an unconstrained
        // bipolar clamp only bounds |prediction| applied, not the resulting
        // output magnitude - if predicted and generatedSideSample have
        // opposite signs, subtracting a bounded-but-opposite-sign
        // prediction INCREASES |output|, not the "removes at most 35%"
        // semantics this is meant to guarantee); and (b) magnitude-capped at
        // kMaxCancelFraction of this sample's own magnitude (measured
        // necessary, not theoretical: the dynamic taps track each ADT
        // voice's EXACT current delay, so an unconstrained predictor can
        // reconstruct almost the entire ADT signal - ADT's dominant content
        // genuinely IS Mid delayed by that amount, so "perfectly
        // predictable" and "safe to remove" are NOT the same thing here.
        // Without both constraints, measured effect: 26dB energy reduction,
        // ~90% of the generated Side removed).
        float cancellation = predicted;
        if (samePolarityOnly && cancellation * generatedSideSample < 0.0f)
            cancellation = 0.0f;
        const float maxCancellation = kMaxCancelFraction * std::abs(generatedSideSample);
        cancellation = juce::jlimit(-maxCancellation, maxCancellation, cancellation);
        const float orthogonal = generatedSideSample - cancellation;

        if (!bypassed) {
            // Energy gate: only adapt when both Mid and the generated layer
            // carry real signal - adapting on near-silence just chases noise.
            constexpr float kEnergyAlpha = 0.999f; // ~50ms-ish at 48kHz block-rate feel
            smoothedMidEnergy = kEnergyAlpha * smoothedMidEnergy + (1.0f - kEnergyAlpha) * (midSample * midSample);
            smoothedSideEnergy = kEnergyAlpha * smoothedSideEnergy + (1.0f - kEnergyAlpha) * (generatedSideSample * generatedSideSample);
            constexpr float kEnergyFloor = 1.0e-7f;
            if (smoothedMidEnergy > kEnergyFloor && smoothedSideEnergy > kEnergyFloor) {
                // Conservative NLMS: step size shrinks during transients
                // (freeze/slow adaptation, don't chase onset-driven stats).
                const float adaptRate = kBaseAdaptRate * (1.0f - 0.9f * juce::jlimit(0.0f, 1.0f, transientRisk));
                double normSq = kNlmsEpsilon;
                for (size_t k = 0; k < kNumTaps; ++k)
                    normSq += static_cast<double>(x[k]) * x[k];
                const float muEff = static_cast<float>(adaptRate / normSq);
                for (size_t k = 0; k < kNumTaps; ++k) {
                    weights[k] += muEff * rawPredictionError * x[k];
                    weights[k] = juce::jlimit(-kWeightBound, kWeightBound, weights[k]);
                    weights[k] *= (1.0f - kLeak); // slow leak toward zero (bounded drift)
                }
            }
        }

        writePos = (writePos + 1) % historySize;

        // Debug telemetry: smoothed POWER (mean square) before/after, not
        // smoothed dB-of-instantaneous-magnitude (an earlier version did
        // that - averaging dB values isn't equivalent to an energy/RMS
        // reduction, even though it happened to point the right direction).
        constexpr float kTeleAlpha = 0.999f;
        smoothedPowerBefore = kTeleAlpha * smoothedPowerBefore + (1.0f - kTeleAlpha) * (generatedSideSample * generatedSideSample);
        smoothedPowerAfter = kTeleAlpha * smoothedPowerAfter + (1.0f - kTeleAlpha) * (orthogonal * orthogonal);

        return bypassed ? generatedSideSample : orthogonal;
    }

    // How much the orthogonaliser is currently reducing generated-Side
    // ENERGY by (smoothed power ratio, in dB) - a large sustained value
    // here means it's suppressing most of the effect, not just the
    // Mid-correlated part (exactly what the user flagged as the wrong
    // outcome to optimise for).
    [[nodiscard]] float lastEnergyReductionDb() const noexcept {
        constexpr float kEps = 1.0e-12f;
        return 10.0f * std::log10((smoothedPowerBefore + kEps) / (smoothedPowerAfter + kEps));
    }

    [[nodiscard]] float coefficientEnergy() const noexcept {
        float e = 0.0f;
        for (const auto w : weights) e += w * w;
        return e;
    }

private:
    static constexpr float kBaseAdaptRate = 0.02f;
    static constexpr double kNlmsEpsilon = 1.0e-6;
    static constexpr float kWeightBound = 1.5f;
    static constexpr float kLeak = 2.0e-6f;
    // Max fraction of each sample's generated-Side MAGNITUDE the predictor
    // is allowed to remove - see the process() comment for why this is
    // load-bearing, not a minor tuning knob.
    static constexpr float kMaxCancelFraction = 0.35f;

    double sr = 48000.0;
    bool bypassed = false;
    bool samePolarityOnly = true;
    std::vector<float> midHistory;
    int writePos = 0;
    int historySize = 0;
    std::array<int, kNumFixedLags> fixedLagSamples {};
    std::array<float, kNumTaps> weights {};
    float smoothedMidEnergy = 0.0f;
    float smoothedSideEnergy = 0.0f;
    float smoothedPowerBefore = 0.0f;
    float smoothedPowerAfter = 0.0f;
};

} // namespace vxsuite::width
