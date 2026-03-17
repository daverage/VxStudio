#pragma once

#include <cmath>

#include <juce_core/juce_core.h>

namespace vxsuite {

inline float blockBlendAlpha(const double sampleRate, const int numSamples, const float timeSeconds) noexcept {
    if (sampleRate <= 1000.0 || numSamples <= 0 || timeSeconds <= 0.0f)
        return 1.0f;
    return 1.0f - std::exp(-static_cast<float>(numSamples) / (timeSeconds * static_cast<float>(sampleRate)));
}

inline float smoothBlockValue(const float current,
                              const float target,
                              const double sampleRate,
                              const int numSamples,
                              const float timeSeconds) noexcept {
    return current + blockBlendAlpha(sampleRate, numSamples, timeSeconds) * (target - current);
}

inline float smoothBlockToward(const float current,
                               const float target,
                               const double sampleRate,
                               const int numSamples,
                               const float attackSeconds,
                               const float releaseSeconds) noexcept {
    const float alpha = target > current
        ? blockBlendAlpha(sampleRate, numSamples, attackSeconds)
        : blockBlendAlpha(sampleRate, numSamples, releaseSeconds);
    return current + alpha * (target - current);
}

inline float clamp01(const float value) noexcept {
    return juce::jlimit(0.0f, 1.0f, value);
}

} // namespace vxsuite
