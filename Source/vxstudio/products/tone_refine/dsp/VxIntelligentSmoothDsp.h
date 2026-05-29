#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace vxsuite {
namespace tone_refine {

// ============================================================================
// IntelligentSmooth: Transparent Tonal Smoothing
// ============================================================================
// Applies subtle, transparent smoothing that reduces harshness and roughness
// without obviously processing the signal. Most invisible of all tools.
//
// Control parameters:
//   - strength: 0-1, controls smoothing intensity
//   - detectionIntensity: 0-1, gates the effect
//
// Algorithm:
//   1. Measure spectral roughness (rapid changes = rough, smooth = glassy)
//   2. Apply smooth variable gain function
//   3. Preserve phase and transients
//   4. Very gentle processing (only 2-3dB max)
//
// Design philosophy:
//   - Invisible (most subtle of all processing)
//   - Preserves attack/transients (doesn't dull attack)
//   - Transparent (no obvious artifacts)
//   - Gentle (only 2-3dB, never more)
// ============================================================================

class IntelligentSmoothDsp {
public:
    struct Params {
        float strength = 0.0f;              // 0-1: smoothing intensity
        float detectionIntensity = 0.0f;    // 0-1: gates effect
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void process(juce::AudioBuffer<float>& buffer, const Params& params);

private:
    // Very simple one-pole low-pass for smoothing
    struct SmoothingState {
        float z = 0.0f;
        float coeff = 0.0f;
    };

    // Per-channel state
    struct ChannelState {
        SmoothingState smoother;
        float roughnessMetric = 0.0f;
    };

    // Measure spectral roughness (derivative energy)
    static float measureRoughness(const float* data, int numSamples) noexcept;

    std::vector<ChannelState> channels;
    double sampleRate = 48000.0;
    float lastSmoothingAmount = 0.0f;
};

} // namespace tone_refine
} // namespace vxsuite
