#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// Input analysis (VXWIDTH_BUILD.md §6): continuous, smoothed evidence read
// by downstream regions before they act, not exposed to the user. Reuses
// vxsuite::VoiceAnalysisSnapshot/SignalQualitySnapshot (already computed by
// ProcessorBase on the raw input, ahead of processProduct()) for mono/centre
// confidence and transient risk rather than re-deriving them; adds only what
// the framework doesn't already provide (broadband correlation, short-term
// loudness).
namespace vxsuite::width {

struct InputAnalysisSnapshot {
    // §6.1: 0 = clearly stereo, 1 = effectively mono. Blend of framework
    // centre-confidence (L≈R) and side/mid mono score - continuous, no
    // binary switching (§6.2).
    float monoConfidence = 0.0f;
    // Smoothed Pearson L/R correlation, -1..+1. Feeds mono/phase-risk
    // restraint (§11 preview; full guardrail lands in a later build step).
    float broadbandCorrelation = 1.0f;
    float shortTermLoudnessDb = -100.0f;
    float transientLikelihood = 0.0f;
};

class InputAnalyser {
public:
    void prepare(const double sampleRate) noexcept {
        sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
        reset();
    }

    void reset() noexcept {
        smoothedCorrelation = 1.0f;
        smoothedLoudnessDb = -100.0f;
        primed = false;
        current = {};
        current.broadbandCorrelation = 1.0f;
    }

    void update(const juce::AudioBuffer<float>& buffer, const int numSamples,
                const float centerConfidence, const float monoScore,
                const float transientRisk) noexcept {
        if (numSamples <= 0)
            return;

        const int channels = juce::jmin(buffer.getNumChannels(), 2);
        const auto* l = buffer.getReadPointer(0);
        const auto* r = channels > 1 ? buffer.getReadPointer(1) : l;

        double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0, sumMonoSq = 0.0;
        for (int i = 0; i < numSamples; ++i) {
            const float lv = l[i];
            const float rv = r[i];
            sumLR += static_cast<double>(lv) * rv;
            sumLL += static_cast<double>(lv) * lv;
            sumRR += static_cast<double>(rv) * rv;
            const float mono = 0.5f * (lv + rv);
            sumMonoSq += static_cast<double>(mono) * mono;
        }

        const double denom = std::sqrt(sumLL * sumRR);
        const float rawCorrelation = denom > 1.0e-12
            ? juce::jlimit(-1.0f, 1.0f, static_cast<float>(sumLR / denom))
            : 1.0f;
        const float rawLoudnessDb = juce::Decibels::gainToDecibels(
            std::sqrt(static_cast<float>(sumMonoSq / numSamples)) + 1.0e-9f, -100.0f);

        // ~150ms smoothing, matches §6.2's "continuous weighting" requirement.
        const float blockAlpha = std::exp(-static_cast<float>(numSamples) / (0.15f * static_cast<float>(sr)));
        if (!primed) {
            smoothedCorrelation = rawCorrelation;
            smoothedLoudnessDb = rawLoudnessDb;
            primed = true;
        } else {
            smoothedCorrelation = blockAlpha * smoothedCorrelation + (1.0f - blockAlpha) * rawCorrelation;
            smoothedLoudnessDb = blockAlpha * smoothedLoudnessDb + (1.0f - blockAlpha) * rawLoudnessDb;
        }

        current.broadbandCorrelation = smoothedCorrelation;
        current.shortTermLoudnessDb = smoothedLoudnessDb;
        current.monoConfidence = juce::jlimit(0.0f, 1.0f, 0.5f * centerConfidence + 0.5f * monoScore);
        current.transientLikelihood = juce::jlimit(0.0f, 1.0f, transientRisk);
    }

    [[nodiscard]] InputAnalysisSnapshot snapshot() const noexcept { return current; }

private:
    double sr = 48000.0;
    float smoothedCorrelation = 1.0f;
    float smoothedLoudnessDb = -100.0f;
    bool primed = false;
    InputAnalysisSnapshot current;
};

} // namespace vxsuite::width
