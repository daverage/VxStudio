#include "VxDeMudDsp.h"
#include <algorithm>
#include <cmath>

namespace vxsuite {
namespace tone_refine {

void DeMudDsp::prepare(double sampleRate, int maxBlockSize, int numChannels) {
    this->sampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    channels.clear();
    channels.resize(numChannels);

    // Design low-shelf filter: reduce below 300 Hz (mud region center)
    // Maximum reduction: -12dB (will be scaled by strength parameter)
    for (auto& ch : channels) {
        designLowShelfCoefficients(this->sampleRate, 300.0f, -12.0f, ch.shelfFilter);
    }

    reset();
}

void DeMudDsp::reset() noexcept {
    for (auto& ch : channels) {
        ch.shelfFilter.x1 = ch.shelfFilter.x2 = 0.0f;
        ch.shelfFilter.y1 = ch.shelfFilter.y2 = 0.0f;
        ch.mudEnvelopeDb = 0.0f;
    }
    lastReductionDb = 0.0f;
}

void DeMudDsp::designLowShelfCoefficients(
    double sampleRate,
    float shelfFreqHz,
    float gainDb,
    ShelfFilterState& state) {
    // Design normalized biquad low-shelf filter coefficients

    const float omega = 2.0f * juce::MathConstants<float>::pi * shelfFreqHz / static_cast<float>(sampleRate);
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);

    const float A = std::pow(10.0f, gainDb / 40.0f);  // sqrt(10^(gain/20))
    const float alpha = sinOmega / 2.0f;

    // Low-shelf formulas
    const float a0 = (A + 1.0f) + (A - 1.0f) * cosOmega + 2.0f * std::sqrt(A) * alpha;
    const float b0 = A * ((A + 1.0f) - (A - 1.0f) * cosOmega + 2.0f * std::sqrt(A) * alpha);
    const float b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosOmega);
    const float b2 = A * ((A + 1.0f) - (A - 1.0f) * cosOmega - 2.0f * std::sqrt(A) * alpha);
    const float a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosOmega);
    const float a2 = (A + 1.0f) + (A - 1.0f) * cosOmega - 2.0f * std::sqrt(A) * alpha;

    // Normalize
    state.b0 = b0 / a0;
    state.b1 = b1 / a0;
    state.b2 = b2 / a0;
    state.a1 = a1 / a0;
    state.a2 = a2 / a0;
}

float DeMudDsp::processBiquad(float sample, ShelfFilterState& state) noexcept {
    const float out = state.b0 * sample + state.b1 * state.x1 + state.b2 * state.x2
                    - state.a1 * state.y1 - state.a2 * state.y2;
    state.x2 = state.x1;
    state.x1 = sample;
    state.y2 = state.y1;
    state.y1 = out;
    return out;
}

void DeMudDsp::process(juce::AudioBuffer<float>& buffer, const Params& params) {
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    // Early exit if not active
    if (params.strength < 0.001f || params.detectionIntensity < 0.001f)
        return;

    // 1. MEASURE mud energy
    float maxMudEnergy = 0.0f;

    for (int ch = 0; ch < numChannels; ++ch) {
        const float* inputData = buffer.getReadPointer(ch);
        auto& chState = channels[ch];

        // Measure low-mid energy (mud region)
        float mudAccum = 0.0f;
        for (int i = 0; i < numSamples; ++i) {
            const float sample = inputData[i];
            // Simple low-pass to approximate 100-500 Hz band
            mudAccum = 0.98f * mudAccum + 0.02f * std::abs(sample);
            maxMudEnergy = std::max(maxMudEnergy, mudAccum);
        }
    }

    // 2. COMPUTE reduction amount
    const float mudDb = 20.0f * std::log10(std::max(maxMudEnergy, 1.0e-6f));

    // If mud is above -35dB, we have excess
    const float thresholdDb = -35.0f;
    float targetReductionDb = 0.0f;

    if (mudDb > thresholdDb) {
        // Scale reduction by how much mud exceeds threshold
        const float excessDb = mudDb - thresholdDb;
        targetReductionDb = -excessDb * params.strength * 0.5f;  // Max -6dB
    }

    // Gate by detection intensity
    targetReductionDb *= params.detectionIntensity;

    // Limit to -12dB maximum
    targetReductionDb = std::max(targetReductionDb, -12.0f);

    // Smooth reduction (prevent zipper noise)
    lastReductionDb = 0.95f * lastReductionDb + 0.05f * targetReductionDb;

    // 3. APPLY low-shelf filter with adjusted gain
    // The filter was designed for -12dB; scale it by our target reduction
    const float filterScaling = lastReductionDb / -12.0f;

    for (int ch = 0; ch < numChannels; ++ch) {
        float* audioData = buffer.getWritePointer(ch);
        auto& chState = channels[ch];

        for (int i = 0; i < numSamples; ++i) {
            // Apply low-shelf filter
            float filtered = processBiquad(audioData[i], chState.shelfFilter);

            // Blend: interpolate between original and filtered based on scaling
            audioData[i] = audioData[i] + (filtered - audioData[i]) * filterScaling;
        }
    }
}

} // namespace tone_refine
} // namespace vxsuite
