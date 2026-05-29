#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace vxsuite {
namespace tone_refine {

// ============================================================================
// DeHarshness: Presence Peak Reduction (2-5 kHz Brittleness Removal)
// ============================================================================
// Reduces the sharp, brittle presence peak that makes speech sound harsh.
//
// Control parameters:
//   - strength: 0-1, controls reduction intensity
//   - detectionIntensity: 0-1, gates the effect
//
// Algorithm:
//   1. Detect presence peak (2-5 kHz region)
//   2. Measure peak sharpness/isolation (is it an isolated spike?)
//   3. Apply gentle tilt or notch reduction
//   4. Only reduce when harsh peak detected
//
// Design philosophy:
//   - Targeted (only affects isolated peaks, not broad presence)
//   - Transparent (gentle tilt, not aggressive notch)
//   - Preserves speech clarity (doesn't over-reduce)
// ============================================================================

class DeHarshnessDsp {
public:
    struct Params {
        float strength = 0.0f;              // 0-1: reduction intensity
        float detectionIntensity = 0.0f;    // 0-1: gates effect
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void process(juce::AudioBuffer<float>& buffer, const Params& params);

private:
    // High-shelf filter for presence reduction
    struct ShelfFilterState {
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;
    };

    // Per-channel state
    struct ChannelState {
        ShelfFilterState shelfFilter;
        float peakDetectionEnv = 0.0f;
    };

    // Design high-shelf filter
    static void designHighShelfCoefficients(
        double sampleRate,
        float shelfFreqHz,
        float gainDb,
        ShelfFilterState& state);

    // Process sample through biquad
    static float processBiquad(float sample, ShelfFilterState& state) noexcept;

    // Detect presence peak sharpness
    static float detectPresencePeakSharpness(const float* data, int numSamples) noexcept;

    std::vector<ChannelState> channels;
    double sampleRate = 48000.0;
    float lastReductionDb = 0.0f;
};

} // namespace tone_refine
} // namespace vxsuite
