#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

namespace vxsuite {

/**
 * Signal evidence computed from raw input audio, block-rate, zero-heap.
 * Products read this to inform DSP protection decisions.
 * ModePolicy is the product-level intent; VoiceAnalysisSnapshot is the signal evidence.
 */
struct VoiceAnalysisSnapshot {
    float speechPresence   = 0.0f; // [0,1] active speech likelihood
    float speechStability  = 0.0f; // [0,1] signal stability vs transient/erratic
    float speechBandEnergy = 0.0f; // [0,1] normalised energy in 120–4200 Hz
    float directness       = 0.0f; // [0,1] direct-sound vs diffuse/tail ratio
    float tailLikelihood   = 0.0f; // [0,1] reverberant tail likelihood
    float centerConfidence = 0.0f; // [0,1] stereo centre/mono confidence (L≈R)
    float transientRisk    = 0.0f; // [0,1] transient attack risk
    float protectVoice     = 0.0f; // [0,1] composite protection recommendation
};

/**
 * Realtime-safe analysis state. All working memory pre-allocated in prepare().
 * Call update() once per block before product DSP. Thread: audio thread only.
 */
class VoiceAnalysisState {
public:
    void prepare(double sampleRate, int maxSamplesPerBlock);
    void reset();
    void update(const juce::AudioBuffer<float>& input, int numSamples);
    VoiceAnalysisSnapshot snapshot() const noexcept { return current; }

private:
    // Per-channel one-pole filter states (index 0=L, 1=R)
    std::array<float, 2> hpState { 0.0f, 0.0f };
    std::array<float, 2> bpState { 0.0f, 0.0f };

    // Envelope trackers
    float envFast   = 0.0f;        // ~7 ms (transient detection)
    float envSlow   = 0.0f;        // ~50 ms (speech-rate)
    float envVslow  = 1.0e-6f;     // ~500 ms (noise floor reference)

    // Composite smoothers
    float stability  = 0.0f;
    float centerCorr = 0.0f;
    float directness = 0.0f;

    // Pre-computed one-pole coefficients
    float alphaHp    = 0.0f;
    float alphaBp    = 0.0f;
    float alphaFast  = 0.0f;
    float alphaSlow  = 0.0f;
    float alphaVslow = 0.0f;
    float alphaStab  = 0.0f;
    float alphaCenter = 0.0f;
    float alphaDir   = 0.0f;

    VoiceAnalysisSnapshot current;

    static float onePoleAlpha(double sampleRate, float cutoffHz) noexcept;
};

} // namespace vxsuite
