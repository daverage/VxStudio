#pragma once

#include <cmath>

#include <juce_core/juce_core.h>

namespace vxsuite::spectral {

inline float clamp01(const float x) noexcept {
    return juce::jlimit(0.0f, 1.0f, x);
}

inline float wrapPi(const float x) noexcept {
    return std::remainder(x, 2.0f * juce::MathConstants<float>::pi);
}

inline float hzToBark(const float hz) noexcept {
    return 13.0f * std::atan(0.00076f * hz)
         + 3.5f  * std::atan((hz / 7500.0f) * (hz / 7500.0f));
}

inline float tonalnessFromNeighbors(const float centerPower,
                                    const float leftPower,
                                    const float rightPower) noexcept {
    const float tonalRatio = centerPower
                           / std::max(1.0e-12f, 0.5f * (leftPower + rightPower) + 0.15f * centerPower);
    return clamp01((tonalRatio - 1.25f) / 2.8f);
}

template <typename Index>
inline bool isLocalPeak(const Index index,
                        const float center,
                        const float left,
                        const float right) noexcept {
    juce::ignoreUnused(index);
    return center > left && center >= right;
}

} // namespace vxsuite::spectral
