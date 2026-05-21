#pragma once

#include <algorithm>
#include <cmath>

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite::corrective::detail {

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

inline BiquadCoeffs makeHighShelf(const double sr, const float centerHz, const float q, const float gainDb) {
    BiquadCoeffs c {};
    if (sr <= 0.0 || centerHz <= 0.0f || q <= 0.0f)
        return c;

    if (std::abs(gainDb) < 0.01f)
        return c;

    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * juce::MathConstants<float>::pi * centerHz / static_cast<float>(sr);
    const float cw = std::cos(w0);
    const float sw = std::sin(w0);
    const float alpha = sw / std::sqrt(2.0f);
    const float sqrtA = std::sqrt(A);
    const float twoSqrtAAlpha = 2.0f * sqrtA * alpha;
    const float aPlus1 = A + 1.0f;
    const float aMinus1 = A - 1.0f;

    const float b0 =    A * (aPlus1 + aMinus1 * cw + twoSqrtAAlpha);
    const float b1 = -2.0f * A * (aMinus1 + aPlus1 * cw);
    const float b2 =    A * (aPlus1 + aMinus1 * cw - twoSqrtAAlpha);
    const float a0 =        aPlus1 - aMinus1 * cw + twoSqrtAAlpha;
    const float a1 =  2.0f * (aMinus1 - aPlus1 * cw);
    const float a2 =        aPlus1 - aMinus1 * cw - twoSqrtAAlpha;

    const float invA0 = 1.0f / std::max(1.0e-12f, a0);
    c.b0 = b0 * invA0;
    c.b1 = b1 * invA0;
    c.b2 = b2 * invA0;
    c.a1 = a1 * invA0;
    c.a2 = a2 * invA0;
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

// Buffer statistics: RMS and peak with stability checking
struct BufferStats {
    float rms = 0.0f;
    float peak = 0.0f;
    bool stable = true;
};

inline BufferStats computeBufferStats(const juce::AudioBuffer<float>& buffer, const float maxStablePeak = 1.0e30f) {
    const int channels = buffer.getNumChannels();
    const int samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return {};

    double sumSquares = 0.0;
    float peak = 0.0f;
    bool stable = true;
    for (int ch = 0; ch < channels; ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < samples; ++i) {
            const float s = data[i];
            if (!std::isfinite(s)) { stable = false; continue; }
            const float abs = std::abs(s);
            if (abs > maxStablePeak) stable = false;
            peak = std::max(peak, abs);
            sumSquares += static_cast<double>(s) * static_cast<double>(s);
        }
    }
    const int count = channels * samples;
    return { count > 0 ? static_cast<float>(std::sqrt(sumSquares / static_cast<double>(count))) : 0.0f,
             peak, stable };
}

inline float computeBufferRms(const juce::AudioBuffer<float>& buffer) {
    return computeBufferStats(buffer).rms;
}

} // namespace vxsuite::corrective::detail
