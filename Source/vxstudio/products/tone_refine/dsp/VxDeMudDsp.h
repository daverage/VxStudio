#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace vxsuite {
namespace tone_refine {

// ============================================================================
// DeMud: Low-Mid Reduction (100-500 Hz Boxiness Removal)
// ============================================================================
// Reduces the heavy, boxy low-mid buildup that makes speech sound muddy.
//
// Control parameters:
//   - strength: 0-1, controls reduction intensity
//   - detectionIntensity: 0-1, gates the effect
//
// Algorithm:
//   1. Measure energy in 100-500 Hz band (mud region)
//   2. Compare to overall spectrum (is there excess?)
//   3. Apply gentle shelving EQ reduction
//   4. Only reduce when excess mud detected
//
// Design philosophy:
//   - Surgical (targets specific frequency range)
//   - Gentle (shelving EQ, not aggressive notch)
//   - Preserves voice character (only removes excess)
// ============================================================================

class DeMudDsp {
public:
    struct Params {
        float strength = 0.0f;              // 0-1: reduction intensity
        float detectionIntensity = 0.0f;    // 0-1: gates effect
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void process(juce::AudioBuffer<float>& buffer, const Params& params);

private:
    // Low-shelf filter for mud reduction
    struct ShelfFilterState {
        // Biquad coefficients for low-shelf
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;
    };

    // Per-channel state
    struct ChannelState {
        ShelfFilterState shelfFilter;
        float mudEnvelopeDb = 0.0f;
    };

    // Design low-shelf filter for mud reduction
    static void designLowShelfCoefficients(
        double sampleRate,
        float shelfFreqHz,
        float gainDb,
        ShelfFilterState& state);

    // Process sample through biquad
    static float processBiquad(float sample, ShelfFilterState& state) noexcept;

    std::vector<ChannelState> channels;
    double sampleRate = 48000.0;
    float lastReductionDb = 0.0f;  // Smooth reduction amount
};

} // namespace tone_refine
} // namespace vxsuite
