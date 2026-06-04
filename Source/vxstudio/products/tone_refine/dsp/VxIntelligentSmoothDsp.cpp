#include "VxIntelligentSmoothDsp.h"
#include <cmath>

namespace vxsuite {
namespace tone_refine {

void IntelligentSmoothDsp::prepare(double sampleRate, int maxBlockSize, int numChannels) {
    this->sampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    channels.clear();
    channels.resize(numChannels);

    // Configure one-pole filter with moderate cutoff (gentle smoothing)
    // Using ~500 Hz cutoff for gentle tonal smoothing
    const float cutoffHz = 500.0f;
    const float omega = 2.0f * juce::MathConstants<float>::pi * cutoffHz / static_cast<float>(sampleRate);
    const float coeff = omega / (1.0f + omega);

    for (auto& ch : channels) {
        ch.smoother.coeff = coeff;
        ch.smoother.z = 0.0f;
    }

    reset();
}

void IntelligentSmoothDsp::reset() noexcept {
    for (auto& ch : channels) {
        ch.smoother.z = 0.0f;
        ch.roughnessMetric = 0.0f;
    }
    lastSmoothingAmount = 0.0f;
}

float IntelligentSmoothDsp::measureRoughness(const float* data, int numSamples) noexcept {
    // Measure spectral roughness: high derivative = rough, low derivative = smooth
    // (Simplified: just look at sample-to-sample variation)

    if (numSamples < 2) return 0.0f;

    float roughness = 0.0f;
    float prevSample = 0.0f;

    for (int i = 0; i < numSamples; ++i) {
        const float sample = data[i];
        const float derivative = std::abs(sample - prevSample);
        roughness = std::max(roughness, derivative);
        prevSample = sample;
    }

    return roughness;
}

void IntelligentSmoothDsp::process(juce::AudioBuffer<float>& buffer, const Params& params) {
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    // Early exit if not active
    if (params.strength < 0.001f || params.detectionIntensity < 0.001f)
        return;

    // 1. MEASURE roughness
    float maxRoughness = 0.0f;

    for (int ch = 0; ch < numChannels; ++ch) {
        const float* inputData = buffer.getReadPointer(ch);
        float blockRoughness = measureRoughness(inputData, numSamples);
        maxRoughness = std::max(maxRoughness, blockRoughness);
    }

    // 2. COMPUTE smoothing amount
    // Only smooth if signal is rough, not if it's already smooth
    const float roughnessThreshold = 0.05f;
    float targetSmoothingAmount = 0.0f;

    if (maxRoughness > roughnessThreshold) {
        const float excessRoughness = (maxRoughness - roughnessThreshold) / 0.1f;
        targetSmoothingAmount = std::min(1.0f, excessRoughness) * params.strength * params.detectionIntensity;
    }

    // Limit smoothing to 0.3 (30% blend with smoothed signal = very subtle)
    targetSmoothingAmount = std::min(0.3f, targetSmoothingAmount);

    // Smooth the smoothing amount itself (meta-smoothing for natural feel)
    lastSmoothingAmount = 0.9f * lastSmoothingAmount + 0.1f * targetSmoothingAmount;

    // 3. APPLY gentle smoothing via one-pole filter
    for (int ch = 0; ch < numChannels; ++ch) {
        float* audioData = buffer.getWritePointer(ch);
        auto& chState = channels[ch];

        for (int i = 0; i < numSamples; ++i) {
            // One-pole low-pass
            const float sample = audioData[i];
            chState.smoother.z = sample * chState.smoother.coeff + chState.smoother.z * (1.0f - chState.smoother.coeff);

            // Blend: only a small amount of the smoothed signal (very subtle)
            audioData[i] = sample + (chState.smoother.z - sample) * lastSmoothingAmount;
        }
    }
}

} // namespace tone_refine
} // namespace vxsuite
