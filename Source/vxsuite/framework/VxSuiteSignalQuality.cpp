#include "VxSuiteSignalQuality.h"

#include <cmath>

namespace vxsuite {

namespace {

inline float clamp01(const float value) noexcept {
    return juce::jlimit(0.0f, 1.0f, value);
}

inline float onePoleAlpha(const double sampleRate, const float cutoffHz) noexcept {
    if (sampleRate <= 0.0 || cutoffHz <= 0.0f)
        return 0.0f;
    return std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz / static_cast<float>(sampleRate));
}

} // namespace

float SignalQualityState::timeAlpha(const double sampleRate, const float seconds) noexcept {
    const float srSafe = static_cast<float>(sampleRate > 1000.0 ? sampleRate : 48000.0);
    const float samples = std::max(1.0f, seconds * srSafe);
    return std::exp(-1.0f / samples);
}

void SignalQualityState::prepare(const double sampleRate, const int /*maxSamplesPerBlock*/) {
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    alpha500 = onePoleAlpha(sr, 500.0f);
    alpha4k = onePoleAlpha(sr, 4000.0f);
    reset();
}

void SignalQualityState::reset() {
    low500L = 0.0f;
    low500R = 0.0f;
    low4kL = 0.0f;
    low4kR = 0.0f;
    smoothedPeak = 0.0f;
    smoothedRms = 0.0f;
    current = {};
    current.separationConfidence = 1.0f;
}

void SignalQualityState::update(const juce::AudioBuffer<float>& input, const int numSamples) {
    if (numSamples <= 0)
        return;

    const int channels = std::min(input.getNumChannels(), 2);
    if (channels <= 0)
        return;

    const auto* chL = input.getReadPointer(0);
    const auto* chR = channels > 1 ? input.getReadPointer(1) : chL;

    float sideAccum = 0.0f;
    float midAccum = 0.0f;
    float framePeak = 0.0f;
    float sumSq = 0.0f;
    float loAccum = 0.0f;
    float hiAccum = 0.0f;

    for (int i = 0; i < numSamples; ++i) {
        const float left = chL[i];
        const float right = chR[i];
        const float mono = 0.5f * (left + right);
        const float side = 0.5f * (left - right);

        sideAccum += std::abs(side);
        midAccum += std::abs(mono);

        const float absMono = std::abs(mono);
        framePeak = std::max(framePeak, absMono);
        sumSq += mono * mono;

        low500L = alpha500 * low500L + (1.0f - alpha500) * left;
        low500R = alpha500 * low500R + (1.0f - alpha500) * right;
        low4kL = alpha4k * low4kL + (1.0f - alpha4k) * left;
        low4kR = alpha4k * low4kR + (1.0f - alpha4k) * right;

        const float lowBand = 0.5f * (std::abs(low500L) + std::abs(low500R));
        const float highBand = 0.5f * (std::abs(left - low4kL) + std::abs(right - low4kR));
        loAccum += lowBand;
        hiAccum += highBand;
    }

    const float invSamples = 1.0f / static_cast<float>(numSamples);
    const float meanSide = sideAccum * invSamples;
    const float meanMid = midAccum * invSamples;
    const float rawMonoScore = 1.0f - clamp01(meanSide / std::max(1.0e-8f, meanMid * 0.08f + 1.0e-8f));
    current.monoScore = 0.97f * current.monoScore + 0.03f * rawMonoScore;

    const float frameRms = std::sqrt(sumSq * invSamples + 1.0e-9f);
    smoothedPeak = std::max(framePeak, 0.995f * smoothedPeak);
    smoothedRms = 0.97f * smoothedRms + 0.03f * frameRms;
    const float crestFactor = smoothedPeak / std::max(1.0e-9f, smoothedRms);
    const float rawCompressionScore = 1.0f - clamp01((crestFactor - 1.5f) / (6.0f - 1.5f));
    current.compressionScore = 0.95f * current.compressionScore + 0.05f * rawCompressionScore;

    const float loMean = loAccum * invSamples;
    const float hiMean = hiAccum * invSamples;
    const float tiltRatio = loMean / std::max(1.0e-9f, hiMean);
    const float rawTiltScore = clamp01((tiltRatio - 3.0f) / (8.0f - 3.0f));
    current.tiltScore = 0.95f * current.tiltScore + 0.05f * rawTiltScore;

    current.separationConfidence = 1.0f - clamp01(0.45f * current.monoScore
                                                 + 0.35f * current.compressionScore
                                                 + 0.20f * current.tiltScore);
}

} // namespace vxsuite
