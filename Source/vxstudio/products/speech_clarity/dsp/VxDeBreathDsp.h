#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace vxsuite {
namespace speech_clarity {

// ============================================================================
// DeBreath: Breath & Wind Noise Reduction
// ============================================================================
// Reduces breathing sounds and wind noise while preserving voice integrity.
//
// Control parameters:
//   - strength: 0-1, controls noise reduction intensity
//   - detectionIntensity: 0-1, gates the effect
//
// Algorithm:
//   1. Identify breath/wind as low-frequency noise (spectral flatness)
//   2. Selective subtraction (not full gating)
//   3. Preserve voice fundamentals by only reducing non-harmonic content
//
// Design philosophy:
//   - Transparent: doesn't affect sung vowels or consonants
//   - Surgical: targets only noise-like characteristics
//   - Preserves naturalness (selective removal, not obvious processing)
// ============================================================================

class DeBreathDsp {
public:
    struct Params {
        float strength = 0.0f;              // 0-1: reduction strength
        float detectionIntensity = 0.0f;    // 0-1: gates effect
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void process(juce::AudioBuffer<float>& buffer, const Params& params);

private:
    // Breath detector state
    struct BreathDetectorState {
        float noiseFloorDb = -80.0f;
        float smoothedFlatness = 0.0f;
    };

    // Per-channel state
    struct ChannelState {
        BreathDetectorState detector;
        float noiseBuffer[2048];  // Noise profile for spectral subtraction
        int noiseBufferSize = 0;
        float lastSubtractionAmount = 0.0f;
    };

    // Helper: estimate spectral flatness (noise-like character)
    static float estimateSpectralFlatness(const float* data, int numSamples) noexcept;

    // Helper: measure noise floor (minimum spectral energy)
    static float measureNoiseFloor(const float* data, int numSamples) noexcept;

    std::vector<ChannelState> channels;
    double sampleRate = 48000.0;
    int blockCounter = 0;
};

} // namespace speech_clarity
} // namespace vxsuite
