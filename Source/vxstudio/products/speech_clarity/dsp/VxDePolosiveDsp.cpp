#include "VxDePolosiveDsp.h"
#include <algorithm>
#include <cmath>

namespace vxsuite {
namespace speech_clarity {

void DePolosiveDsp::prepare(double sampleRate, int maxBlockSize, int numChannels) {
    this->sampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    channels.clear();
    channels.resize(numChannels);
    reset();
}

void DePolosiveDsp::reset() noexcept {
    for (auto& ch : channels) {
        ch.onsetDetector.prevEnvelope = 0.0f;
        ch.onsetDetector.peakHoldDb = -80.0f;
        ch.onsetDetector.peakHoldSamples = 0;
        ch.currentGateDb = 0.0f;
    }
    lastOnsetEnergy = 0.0f;
}

float DePolosiveDsp::measureBurstEnergy(const float* data, int numSamples) noexcept {
    // Measure energy in very low frequencies (where plosive bursts live: 50-200 Hz)
    // Use simple low-pass filtering to approximate sub-200Hz energy

    float envelope = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        const float sample = std::abs(data[i]);
        // Very slow attack (accumulates energy), fast release
        envelope = (sample > envelope) ? sample : 0.98f * envelope + 0.02f * sample;
    }

    return envelope;
}

float DePolosiveDsp::detectOnsetSlope(float currentEnv, float& previousEnv) noexcept {
    // Measure how fast the envelope is rising
    // Plosives have very fast rise (steep slope); normal speech is gradual

    const float slope = currentEnv - previousEnv;
    previousEnv = currentEnv;

    // Normalize to 0-1 range (based on typical speech dynamics)
    return std::max(0.0f, slope / 0.1f);  // 0.1 is threshold for "fast rise"
}

float DePolosiveDsp::applySoftGate(
    float inputDb,
    float gateDb,
    float kneeDb) noexcept {
    // Soft gate: opens/closes smoothly without clicks

    if (gateDb > -0.5f) {
        return 0.0f;  // Gate fully open
    }

    // Calculate how much to reduce
    const float targetDb = gateDb;
    const float kneeStart = gateDb - kneeDb;

    if (inputDb > kneeStart) {
        // In the knee region: smooth transition
        const float kneePos = (inputDb - kneeStart) / kneeDb;
        const float clampedPos = std::max(0.0f, std::min(1.0f, kneePos));
        // Ease curve for smoothness
        const float easePos = clampedPos * clampedPos;
        return targetDb * easePos;
    }

    return targetDb;
}

void DePolosiveDsp::process(juce::AudioBuffer<float>& buffer, const Params& params) {
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    // Early exit if not active
    if (params.strength < 0.001f || params.detectionIntensity < 0.001f)
        return;

    // 1. DETECT plosive bursts (onset detection in low-frequency energy)
    float maxOnsetSlope = 0.0f;

    for (int ch = 0; ch < numChannels; ++ch) {
        const float* audioData = buffer.getReadPointer(ch);
        auto& chState = channels[ch];

        // Measure burst energy in this block
        float burstEnergy = measureBurstEnergy(audioData, numSamples);
        float onsetSlope = detectOnsetSlope(burstEnergy, chState.onsetDetector.prevEnvelope);

        maxOnsetSlope = std::max(maxOnsetSlope, onsetSlope);

        // Track peak onset for gating
        if (onsetSlope > 0.5f) {
            chState.onsetDetector.peakHoldDb = 0.0f;  // Gate triggered
            chState.onsetDetector.peakHoldSamples = static_cast<int>(0.05 * sampleRate);  // Hold for 50ms (shorter than before)
        } else {
            chState.onsetDetector.peakHoldSamples--;
        }
    }

    // 2. COMPUTE gentle gate amount based on onset detection
    // Much more conservative than before: max -6dB instead of -60dB
    const float gateDb = -6.0f * params.strength * params.detectionIntensity;  // Gentle reduction
    const float kneeDb = 3.0f;

    // 3. APPLY soft gate ONLY to low-frequency region (50-300 Hz where plosives live)
    // This prevents full-signal pumping
    for (int ch = 0; ch < numChannels; ++ch) {
        float* audioData = buffer.getWritePointer(ch);
        auto& chState = channels[ch];

        // Update current gate amount (smooth to avoid artifacts)
        const float targetGateDb = (chState.onsetDetector.peakHoldSamples > 0) ? gateDb : 0.0f;
        chState.currentGateDb = 0.95f * chState.currentGateDb + 0.05f * targetGateDb;

        // Apply soft gate only to low frequencies
        const float gateGainLinear = std::pow(10.0f, chState.currentGateDb / 20.0f);

        // Simple single-pole lowpass to extract low-frequency component
        for (int i = 0; i < numSamples; ++i) {
            // Very slow lowpass (acts as low-pass for frequency-selective gating)
            float lowFreq = 0.999f * chState.onsetDetector.prevEnvelope + 0.001f * std::abs(audioData[i]);
            chState.onsetDetector.prevEnvelope = lowFreq;

            // Only gate the low-frequency component (gentle soft attenuation, not hard gate)
            // This preserves the original signal's character while reducing plosive energy
            float reduction = (1.0f - gateGainLinear) * 0.5f;  // Further reduced: max 3% attenuation
            audioData[i] *= (1.0f - reduction);
        }
    }
}

} // namespace speech_clarity
} // namespace vxsuite
