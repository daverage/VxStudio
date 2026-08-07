#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>
#include <vector>

// Sparse "velvet noise" style FIR decorrelator (VXWIDTH_BUILD.md §7.3-7.4):
// a short causal FIR made of a handful of unit-gain +/-1 taps at fixed,
// offline-selected fractional delay positions (not runtime-randomised, and
// not resampled per block - avoids the time-varying-coefficient
// discontinuities §7.3 rules out). Delay positions are stored as fractions
// of the decorrelator's window so the same pattern scales cleanly across
// sample rates (§14) instead of baking in sample counts.
namespace vxsuite::width {

struct VelvetTap {
    float fractionalDelay; // 0..1 within the decorrelator window
    float sign;            // +1 or -1
};

// Two fixed, disjoint-ish tap patterns so decorrelatorA and decorrelatorB
// produce independent, complementary output for the same input (§7.3).
//
// A corpus-wide search (`VXWidthAdtSeedOptimizer`, tests/
// VXWidthAdtSeedOptimizer.cpp, run 2026-08-07) found a candidate tap set
// scoring ~25% better on an isolated correlation+spectral-flatness proxy.
// Measured through the REAL processor pipeline it made NO measurable
// difference to output L/R imbalance (see VxWidthProcessor.cpp's ADT-seed
// comment for the same finding on the voice-seed side of this
// investigation - ContentPredictabilityRestraint already adaptively
// compensates for whatever correlation is present, regardless of these
// taps). Reverted to these original taps, which have real end-to-end
// evidence behind them (existing L/R balance regression coverage); the
// optimizer's tap search is kept as research tooling, not applied to
// production. See tasks/todo.md for the full investigation.
inline constexpr std::array<VelvetTap, 14> kDecorrelatorTapsA {{
    {0.03f, 1.0f}, {0.11f, -1.0f}, {0.18f, 1.0f}, {0.24f, 1.0f},
    {0.31f, -1.0f}, {0.39f, -1.0f}, {0.46f, 1.0f}, {0.54f, -1.0f},
    {0.61f, 1.0f}, {0.68f, 1.0f}, {0.76f, -1.0f}, {0.83f, -1.0f},
    {0.91f, 1.0f}, {0.97f, -1.0f},
}};

inline constexpr std::array<VelvetTap, 14> kDecorrelatorTapsB {{
    {0.07f, -1.0f}, {0.14f, 1.0f}, {0.21f, -1.0f}, {0.28f, -1.0f},
    {0.36f, 1.0f}, {0.43f, 1.0f}, {0.51f, -1.0f}, {0.58f, 1.0f},
    {0.65f, -1.0f}, {0.72f, -1.0f}, {0.79f, 1.0f}, {0.87f, 1.0f},
    {0.94f, -1.0f}, {0.99f, 1.0f},
}};

// Realtime-safe: ring buffer sized once in prepare(), no allocation after.
class VelvetDecorrelator {
public:
    void prepare(const double sampleRate, const float windowMs,
                 const std::array<VelvetTap, 14>& tapPattern) {
        const float srSafe = static_cast<float>(sampleRate > 1000.0 ? sampleRate : 48000.0);
        const int windowSamples = std::max(4, static_cast<int>(windowMs * 0.001f * srSafe));
        taps.fill({0, 0.0f});
        for (size_t i = 0; i < tapPattern.size(); ++i) {
            const int delay = juce::jlimit(0, windowSamples - 1,
                static_cast<int>(tapPattern[i].fractionalDelay * static_cast<float>(windowSamples)));
            taps[i] = { delay, tapPattern[i].sign };
        }
        ring.assign(static_cast<size_t>(windowSamples), 0.0f);
        writePos = 0;
        // Unit-tap sum on noise-like input scales like sqrt(N); normalise so
        // the decorrelator's output level roughly matches its input's (§7.4
        // "normalise the result before blending").
        normalisation = 1.0f / std::sqrt(static_cast<float>(tapPattern.size()));
    }

    void reset() noexcept {
        std::fill(ring.begin(), ring.end(), 0.0f);
        writePos = 0;
    }

    float process(const float x) noexcept {
        if (ring.empty())
            return 0.0f;
        const int ringSize = static_cast<int>(ring.size());
        ring[static_cast<size_t>(writePos)] = x;
        float acc = 0.0f;
        for (const auto& tap : taps) {
            int idx = writePos - tap.delaySamples;
            if (idx < 0)
                idx += ringSize;
            acc += tap.sign * ring[static_cast<size_t>(idx)];
        }
        writePos = (writePos + 1) % ringSize;
        return acc * normalisation;
    }

private:
    struct ResolvedTap {
        int delaySamples = 0;
        float sign = 0.0f;
    };
    std::array<ResolvedTap, 14> taps {};
    std::vector<float> ring;
    int writePos = 0;
    float normalisation = 1.0f;
};

} // namespace vxsuite::width
