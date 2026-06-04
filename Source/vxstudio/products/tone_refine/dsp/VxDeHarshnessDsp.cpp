#include "VxDeHarshnessDsp.h"
#include <cmath>

namespace vxsuite {
namespace tone_refine {

void DeHarshnessDsp::prepare(double sampleRate, int maxBlockSize, int numChannels) {
    this->sampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    channels.clear();
    channels.resize(numChannels);

    // Design high-shelf filter: reduce above 2 kHz (presence region)
    // Maximum reduction: -8dB (will be scaled by strength parameter)
    for (auto& ch : channels) {
        designHighShelfCoefficients(this->sampleRate, 2000.0f, -8.0f, ch.shelfFilter);
    }

    reset();
}

void DeHarshnessDsp::reset() noexcept {
    for (auto& ch : channels) {
        ch.shelfFilter.x1 = ch.shelfFilter.x2 = 0.0f;
        ch.shelfFilter.y1 = ch.shelfFilter.y2 = 0.0f;
        ch.peakDetectionEnv = 0.0f;
    }
    lastReductionDb = 0.0f;
}

void DeHarshnessDsp::designHighShelfCoefficients(
    double sampleRate,
    float shelfFreqHz,
    float gainDb,
    ShelfFilterState& state) {
    // Design normalized biquad high-shelf filter coefficients

    const float omega = 2.0f * juce::MathConstants<float>::pi * shelfFreqHz / static_cast<float>(sampleRate);
    const float sinOmega = std::sin(omega);
    const float cosOmega = std::cos(omega);

    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float alpha = sinOmega / 2.0f;

    // High-shelf formulas
    const float a0 = (A + 1.0f) - (A - 1.0f) * cosOmega + 2.0f * std::sqrt(A) * alpha;
    const float b0 = A * ((A + 1.0f) + (A - 1.0f) * cosOmega + 2.0f * std::sqrt(A) * alpha);
    const float b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosOmega);
    const float b2 = A * ((A + 1.0f) + (A - 1.0f) * cosOmega - 2.0f * std::sqrt(A) * alpha);
    const float a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cosOmega);
    const float a2 = (A + 1.0f) - (A - 1.0f) * cosOmega - 2.0f * std::sqrt(A) * alpha;

    // Normalize
    state.b0 = b0 / a0;
    state.b1 = b1 / a0;
    state.b2 = b2 / a0;
    state.a1 = a1 / a0;
    state.a2 = a2 / a0;
}

float DeHarshnessDsp::processBiquad(float sample, ShelfFilterState& state) noexcept {
    const float out = state.b0 * sample + state.b1 * state.x1 + state.b2 * state.x2
                    - state.a1 * state.y1 - state.a2 * state.y2;
    state.x2 = state.x1;
    state.x1 = sample;
    state.y2 = state.y1;
    state.y1 = out;
    return out;
}

float DeHarshnessDsp::detectPresencePeakSharpness(const float* data, int numSamples) noexcept {
    // Detect how "sharp" or isolated the presence peak is
    // High sharpness = isolated peak (harsh), low sharpness = broad presence

    if (numSamples < 4) return 0.0f;

    float peakiness = 0.0f;
    float prevSample = 0.0f;

    for (int i = 0; i < numSamples; ++i) {
        const float sample = std::abs(data[i]);
        // Measure sudden changes (sharp peaks)
        const float derivative = std::abs(sample - prevSample);
        peakiness = std::max(peakiness, derivative);
        prevSample = sample;
    }

    return peakiness;
}

void DeHarshnessDsp::process(juce::AudioBuffer<float>& buffer, const Params& params) {
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    // Early exit if not active
    if (params.strength < 0.001f || params.detectionIntensity < 0.001f)
        return;

    // 1. MEASURE presence peak characteristics
    float maxPresencePeakiness = 0.0f;

    for (int ch = 0; ch < numChannels; ++ch) {
        const float* inputData = buffer.getReadPointer(ch);

        // Measure presence peak sharpness
        float blockPeakiness = detectPresencePeakSharpness(inputData, numSamples);
        maxPresencePeakiness = std::max(maxPresencePeakiness, blockPeakiness);
    }

    // 2. COMPUTE reduction amount
    // Sharper peaks = more reduction
    const float thresholdPeakiness = 0.1f;
    float targetReductionDb = 0.0f;

    if (maxPresencePeakiness > thresholdPeakiness) {
        const float excessPeakiness = (maxPresencePeakiness - thresholdPeakiness) / 0.1f;
        targetReductionDb = -std::min(1.0f, excessPeakiness) * params.strength * 8.0f;
    }

    // Gate by detection intensity
    targetReductionDb *= params.detectionIntensity;

    // Limit to -8dB maximum
    targetReductionDb = std::max(targetReductionDb, -8.0f);

    // Smooth reduction
    lastReductionDb = 0.95f * lastReductionDb + 0.05f * targetReductionDb;

    // 3. APPLY high-shelf filter
    const float filterScaling = lastReductionDb / -8.0f;

    for (int ch = 0; ch < numChannels; ++ch) {
        float* audioData = buffer.getWritePointer(ch);
        auto& chState = channels[ch];

        for (int i = 0; i < numSamples; ++i) {
            // Apply high-shelf filter
            float filtered = processBiquad(audioData[i], chState.shelfFilter);

            // Blend
            audioData[i] = audioData[i] + (filtered - audioData[i]) * filterScaling;
        }
    }
}

} // namespace tone_refine
} // namespace vxsuite
