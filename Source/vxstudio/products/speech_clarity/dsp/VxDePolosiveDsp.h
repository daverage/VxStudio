#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

namespace vxsuite {
namespace speech_clarity {

// ============================================================================
// DePolosive: Plosive Burst Reduction
// ============================================================================
// Reduces harsh plosive bursts (/p/, /b/, /t/, /d/, /k/, /g/) without affecting
// the rest of the speech.
//
// Control parameters:
//   - strength: 0-1, controls gate severity
//   - detectionIntensity: 0-1, gates the effect (only works when plosive detected)
//
// Algorithm:
//   1. Detect sudden onset of low-frequency energy (burst characteristic)
//   2. Apply soft gate during burst (gentle, not abrupt)
//   3. Release quickly to restore normal speech
//
// Design philosophy:
//   - Transparent: doesn't affect normal speech
//   - Only gates during actual plosive burst
//   - Soft knee gate (no clicking/artifacts)
// ============================================================================

class DePolosiveDsp {
public:
    struct Params {
        float strength = 0.0f;              // 0-1: gate severity
        float detectionIntensity = 0.0f;    // 0-1: gates effect
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void process(juce::AudioBuffer<float>& buffer, const Params& params);

private:
    // Onset detector: measures burst characteristics
    struct OnsetDetectorState {
        float prevEnvelope = 0.0f;
        float peakHoldDb = -80.0f;
        int peakHoldSamples = 0;
    };

    // Per-channel state
    struct ChannelState {
        OnsetDetectorState onsetDetector;
        float currentGateDb = 0.0f;  // Current gate amount (0 = open, negative = reducing)
    };

    // Helper: measure burst envelope in low frequencies
    static float measureBurstEnergy(const float* data, int numSamples) noexcept;

    // Helper: detect onset (fast rise) in low-frequency envelope
    static float detectOnsetSlope(float currentEnv, float& previousEnv) noexcept;

    // Helper: soft gate for smooth reduction (no clicking)
    static float applySoftGate(
        float inputDb,
        float gateDb,
        float kneeDb) noexcept;

    std::vector<ChannelState> channels;
    double sampleRate = 48000.0;
    float lastOnsetEnergy = 0.0f;  // Smooth onset tracking
};

} // namespace speech_clarity
} // namespace vxsuite
