#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <juce_core/juce_core.h>

namespace vxsuite::spectral {

struct MinStatsBin {
    float smoothPow = 1.0e-12f;
    float subWinMin = 1.0e-12f;
    float globalMin = 1.0e-12f;
    int frameCount = 0;
    int subWinIdx = 0;
    std::vector<float> subWindows;
};

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

inline void prepareSqrtHannWindow(std::vector<float>& window, const int fftSize) {
    window.assign(static_cast<size_t>(fftSize), 0.0f);
    for (int i = 0; i < fftSize; ++i) {
        window[static_cast<size_t>(i)] = std::sqrt(0.5f - 0.5f * std::cos(
            2.0f * juce::MathConstants<float>::pi
            * static_cast<float>(i) / static_cast<float>(fftSize)));
    }
}

template <typename Index, size_t BarkBands>
inline void prepareBarkScaleLayout(const double sampleRate,
                                   const int fftSize,
                                   std::vector<int>& binToBark,
                                   std::vector<float>& phaseAdvance,
                                   std::array<std::vector<Index>, BarkBands>& barkBins) {
    const int bins = fftSize / 2 + 1;
    const float binHz = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
    binToBark.resize(static_cast<size_t>(bins));
    phaseAdvance.resize(static_cast<size_t>(bins));
    for (auto& barkBand : barkBins)
        barkBand.clear();

    for (int k = 0; k < bins; ++k) {
        const float hz = static_cast<float>(k) * binHz;
        const int bark = juce::jlimit(0, static_cast<int>(BarkBands) - 1,
                                      static_cast<int>(std::floor(hzToBark(hz))));
        binToBark[static_cast<size_t>(k)] = bark;
        barkBins[static_cast<size_t>(bark)].push_back(static_cast<Index>(k));
        phaseAdvance[static_cast<size_t>(k)] =
            2.0f * juce::MathConstants<float>::pi
            * static_cast<float>(fftSize / 4) * static_cast<float>(k)
            / static_cast<float>(fftSize);
    }
}

inline void prepareMinStats(std::vector<MinStatsBin>& state,
                            const size_t numBins,
                            const int subWindowCount,
                            const float eps) {
    state.resize(numBins);
    for (auto& bin : state) {
        bin.smoothPow = eps;
        bin.subWinMin = eps;
        bin.globalMin = eps;
        bin.frameCount = 0;
        bin.subWinIdx = 0;
        bin.subWindows.assign(static_cast<size_t>(std::max(1, subWindowCount)), eps);
    }
}

inline void resetMinStats(std::vector<MinStatsBin>& state, const float eps) {
    for (auto& bin : state) {
        bin.smoothPow = eps;
        bin.subWinMin = eps;
        bin.globalMin = eps;
        bin.frameCount = 0;
        bin.subWinIdx = 0;
        std::fill(bin.subWindows.begin(), bin.subWindows.end(), eps);
    }
}

inline float updateMinStats(MinStatsBin& state,
                            const float power,
                            const float presence,
                            const int subWindowLength,
                            const int subWindowCount,
                            const float alphaNoise,
                            const float bias,
                            const float eps) noexcept {
    const float alpha = presence > 0.55f ? 0.96f : alphaNoise;
    state.smoothPow = alpha * state.smoothPow + (1.0f - alpha) * power;

    if (!state.subWindows.empty()
        && state.subWindows.front() <= 1.0e-7f
        && state.smoothPow > 1.0e-7f) {
        std::fill(state.subWindows.begin(), state.subWindows.end(), state.smoothPow);
        state.globalMin = state.smoothPow;
        state.subWinMin = state.smoothPow;
        return bias * state.smoothPow;
    }

    state.subWinMin = std::min(state.subWinMin, state.smoothPow);
    if (++state.frameCount >= subWindowLength) {
        state.subWindows[static_cast<size_t>(state.subWinIdx)] = state.subWinMin;
        state.subWinIdx = (state.subWinIdx + 1) % std::max(1, subWindowCount);
        state.subWinMin = state.smoothPow;
        state.frameCount = 0;
        state.globalMin = *std::min_element(state.subWindows.begin(), state.subWindows.end());
    }

    return bias * std::max(eps, state.globalMin);
}

} // namespace vxsuite::spectral
