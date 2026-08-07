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
    // §6/§7 of docs/Task Based/VX_WIDTH_ENGINE_UPGRADE.md: continuous perceptual
    // width estimate, 0=centre, ~1=genuinely broad balanced stereo. Combines
    // Side/Mid RMS ratio with channel balance and a phase-risk correlation
    // modifier (see update()'s comment) so a hard-panned mono object or
    // anti-phase content don't read as "already maximally wide". Drives the
    // target-geometry gain model in VxWidthProcessor.cpp (Width becomes
    // relative to THIS, not an absolute gain on the requested control value)
    // - see that file's widthGapPercent.
    float estimatedWidth01 = 0.0f;
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
        smoothedWidth01 = 0.0f;
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
        double sumMidSq = 0.0, sumSideSq = 0.0;
        for (int i = 0; i < numSamples; ++i) {
            const float lv = l[i];
            const float rv = r[i];
            sumLR += static_cast<double>(lv) * rv;
            sumLL += static_cast<double>(lv) * lv;
            sumRR += static_cast<double>(rv) * rv;
            const float mono = 0.5f * (lv + rv);
            sumMonoSq += static_cast<double>(mono) * mono;
            const double mid = static_cast<double>(lv) + rv;
            const double side = static_cast<double>(lv) - rv;
            sumMidSq += mid * mid;
            sumSideSq += side * side;
        }

        const double denom = std::sqrt(sumLL * sumRR);
        const float rawCorrelation = denom > 1.0e-12
            ? juce::jlimit(-1.0f, 1.0f, static_cast<float>(sumLR / denom))
            : 1.0f;
        const float rawLoudnessDb = juce::Decibels::gainToDecibels(
            std::sqrt(static_cast<float>(sumMonoSq / numSamples)) + 1.0e-9f, -100.0f);

        // §6.1: Side/Mid RMS ratio alone is NOT a width estimate - it also
        // reads 1.0 for a mono source panned hard to one side (L=x, R=0:
        // Mid=Side=x) and for pure anti-phase (L=x, R=-x: Mid=0, Side huge).
        // Neither is "wide stereo" - the first is a mono OBJECT at an
        // extreme POSITION, the second is a phase-risk condition. Two
        // correction terms fix this without discarding the ratio (still the
        // right primary signal for genuinely broad, balanced stereo):
        //
        // (1) channelBalance: 0 when all energy sits in one channel (hard
        //     pan), 1 when L/R RMS are equal - "is there actually meaningful
        //     energy on BOTH sides", not just "is there a Side signal".
        // (2) phaseRiskFactor: 1 for correlation>=0 (including genuinely
        //     decorrelated-but-in-phase broad stereo, which must NOT be
        //     punished), ramping to 0 only as correlation goes negative -
        //     anti-phase content is phase risk, not width.
        constexpr float kMaxPracticalSideMidRatio = 1.0f;
        const float rawRatio = sumMidSq > 1.0e-12
            ? static_cast<float>(std::sqrt(sumSideSq / sumMidSq))
            : 0.0f;
        const float sideMidWidth01 = juce::jlimit(0.0f, 1.0f, rawRatio / kMaxPracticalSideMidRatio);

        const float lRms = static_cast<float>(std::sqrt(sumLL / numSamples));
        const float rRms = static_cast<float>(std::sqrt(sumRR / numSamples));
        const float channelBalance = (lRms + rRms) > 1.0e-9f
            ? juce::jlimit(0.0f, 1.0f, 2.0f * juce::jmin(lRms, rRms) / (lRms + rRms))
            : 0.0f;
        const float phaseRiskFactor = rawCorrelation >= 0.0f
            ? 1.0f
            : juce::jlimit(0.0f, 1.0f, 1.0f + rawCorrelation);

        const float rawWidth01 = juce::jlimit(0.0f, 1.0f,
            sideMidWidth01 * channelBalance * phaseRiskFactor);

        // ~150ms smoothing for correlation/loudness (matches §6.2's "continuous
        // weighting"); the width estimate itself is smoothed noticeably slower
        // (~450ms) since it feeds the gain model directly (§7) - a twitchy
        // estimate there would make the Width control itself feel unstable,
        // not just the UI.
        const float blockAlpha = std::exp(-static_cast<float>(numSamples) / (0.15f * static_cast<float>(sr)));
        const float widthBlockAlpha = std::exp(-static_cast<float>(numSamples) / (0.45f * static_cast<float>(sr)));
        if (!primed) {
            smoothedCorrelation = rawCorrelation;
            smoothedLoudnessDb = rawLoudnessDb;
            smoothedWidth01 = rawWidth01;
            primed = true;
        } else {
            smoothedCorrelation = blockAlpha * smoothedCorrelation + (1.0f - blockAlpha) * rawCorrelation;
            smoothedLoudnessDb = blockAlpha * smoothedLoudnessDb + (1.0f - blockAlpha) * rawLoudnessDb;
            smoothedWidth01 = widthBlockAlpha * smoothedWidth01 + (1.0f - widthBlockAlpha) * rawWidth01;
        }

        current.broadbandCorrelation = smoothedCorrelation;
        current.shortTermLoudnessDb = smoothedLoudnessDb;
        current.monoConfidence = juce::jlimit(0.0f, 1.0f, 0.5f * centerConfidence + 0.5f * monoScore);
        current.transientLikelihood = juce::jlimit(0.0f, 1.0f, transientRisk);
        current.estimatedWidth01 = juce::jlimit(0.0f, 1.0f, smoothedWidth01);
    }

    [[nodiscard]] InputAnalysisSnapshot snapshot() const noexcept { return current; }

private:
    double sr = 48000.0;
    float smoothedCorrelation = 1.0f;
    float smoothedLoudnessDb = -100.0f;
    float smoothedWidth01 = 0.0f;
    bool primed = false;
    InputAnalysisSnapshot current;
};

} // namespace vxsuite::width
