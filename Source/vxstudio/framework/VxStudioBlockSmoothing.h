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

// Reshapes a normalised [0,1] control so early knob travel produces more
// effect than a straight-line mapping would (exponent < 1 = concave/
// front-loaded, > 1 = back-loaded). 0 and 1 always map to themselves.
// Use where a linear knob-to-gain mapping reads as "you have to max it out
// to hear anything" - human perception of a control's usefulness isn't the
// same as a straight dB-per-unit ramp being perceptually linear.
inline float frontLoadedControl(const float normalised, const float exponent = 0.65f) noexcept {
    return std::pow(clamp01(normalised), exponent);
}

} // namespace vxsuite
