#pragma once

#include <algorithm>
#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite::polish::detail {

inline float onePoleCoeff(const double sr, const float hz) {
    if (sr <= 0.0 || hz <= 0.0f)
        return 0.0f;
    const float a = std::exp(-2.0f * juce::MathConstants<float>::pi * hz / static_cast<float>(sr));
    return juce::jlimit(0.0f, 0.99999f, a);
}

struct BiquadCoeffs {
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
};

inline BiquadCoeffs makeBandpass(const double sr, const float centerHz, const float q) {
    BiquadCoeffs c {};
    if (sr <= 0.0 || centerHz <= 0.0f || q <= 0.0f)
        return c;
    const float w0 = 2.0f * juce::MathConstants<float>::pi * centerHz / static_cast<float>(sr);
    const float cw = std::cos(w0);
    const float sw = std::sin(w0);
    const float alpha = sw / (2.0f * q);
    const float a0 = 1.0f + alpha;
    const float invA0 = 1.0f / std::max(1.0e-12f, a0);
    c.b0 = alpha * invA0;
    c.b1 = 0.0f;
    c.b2 = -alpha * invA0;
    c.a1 = (-2.0f * cw) * invA0;
    c.a2 = (1.0f - alpha) * invA0;
    return c;
}

inline BiquadCoeffs makePeakingEq(const double sr, const float centerHz, const float q, const float gainDb) {
    BiquadCoeffs c {};
    if (sr <= 0.0 || centerHz <= 0.0f || q <= 0.0f)
        return c;
    const float a = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * juce::MathConstants<float>::pi * centerHz / static_cast<float>(sr);
    const float cw = std::cos(w0);
    const float sw = std::sin(w0);
    const float alpha = sw / (2.0f * q);
    const float a0 = 1.0f + alpha / a;
    const float invA0 = 1.0f / std::max(1.0e-12f, a0);
    c.b0 = (1.0f + alpha * a) * invA0;
    c.b1 = (-2.0f * cw) * invA0;
    c.b2 = (1.0f - alpha * a) * invA0;
    c.a1 = (-2.0f * cw) * invA0;
    c.a2 = (1.0f - alpha / a) * invA0;
    return c;
}

inline float processBiquadDf2(const float x, const BiquadCoeffs& c, float& z1, float& z2) {
    const float y = c.b0 * x + z1;
    z1 = c.b1 * x - c.a1 * y + z2;
    z2 = c.b2 * x - c.a2 * y;
    return y;
}

inline float processBiquadDf2(const float x, const float b0, const float b1, const float b2,
                              const float a1, const float a2, float& z1, float& z2) {
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
}

} // namespace vxsuite::polish::detail
