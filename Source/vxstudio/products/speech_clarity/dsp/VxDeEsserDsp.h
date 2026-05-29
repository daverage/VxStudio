#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace vxsuite {
namespace speech_clarity {

// ============================================================================
// DeEsser: Sibilance Reduction DSP
// ============================================================================
// Reduces harsh sibilance (/s/ and /z/ sounds) while preserving speech naturalness.
// Uses frequency-selective processing in the 4.5-8 kHz region.
//
// Control parameters:
//   - strength: 0-1, controls how aggressive the reduction
//   - detectionIntensity: 0-1, gates the effect (only works when sibilance detected)
//
// Algorithm:
//   1. Band-pass filter input in sibilance range (4.5-8 kHz)
//   2. Measure envelope of sibilance band
//   3. Apply soft gain reduction to that band
//   4. Blend back with original (preserves tone)
//
// Design philosophy:
//   - Surgical but transparent (not obviou processing)
//   - Preserves voice color (uses blend, not hard EQ)
//   - Only acts when sibilance is actually present
// ============================================================================

class DeEsserDsp {
public:
    struct Params {
        float strength = 0.0f;              // 0-1: reduction intensity
        float detectionIntensity = 0.0f;    // 0-1: LED feedback (gates effect)
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void process(juce::AudioBuffer<float>& buffer, const Params& params);

private:
    // Band-pass filter state for sibilance detection/processing
    struct BiquadState {
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;
    };

    // Per-channel state
    struct ChannelState {
        BiquadState bandPassFilter;
        float envelopeDb = 0.0f;
    };

    // Helper: design biquad coefficients for band-pass
    static void designBandPassCoefficients(
        double sampleRate,
        float centerHz,
        float qFactor,
        BiquadState& state);

    // Helper: process one sample through biquad
    static float processBiquad(float sample, BiquadState& state) noexcept;

    // Helper: soft knee compressor (used for reduction)
    static float applySoftCompression(
        float inputDb,
        float thresholdDb,
        float ratio,
        float kneeDb) noexcept;

    std::vector<ChannelState> channels;
    double sampleRate = 48000.0;
    float lastReductionDb = 0.0f;  // Smooth reduction amount
};

} // namespace speech_clarity
} // namespace vxsuite
