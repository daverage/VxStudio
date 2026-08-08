#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Harmonic/residual/transient decomposition (VXWIDTH_BUILD.md §8.8,
// 2026-08-08 - the "major differentiator... prioritised after the base ADT
// engine is stable" item, previously not started). §8.8 explicitly says
// "this does not require source separation or machine learning" and lists
// several lightweight candidate methods (harmonic-percussive spectral
// masking, sinusoidal confidence, spectral flatness, onset detection,
// short-time phase stability, prediction error) - a genuine STFT-based
// harmonic-percussive mask is a much larger undertaking (FFT/overlap-add
// infrastructure, a latency budget §13's Live mode doesn't currently have,
// phase reconstruction for time-domain rendering) than fits this pass, and
// isn't what the spec mandates. This implements the lightweight path:
// block-rate normalised autocorrelation (a standard, cheap periodicity/
// tonality estimator - the same family of technique as a pitch detector's
// first stage) for the harmonic/residual axis, combined with the
// framework's own existing transient-risk analysis for the transient axis.
// Zero added latency, block-rate (not per-sample), real-time safe.
namespace vxsuite::width {

class HarmonicResidualAnalyser {
public:
    // Lag range covers ~70-600Hz (typical vocal/instrument fundamental
    // range for this product's use cases) - not an exhaustive pitch
    // detector, just enough lags to catch a strong periodicity peak if one
    // exists. Fixed count (not a std::vector sized per-block) so the
    // per-block accumulation below never allocates on the audio thread.
    static constexpr int kNumLags = 24;

    void prepare(const double sampleRate) noexcept {
        sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
        constexpr float kMinHz = 70.0f, kMaxHz = 600.0f;
        for (int i = 0; i < kNumLags; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kNumLags - 1);
            const float hz = kMinHz * std::pow(kMaxHz / kMinHz, t);
            lagSamples[static_cast<size_t>(i)] = juce::jmax(1, static_cast<int>(std::round(sr / hz)));
        }
        maxLagSamples = 0;
        for (const auto l : lagSamples) maxLagSamples = std::max(maxLagSamples, l);
        history.assign(static_cast<size_t>(maxLagSamples + 1), 0.0f);
        reset();
    }

    void reset() noexcept {
        std::fill(history.begin(), history.end(), 0.0f);
        writePos = 0;
        smoothedHarmonicConfidence01 = 0.0f;
        primed = false;
        clearAccumulators();
    }

    // Call once per sample, from inside the main processing loop, with that
    // sample's Mid value - matches the accumulate-then-finalize pattern the
    // rest of this file's per-block measurements already use (e.g.
    // PhaseRiskGuardrail::accumulateSample), so this never needs its own
    // block-sized scratch buffer (host block size can vary/exceed the
    // negotiated samplesPerBlock in rare cases - accumulating inline avoids
    // that entirely).
    void accumulateSample(const float mid) noexcept {
        const int historySize = static_cast<int>(history.size());
        history[static_cast<size_t>(writePos)] = mid;
        sum0Sq += static_cast<double>(mid) * mid;
        for (int k = 0; k < kNumLags; ++k) {
            int idx = writePos - lagSamples[static_cast<size_t>(k)];
            if (idx < 0) idx += historySize;
            const float lagged = history[static_cast<size_t>(idx)];
            sumLagProduct[static_cast<size_t>(k)] += static_cast<double>(mid) * lagged;
            sumLagSq[static_cast<size_t>(k)] += static_cast<double>(lagged) * lagged;
        }
        writePos = (writePos + 1) % historySize;
    }

    // Call once per block (after all accumulateSample() calls for that
    // block). transientRisk01 comes from the framework's own
    // VoiceAnalysisSnapshot (already computed ahead of processProduct(),
    // same source the rest of this product already uses for transient
    // protection - not recomputed here).
    void updateForNextBlock(const float transientRisk01, const int numSamples) noexcept {
        if (numSamples <= 0 || history.empty()) {
            clearAccumulators();
            return;
        }

        // Normalised autocorrelation peak across the lag set - a strongly
        // periodic (harmonic/tonal) signal has a lag where the signal
        // closely predicts itself (correlation near 1); noise-like/residual
        // content has no such lag (correlation stays low everywhere).
        float rawHarmonicConfidence = 0.0f;
        if (sum0Sq > 1.0e-9) {
            for (int k = 0; k < kNumLags; ++k) {
                const double denom = sum0Sq * sumLagSq[static_cast<size_t>(k)];
                if (denom < 1.0e-12)
                    continue;
                const float corr = static_cast<float>(sumLagProduct[static_cast<size_t>(k)] / std::sqrt(denom));
                rawHarmonicConfidence = std::max(rawHarmonicConfidence, corr);
            }
        }
        rawHarmonicConfidence = juce::jlimit(0.0f, 1.0f, rawHarmonicConfidence);

        // ~180ms smoothing - fast enough to track real phrase-to-phrase
        // changes (a vocal line moving between voiced/unvoiced content),
        // slow enough not to chase single-block noise in the autocorrelation
        // estimate itself.
        const float blockAlpha = std::exp(-static_cast<float>(numSamples) / (0.18f * static_cast<float>(sr)));
        if (!primed) {
            smoothedHarmonicConfidence01 = rawHarmonicConfidence;
            primed = true;
        } else {
            smoothedHarmonicConfidence01 = blockAlpha * smoothedHarmonicConfidence01 + (1.0f - blockAlpha) * rawHarmonicConfidence;
        }
        transientWeight = juce::jlimit(0.0f, 1.0f, transientRisk01);
        clearAccumulators();
    }

    // §8.8's three weights, always summing to 1: transient takes priority
    // (already a real, framework-computed onset measurement), the
    // remainder splits between harmonic/residual by autocorrelation
    // confidence.
    [[nodiscard]] float harmonicWeight() const noexcept { return smoothedHarmonicConfidence01 * (1.0f - transientWeight); }
    [[nodiscard]] float residualWeight() const noexcept { return (1.0f - smoothedHarmonicConfidence01) * (1.0f - transientWeight); }
    [[nodiscard]] float transientWeightValue() const noexcept { return transientWeight; }
    [[nodiscard]] float harmonicConfidence01() const noexcept { return smoothedHarmonicConfidence01; }

private:
    void clearAccumulators() noexcept {
        sum0Sq = 0.0;
        sumLagProduct.fill(0.0);
        sumLagSq.fill(0.0);
    }

    double sr = 48000.0;
    std::array<int, kNumLags> lagSamples {};
    int maxLagSamples = 0;
    std::vector<float> history;
    int writePos = 0;
    bool primed = false;
    float smoothedHarmonicConfidence01 = 0.0f;
    float transientWeight = 0.0f;

    double sum0Sq = 0.0;
    std::array<double, kNumLags> sumLagProduct {};
    std::array<double, kNumLags> sumLagSq {};
};

} // namespace vxsuite::width
