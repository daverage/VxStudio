#include "VxProximityDsp.h"
#include "../../../framework/VxStudioDspCommon.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace vxsuite::proximity {

namespace dspcommon = vxsuite::corrective::detail;

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
//   Model a plausible close directional mic transfer rather than a raw bass
//   shelf:
//   1. proximity low shelf in true body bands
//   2. compensating low-mid cut to avoid boom/mud
//   3. presence contour for articulation / directness
//   4. separate air shelf for capsule openness
// This stays lightweight and realtime-safe, but behaves more like a voiced mic
// model than a one-shelf EQ gimmick.

void ProximityDsp::processInPlace(juce::AudioBuffer<float>& buffer,
                                   const int numSamples,
                                   const float closerAmount,
                                   const float airAmount,
                                   const bool isVoice,
                                   const float vocalFocus) noexcept {
    if (numSamples <= 0)
        return;

    const int channels = std::min(buffer.getNumChannels(),
                                  static_cast<int>(chans.size()));
    if (channels <= 0)
        return;

    const float closer = juce::jlimit(0.f, 1.f, closerAmount);
    const float air    = juce::jlimit(0.f, 1.f, airAmount);
    const float focus  = juce::jlimit(0.0f, 1.0f, vocalFocus);
    const float shapedCloser = std::pow(closer, isVoice ? 1.10f : 1.02f);
    const float shapedAir    = std::pow(air, isVoice ? 0.90f : 0.82f);
    const float modelDepth   = juce::jlimit(0.0f, 1.0f,
        shapedCloser * (isVoice ? (0.90f + 0.10f * focus) : 1.0f));

    // 1) Proximity low shelf: keep this in the genuine body/proximity region.
    const float lowFcMin = isVoice ?  85.f : 95.f;
    const float lowFcMax = isVoice ? 135.f : 180.f;
    const float lowGainMax = isVoice ? 6.5f : 5.8f;
    const float lowFc    = lowFcMin + (lowFcMax - lowFcMin) * modelDepth;
    const float lowGain  = lowGainMax * modelDepth;

    // 2) Mud compensation: directional close mics are usually designed to stay
    // usable, not simply boomy. This cut reins in the 220-350 Hz clutter zone.
    const float mudCutCenter = isVoice
        ? juce::jlimit(220.0f, 320.0f, 255.0f + 22.0f * (1.0f - focus))
        : 300.0f;
    const float mudCutQ = isVoice ? 0.82f : 0.75f;
    const float mudCutDb = -modelDepth * (isVoice ? (2.8f + 1.0f * (1.0f - focus)) : 2.2f);

    // 3) Presence contour: closer placement also reads as more direct and more
    // articulate, not just bassier. This is intentionally moderate.
    const float presenceCenter = isVoice
        ? juce::jlimit(2800.0f, 4300.0f, 3600.0f + 450.0f * focus)
        : 3200.0f;
    const float presenceQ = isVoice ? 0.78f : 0.70f;
    const float presenceDb = juce::jlimit(0.0f, isVoice ? 4.2f : 3.2f,
        modelDepth * (isVoice ? (1.5f + 1.2f * focus) : 1.4f)
      + shapedAir * (isVoice ? 1.2f : 0.8f));

    // 4) Capsule/open-top air.
    const float highFc   = isVoice ? 7600.f : 11000.f;
    const float highGain = (isVoice ? 5.0f : 6.5f) * shapedAir;

    const auto convertCoeffs = [](const dspcommon::BiquadCoeffs& c) noexcept {
        return BiquadCoeffs { c.b0, c.b1, c.b2, c.a1, c.a2 };
    };

    const BiquadCoeffs lowC  = makeLowShelf (sr, lowFc,  lowGain);
    const BiquadCoeffs mudC = convertCoeffs(dspcommon::makePeakingEq(sr, mudCutCenter, mudCutQ, mudCutDb));
    const BiquadCoeffs presenceC = convertCoeffs(dspcommon::makePeakingEq(sr, presenceCenter, presenceQ, presenceDb));
    const BiquadCoeffs highC = makeHighShelf(sr, highFc, highGain);
    const float outputTrimDb = -juce::jlimit(0.0f, 3.0f,
        0.22f * lowGain + 0.30f * presenceDb + 0.15f * highGain);
    const float outputTrim = juce::Decibels::decibelsToGain(outputTrimDb);

    for (int ch = 0; ch < channels; ++ch) {
        float* data = buffer.getWritePointer(ch);
        auto& state = chans[static_cast<size_t>(ch)];
        applyBiquad(lowC,  state.lowShelf,  data, numSamples);
        applyBiquad(mudC, state.mudBell, data, numSamples);
        applyBiquad(presenceC, state.presenceBell, data, numSamples);
        applyBiquad(highC, state.highShelf, data, numSamples);
        if (std::abs(outputTrim - 1.0f) > 1.0e-4f)
            juce::FloatVectorOperations::multiply(data, outputTrim, numSamples);
    }
}

} // namespace vxsuite::proximity
