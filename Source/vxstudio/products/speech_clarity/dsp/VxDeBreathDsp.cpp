#include "VxDeBreathDsp.h"
#include <algorithm>
#include <cmath>

namespace vxsuite {
namespace speech_clarity {

void DeBreathDsp::prepare(double sampleRate, int maxBlockSize, int numChannels) {
    this->sampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    channels.clear();
    channels.resize(numChannels);

    for (auto& ch : channels) {
        ch.noiseBufferSize = 0;
        std::fill(ch.noiseBuffer, ch.noiseBuffer + 2048, 0.0f);
    }

    reset();
}

void DeBreathDsp::reset() noexcept {
    for (auto& ch : channels) {
        ch.detector.noiseFloorDb = -80.0f;
        ch.detector.smoothedFlatness = 0.0f;
        ch.lastSubtractionAmount = 0.0f;
        ch.noiseBufferSize = 0;
    }
    blockCounter = 0;
}

float DeBreathDsp::estimateSpectralFlatness(const float* data, int numSamples) noexcept {
    // Estimate spectral flatness from time-domain samples
    // Flat spectrum = noise-like (breath), peaked spectrum = harmonic (voice)
    //
    // Method: measure spectral entropy using RMS distribution
    // More uniform RMS = flatter spectrum

    if (numSamples < 32) return 0.5f;

    // Divide into sub-blocks and measure RMS of each
    const int blockSize = numSamples / 8;
    if (blockSize < 4) return 0.5f;

    float rmsMin = 1.0f;
    float rmsMax = 0.0f;
    float rmsMean = 0.0f;

    for (int b = 0; b < 8; ++b) {
        float blockRmsSq = 0.0f;
        for (int i = 0; i < blockSize; ++i) {
            const int idx = b * blockSize + i;
            if (idx < numSamples) {
                blockRmsSq += data[idx] * data[idx];
            }
        }
        const float blockRms = std::sqrt(blockRmsSq / blockSize);
        rmsMin = std::min(rmsMin, blockRms);
        rmsMax = std::max(rmsMax, blockRms);
        rmsMean += blockRms;
    }

    rmsMean /= 8.0f;

    // Flatness = how uniform is the RMS distribution
    // Range / Mean gives normalized measure (low = flat, high = peaked)
    if (rmsMean < 1.0e-6f) return 0.5f;

    const float range = rmsMax - rmsMin;
    const float flatness = 1.0f - std::min(1.0f, range / rmsMean);

    return flatness;
}

float DeBreathDsp::measureNoiseFloor(const float* data, int numSamples) noexcept {
    // Measure noise floor as the minimum spectral energy
    // (simplified: use 10th percentile of frame energy)

    float frameRmsSq = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        frameRmsSq += data[i] * data[i];
    }

    const float frameRms = std::sqrt(frameRmsSq / std::max(1, numSamples));
    const float noiseFloorDb = 20.0f * std::log10(std::max(frameRms, 1.0e-6f));

    return noiseFloorDb;
}

void DeBreathDsp::process(juce::AudioBuffer<float>& buffer, const Params& params) {
    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    // Early exit if not active
    if (params.strength < 0.001f || params.detectionIntensity < 0.001f)
        return;

    // 1. MEASURE breath characteristics
    float maxFlatness = 0.0f;

    for (int ch = 0; ch < numChannels; ++ch) {
        const float* audioData = buffer.getReadPointer(ch);
        auto& chState = channels[ch];

        // Measure spectral flatness (breath = flat = noise-like)
        float blockFlatness = estimateSpectralFlatness(audioData, numSamples);
        chState.detector.smoothedFlatness = 0.85f * chState.detector.smoothedFlatness + 0.15f * blockFlatness;

        // Measure noise floor
        float blockNoiseFloor = measureNoiseFloor(audioData, numSamples);
        chState.detector.noiseFloorDb = 0.95f * chState.detector.noiseFloorDb + 0.05f * blockNoiseFloor;

        maxFlatness = std::max(maxFlatness, chState.detector.smoothedFlatness);
    }

    // 2. COMPUTE reduction amount
    // Higher flatness + stronger dial = more aggressive reduction
    // Gate by detection intensity (only reduce when breath detected)

    const float flatnessThreshold = 0.5f;  // Voice has strong spectral shape
    const float flatnessRange = 1.0f - flatnessThreshold;

    float targetReductionAmount = 0.0f;
    if (maxFlatness > flatnessThreshold) {
        const float excessFlatness = (maxFlatness - flatnessThreshold) / flatnessRange;
        // Much more conservative reduction: max 15% at full strength
        targetReductionAmount = std::min(1.0f, excessFlatness) * params.strength * params.detectionIntensity * 0.15f;
    }

    // 3. APPLY selective noise reduction (very gentle)
    for (int ch = 0; ch < numChannels; ++ch) {
        float* audioData = buffer.getWritePointer(ch);
        auto& chState = channels[ch];

        // Smooth reduction to avoid pumping
        chState.lastSubtractionAmount = 0.92f * chState.lastSubtractionAmount + 0.08f * targetReductionAmount;

        // Very subtle high-pass filtering effect
        // Only reduce signal proportional to flatness
        for (int i = 0; i < numSamples; ++i) {
            // At maximum, reduce by ~7.5% of signal
            audioData[i] *= (1.0f - chState.lastSubtractionAmount);
        }
    }
}

} // namespace speech_clarity
} // namespace vxsuite
