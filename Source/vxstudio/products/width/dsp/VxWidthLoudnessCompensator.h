#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>

// Loudness/energy management (§12): a gentle, slow makeup-gain corrector,
// not a limiter. Bounded range and a ~250ms time constant per §12's "no
// obvious gain jump when Width changes, no major buildup when Double
// increases" - deliberately not strict RMS/sample-level equality (§12
// explicitly rules that out). Runs as one-block-delayed feedback: this
// block's measured in/out RMS sets the target gain applied from the START
// of the NEXT block, avoiding any lookahead/latency.
namespace vxsuite::width {

class LoudnessCompensator {
public:
    void prepare(const double sampleRate) noexcept {
        const float srSafe = static_cast<float>(sampleRate > 1000.0 ? sampleRate : 48000.0);
        // ~250ms smoothing time constant, expressed per-sample; applied via
        // block-rate update() so effectively per-block but time-correct
        // regardless of host block size.
        blockAlphaPerSample = std::exp(-1.0f / (0.25f * srSafe));
        reset();
    }
    void reset() noexcept { compGain = 1.0f; }

    [[nodiscard]] float currentGain() const noexcept { return compGain; }

    // Call once per block with THIS block's (pre-compensation) input/output
    // RMS; updates the gain that will be applied to the NEXT block.
    void updateForNextBlock(const double inputRms, const double outputRms, const int numSamples) noexcept {
        if (outputRms < 1.0e-6)
            return;
        const float targetGain = juce::jlimit(0.70f, 1.20f,
            static_cast<float>(inputRms / juce::jmax(1.0e-6, outputRms)));
        const float blockAlpha = std::pow(blockAlphaPerSample, static_cast<float>(std::max(1, numSamples)));
        compGain = blockAlpha * compGain + (1.0f - blockAlpha) * targetGain;
    }

private:
    float blockAlphaPerSample = 0.99999f;
    float compGain = 1.0f;
};

// Generated-content predictability restraint: implements §11.3's guardrail
// ("if requested processing causes excessive degradation: reduce new
// decorrelated width first, reduce generated DoubleSide second...") using
// lag-aware correlation-with-Mid as the "how much additional damage"
// measurement (max |correlation| across a handful of lags, computed by the
// caller - see VxWidthProcessor.cpp - not same-sample only; a delayed or
// phase-shifted copy of Mid can be entirely derived from it while reading
// near-zero at lag 0, which same-sample correlation alone would miss).
//
// The Side content Region C/§8 generate is a fixed, deterministic function
// of Mid (filtered/delayed), not independent noise - for real program
// material it can end up predictable from Mid rather than truly
// decorrelated. Since L=Mid+Side, R=Mid-Side, any such correlation biases
// energy toward one output channel for a whole session (reported as "left
// significantly louder than right" widening mono material).
//
// Tried first: actively CANCELLING the predictable part via a memoryless
// least-squares subtraction (`generated - k*mid`). Measured WORSE (24.9%
// imbalance vs 9% before) - the true relationship between a filtered/delayed
// signal and its source isn't a memoryless scalar multiple (a 90-degree
// phase shift of a sine has near-zero simple correlation with the original
// despite being entirely derived from it), so a single same-sample
// coefficient is the wrong model and can't reliably remove it. This class
// instead SCALES DOWN the overall generated amount when it's currently too
// correlated with Mid to be safe (a restraint, like the existing
// broadband-correlation mono-risk restraint on Region B/C) rather than
// trying to surgically cancel a relationship it can't model correctly.
// One-block-delayed feedback like the other correctors here.
class ContentPredictabilityRestraint {
public:
    void prepare(const double sampleRate) noexcept {
        const float srSafe = static_cast<float>(sampleRate > 1000.0 ? sampleRate : 48000.0);
        blockAlphaPerSample = std::exp(-1.0f / (0.8f * srSafe));
        reset();
    }
    void reset() noexcept { restraint = 1.0f; }

