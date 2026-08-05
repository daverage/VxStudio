#pragma once

#include <array>
#include <cmath>

namespace vxsuite::tune {

// Krumhansl-Schmuckler key profiles: perceptual salience weights per scale
// degree, from cognitive key-finding research (Krumhansl & Kessler 1982).
// Index 0 = tonic. These are published research constants reproduced
// identically across independent tools (MIT-licensed ifeelvoid/keyfinder,
// GPL-licensed mixxxdj/libkeyfinder, Apache-2.0-licensed libraz/libsonare) -
// not any one project's original expression.
inline constexpr std::array<float, 12> kKsMajorProfile = {
    6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f,
};
inline constexpr std::array<float, 12> kKsMinorProfile = {
    6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f,
};

// Pearson correlation between a 12-bin pitch-class histogram and a key
// profile rooted at `root`. Mean-centred and normalised by both vectors'
// spread, so it is scale-invariant: works directly on an unnormalised,
// decayed histogram with no separate normalisation step, and is bounded to
// [-1, 1] regardless of how much energy has accumulated.
inline float keyProfileCorrelation(const std::array<float, 12>& histogram,
                                    const std::array<float, 12>& profile,
                                    const int root) noexcept {
    float meanH = 0.0f;
    float meanP = 0.0f;
    for (int i = 0; i < 12; ++i) {
        meanH += histogram[static_cast<size_t>(i)];
        meanP += profile[static_cast<size_t>(i)];
    }
    meanH /= 12.0f;
    meanP /= 12.0f;

    float covariance = 0.0f;
    float varianceH = 0.0f;
    float varianceP = 0.0f;
    for (int i = 0; i < 12; ++i) {
        const float h = histogram[static_cast<size_t>(i)] - meanH;
        const float p = profile[static_cast<size_t>((i - root + 12) % 12)] - meanP;
        covariance += h * p;
        varianceH += h * h;
        varianceP += p * p;
    }
    const float denom = std::sqrt(varianceH * varianceP);
    return denom > 1.0e-9f ? covariance / denom : 0.0f;
}

struct KeyProfileMatch {
    int root = 0;
    bool isMajor = true;
    float correlation = 0.0f;
};

// Best-correlated major/minor key across all 24 candidates in one pass -
// replaces the old two-stage "which 7 notes, then major/minor tie-break"
// scoring. That scheme scored relative major/minor as separate rivals
// sharing an identical in/out-of-scale mask, so the real margin was buried
// under a near-zero tie; a graded profile tells them apart directly.
inline KeyProfileMatch findBestKeyProfile(const std::array<float, 12>& histogram) noexcept {
    KeyProfileMatch best;
    float bestScore = -2.0f;
    for (int root = 0; root < 12; ++root) {
        const float majorScore = keyProfileCorrelation(histogram, kKsMajorProfile, root);
        if (majorScore > bestScore) {
            bestScore = majorScore;
            best.root = root;
            best.isMajor = true;
        }
        const float minorScore = keyProfileCorrelation(histogram, kKsMinorProfile, root);
        if (minorScore > bestScore) {
            bestScore = minorScore;
            best.root = root;
            best.isMajor = false;
        }
    }
    best.correlation = bestScore;
    return best;
}

} // namespace vxsuite::tune
