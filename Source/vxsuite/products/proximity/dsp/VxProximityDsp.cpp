#include "VxProximityDsp.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace vxsuite::proximity {

// ── Biquad coefficient builders ───────────────────────────────────────────────
// Audio EQ Cookbook shelf formulas (Zölzer / RBJ).  Q = 1/sqrt(2) for both.
// When gainDb = 0 → A = 1 → coefficients collapse to identity (bypass-safe).

static constexpr float kQ = 0.7071067811865476f; // 1 / sqrt(2)

ProximityDsp::BiquadCoeffs ProximityDsp::makeLowShelf(const double sr, const float fcHz,
                                                       const float gainDb) noexcept {
    const float A    = std::pow(10.f, gainDb / 40.f);
    const float w0   = 2.f * juce::MathConstants<float>::pi * fcHz / static_cast<float>(sr);
    const float cosW = std::cos(w0);
    const float sinW = std::sin(w0);
    const float sqA  = std::sqrt(A);
    const float alph = sinW / (2.f * kQ);

    const float b0 =    A * ((A + 1.f) - (A - 1.f) * cosW + 2.f * sqA * alph);
    const float b1 = 2.f*A * ((A - 1.f) - (A + 1.f) * cosW);
    const float b2 =    A * ((A + 1.f) - (A - 1.f) * cosW - 2.f * sqA * alph);
    const float a0 =        (A + 1.f) + (A - 1.f) * cosW + 2.f * sqA * alph;
    const float a1 =  -2.f * ((A - 1.f) + (A + 1.f) * cosW);
    const float a2 =        (A + 1.f) + (A - 1.f) * cosW - 2.f * sqA * alph;

    const float inv = 1.f / a0;
    return BiquadCoeffs { b0*inv, b1*inv, b2*inv, a1*inv, a2*inv };
}

ProximityDsp::BiquadCoeffs ProximityDsp::makeHighShelf(const double sr, const float fcHz,
                                                        const float gainDb) noexcept {
    const float A    = std::pow(10.f, gainDb / 40.f);
    const float w0   = 2.f * juce::MathConstants<float>::pi * fcHz / static_cast<float>(sr);
    const float cosW = std::cos(w0);
    const float sinW = std::sin(w0);
    const float sqA  = std::sqrt(A);
    const float alph = sinW / (2.f * kQ);

    const float b0 =    A * ((A + 1.f) + (A - 1.f) * cosW + 2.f * sqA * alph);
    const float b1 = -2.f*A * ((A - 1.f) + (A + 1.f) * cosW);
    const float b2 =    A * ((A + 1.f) + (A - 1.f) * cosW - 2.f * sqA * alph);
    const float a0 =        (A + 1.f) - (A - 1.f) * cosW + 2.f * sqA * alph;
    const float a1 =   2.f * ((A - 1.f) - (A + 1.f) * cosW);
    const float a2 =        (A + 1.f) - (A - 1.f) * cosW - 2.f * sqA * alph;

    const float inv = 1.f / a0;
    return BiquadCoeffs { b0*inv, b1*inv, b2*inv, a1*inv, a2*inv };
}

// ── Direct-Form II biquad ──────────────────────────────────────────────────────

void ProximityDsp::applyBiquad(const BiquadCoeffs& c, BiquadState& s,
                                float* data, const int numSamples) noexcept {
    float w1 = s.w1;
    float w2 = s.w2;
    for (int i = 0; i < numSamples; ++i) {
        const float w = data[i] - c.a1 * w1 - c.a2 * w2;
        data[i] = c.b0 * w + c.b1 * w1 + c.b2 * w2;
        w2 = w1;
        w1 = w;
    }
    s.w1 = w1;
    s.w2 = w2;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void ProximityDsp::setChannelCount(const int numChannels) {
    chans.resize(static_cast<size_t>(std::max(1, numChannels)));
}

void ProximityDsp::prepare(const double sampleRate, const int /*maxBlockSize*/) {
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    reset();
}

void ProximityDsp::reset() noexcept {
    for (auto& ch : chans)
        ch = {};
}

// ── processInPlace ────────────────────────────────────────────────────────────
//
// Mode tuning:
//   Vocal   – low shelf 80–200 Hz (closer sweeps fc), +9 dB max (quadratic)
//             high shelf 3500 Hz, +4 dB max
//   General – low shelf 120–300 Hz (closer sweeps fc), +7 dB max (quadratic)
//             high shelf 8000 Hz, +4 dB max
//
// Quadratic gain mapping models the inverse-square proximity effect:
//   gainDb = maxGainDb * closer²

void ProximityDsp::processInPlace(juce::AudioBuffer<float>& buffer,
                                   const int numSamples,
                                   const float closerAmount,
                                   const float airAmount,
                                   const bool isVoice) noexcept {
    if (numSamples <= 0)
        return;

    const int channels = std::min(buffer.getNumChannels(),
                                  static_cast<int>(chans.size()));
    if (channels <= 0)
        return;

    const float closer = juce::jlimit(0.f, 1.f, closerAmount);
    const float air    = juce::jlimit(0.f, 1.f, airAmount);

    // Low-shelf tuning
    const float lowFcMin = isVoice ?  80.f : 120.f;
    const float lowFcMax = isVoice ? 200.f : 300.f;
    const float lowGainMax = isVoice ? 14.f : 12.f;

    const float lowFc    = lowFcMin + (lowFcMax - lowFcMin) * closer;
    const float lowGain  = lowGainMax * closer;

    // High-shelf tuning
    const float highFc   = isVoice ? 3500.f : 8000.f;
    const float highGain = 7.f * air;

    const BiquadCoeffs lowC  = makeLowShelf (sr, lowFc,  lowGain);
    const BiquadCoeffs highC = makeHighShelf(sr, highFc, highGain);

    for (int ch = 0; ch < channels; ++ch) {
        float* data = buffer.getWritePointer(ch);
        applyBiquad(lowC,  chans[static_cast<size_t>(ch)].lowShelf,  data, numSamples);
        applyBiquad(highC, chans[static_cast<size_t>(ch)].highShelf, data, numSamples);
    }
}

} // namespace vxsuite::proximity