    [[nodiscard]] float currentRestraint() const noexcept { return restraint; }

    // Call once per block with the block's max-|correlation| between the
    // RAW generated content and Mid, evaluated across a handful of lags
    // (0/1/2/4/8/16ms), not same-sample only - see the caller (VxWidthProcessor
    // .cpp) for the multi-lag accumulation. Same-sample-only correlation
    // misses a delayed/phase-shifted copy of Mid (a signal can be entirely
    // derived from Mid, e.g. a pure delay, while reading near-zero at lag 0)
    // - exactly the ADT voice's own delay-line output. Updates the restraint
    // gain applied from the START of the NEXT block.
    void updateForNextBlock(const float maxAbsRho, const int numSamples) noexcept {
        // Soft knee: mild/expected correlation (|rho|<0.15) passes through
        // untouched; only meaningfully-correlated ("poorly decorrelated for
        // this material") content gets restrained.
        constexpr float kKnee = 0.15f;
        const float targetRestraint = 1.0f - juce::jlimit(0.0f, 1.0f,
            (juce::jlimit(0.0f, 1.0f, maxAbsRho) - kKnee) / (1.0f - kKnee));
        const float blockAlpha = std::pow(blockAlphaPerSample, static_cast<float>(std::max(1, numSamples)));
        restraint = blockAlpha * restraint + (1.0f - blockAlpha) * targetRestraint;
    }

private:
    float blockAlphaPerSample = 0.999999f;
    float restraint = 1.0f;
};

// §11.3's explicit guardrail: "compare processed output against unprocessed
// input... how much additional damage is being introduced by this
// processing?" The other restraints here (mono-risk correlation,
// predictability) all look at properties of the INPUT or the generated
// content in isolation - none of them directly verify that the FINAL
// output's mono-downmix energy hasn't degraded relative to the original.
// This closes that loop: compares (L+R) downmix RMS of the processed
// output against the original input, one block delayed. A drop indicates
// this processing introduced new destructive phase cancellation - restrain
// the generated-content contributors first (§11.3's own priority order:
// "reduce new decorrelated width first, reduce generated DoubleSide
// second... preserve original Mid at all times" - direct Region B Side
// expansion and Mid are deliberately NOT gated by this class, only Region
// C/Double are, matching that priority order).
class MonoDownmixGuardrail {
public:
    void prepare(const double sampleRate) noexcept {
        const float srSafe = static_cast<float>(sampleRate > 1000.0 ? sampleRate : 48000.0);
        blockAlphaPerSample = std::exp(-1.0f / (0.5f * srSafe));
        reset();
    }
    void reset() noexcept { restraint = 1.0f; }

    [[nodiscard]] float currentRestraint() const noexcept { return restraint; }

    // Call once per block with the (L+R) downmix sum-of-squares for the
    // original input and the processed output; updates the restraint gain
    // applied from the START of the NEXT block.
    void updateForNextBlock(const double inMonoSumSquared, const double outMonoSumSquared, const int numSamples) noexcept {
        if (inMonoSumSquared < 1.0e-9)
            return;
        const float ratio = juce::jlimit(0.0f, 2.0f,
            static_cast<float>(std::sqrt(outMonoSumSquared / inMonoSumSquared)));
        // Soft knee: no restraint at ratio>=1 (mono downmix unchanged or
        // louder), full restraint at ratio<=0.7 (>30% mono energy lost to
        // cancellation - severe).
        const float targetRestraint = juce::jlimit(0.0f, 1.0f, (ratio - 0.7f) / 0.3f);
        const float blockAlpha = std::pow(blockAlphaPerSample, static_cast<float>(std::max(1, numSamples)));
        restraint = blockAlpha * restraint + (1.0f - blockAlpha) * targetRestraint;
    }

private:
    float blockAlphaPerSample = 0.999999f;
    float restraint = 1.0f;
};

} // namespace vxsuite::width
