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

static float onePoleCoeff(const double sr, const float hz) noexcept {
    return std::exp(-2.0f * juce::MathConstants<float>::pi * hz / static_cast<float>(sr));
}

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
    smoothedModel = {};
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
    const float shapedCloser = std::pow(closer, isVoice ? 0.78f : 0.70f);
    const float shapedAir    = std::pow(air, isVoice ? 0.74f : 0.66f);
    const float modelDepth   = juce::jlimit(0.0f, 1.0f,
        shapedCloser * (isVoice ? (0.90f + 0.10f * focus) : 1.0f));

    const float lowCoeff = onePoleCoeff(sr, isVoice ? 180.0f : 200.0f);
    const float lowMidCoeff = onePoleCoeff(sr, isVoice ? 520.0f : 600.0f);
    const float presenceCoeff = onePoleCoeff(sr, isVoice ? 2400.0f : 2800.0f);
    const float airCoeff = onePoleCoeff(sr, isVoice ? 6800.0f : 7600.0f);

    float lowEnergy = 0.0f;
    float lowMidEnergy = 0.0f;
    float presenceEnergy = 0.0f;
    float airEnergy = 0.0f;
    int energyCount = 0;
    for (int ch = 0; ch < channels; ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        auto& state = chans[static_cast<size_t>(ch)];
        float lowSense = state.analysisLow;
        float lowMidSense = state.analysisLowMid;
        float presenceSense = state.analysisPresence;
        float airSense = state.analysisAir;
        for (int i = 0; i < numSamples; ++i) {
            const float x = data[i];
            lowSense = lowCoeff * lowSense + (1.0f - lowCoeff) * x;
            lowMidSense = lowMidCoeff * lowMidSense + (1.0f - lowMidCoeff) * x;
            presenceSense = presenceCoeff * presenceSense + (1.0f - presenceCoeff) * x;
            airSense = airCoeff * airSense + (1.0f - airCoeff) * x;

            const float lowBand = lowSense;
            const float lowMidBand = lowMidSense - lowSense;
            const float presenceBand = presenceSense - lowMidSense;
            const float airBand = x - airSense;

            lowEnergy += std::abs(lowBand);
            lowMidEnergy += std::abs(lowMidBand);
            presenceEnergy += std::abs(presenceBand);
            airEnergy += std::abs(airBand);
            ++energyCount;
        }
        state.analysisLow = lowSense;
        state.analysisLowMid = lowMidSense;
        state.analysisPresence = presenceSense;
        state.analysisAir = airSense;
    }

    const float invEnergyCount = 1.0f / static_cast<float>(std::max(1, energyCount));
    lowEnergy *= invEnergyCount;
    lowMidEnergy *= invEnergyCount;
    presenceEnergy *= invEnergyCount;
    airEnergy *= invEnergyCount;
    const float totalEnergy = std::max(1.0e-6f, lowEnergy + lowMidEnergy + presenceEnergy + airEnergy);
    const float lowShare = lowEnergy / totalEnergy;
    const float lowMidShare = lowMidEnergy / totalEnergy;
    const float presenceShare = presenceEnergy / totalEnergy;
    const float airShare = airEnergy / totalEnergy;
    const float bodyOpportunity = juce::jlimit(0.0f, 1.0f, (lowShare - 0.55f * lowMidShare + 0.08f) / 0.34f);
    const float mudRisk = juce::jlimit(0.0f, 1.0f, (lowMidShare - 0.38f * lowShare) / 0.24f);
    const float directnessOpportunity = juce::jlimit(0.0f, 1.0f, (presenceShare + 0.35f * airShare) / 0.34f);

    // 1) Proximity low shelf: cover the audible body/warmth region, not just sub rumble.
    const float lowFcMin = isVoice ? 120.f : 140.f;
    const float lowFcMax = isVoice ? 200.f : 260.f;
    const float lowGainMax = isVoice ? 7.5f : 6.5f;
    const float lowFc    = lowFcMin + (lowFcMax - lowFcMin) * modelDepth;
    const float lowGain  = lowGainMax * modelDepth
        * juce::jlimit(0.72f, 1.12f, 0.78f + 0.34f * bodyOpportunity - 0.12f * mudRisk);

    // 2) Mud compensation: directional close mics are usually designed to stay
    // usable, not simply boomy. This cut reins in the 220-350 Hz clutter zone.
    const float mudCutCenter = isVoice
        ? juce::jlimit(220.0f, 320.0f, 255.0f + 22.0f * (1.0f - focus))
        : 300.0f;
    const float mudCutQ = isVoice ? 0.82f : 0.75f;
    const float mudCutDb = -modelDepth
        * (isVoice ? (3.2f + 0.8f * (1.0f - focus)) : 2.6f)
        * juce::jlimit(0.72f, 1.30f, 0.82f + 0.42f * mudRisk);

    // 3) Presence contour: closer placement also reads as more direct and more
    // articulate, not just bassier. This is intentionally moderate.
    const float presenceCenter = isVoice
        ? juce::jlimit(2800.0f, 4300.0f, 3600.0f + 450.0f * focus)
        : 3200.0f;
    const float presenceQ = isVoice ? 0.78f : 0.70f;
    const float presenceDb = juce::jlimit(0.0f, isVoice ? 7.2f : 5.8f,
        modelDepth * (isVoice ? (2.7f + 1.8f * focus) : 2.2f)
            * juce::jlimit(0.84f, 1.20f, 0.90f + 0.30f * directnessOpportunity)
      + shapedAir * (isVoice ? 2.0f : 1.4f));

    // 4) Capsule/open-top air.
    const float highFc   = isVoice ? 7600.f : 11000.f;
    const float highGain = (isVoice ? 7.5f : 9.0f) * shapedAir
        * juce::jlimit(0.82f, 1.12f, 0.88f + 0.24f * directnessOpportunity - 0.10f * mudRisk);

    const auto convertCoeffs = [](const dspcommon::BiquadCoeffs& c) noexcept {
        return BiquadCoeffs { c.b0, c.b1, c.b2, c.a1, c.a2 };
    };

    const float outputTrimDb = -juce::jlimit(0.0f, 2.6f,
        0.18f * lowGain * (0.70f + 0.70f * lowShare)
      + 0.14f * std::abs(mudCutDb) * (0.30f + 0.60f * lowMidShare)
      + 0.13f * presenceDb * (0.45f + 0.75f * presenceShare)
      + 0.08f * highGain * (0.40f + 0.80f * airShare));

    auto smoothModelValue = [](float& current, const float target, const bool primed) noexcept {
        if (!primed) {
            current = target;
            return current;
        }
        current += 0.18f * (target - current);
        return current;
    };

    const bool primed = smoothedModel.primed;
    const float smoothLowFc = smoothModelValue(smoothedModel.lowFcHz, lowFc, primed);
    const float smoothLowGain = smoothModelValue(smoothedModel.lowGainDb, lowGain, primed);
    const float smoothMudCenter = smoothModelValue(smoothedModel.mudCutCenterHz, mudCutCenter, primed);
    const float smoothMudCut = smoothModelValue(smoothedModel.mudCutDb, mudCutDb, primed);
    const float smoothPresenceCenter = smoothModelValue(smoothedModel.presenceCenterHz, presenceCenter, primed);
    const float smoothPresence = smoothModelValue(smoothedModel.presenceDb, presenceDb, primed);
    const float smoothHighFc = smoothModelValue(smoothedModel.highFcHz, highFc, primed);
    const float smoothHighGain = smoothModelValue(smoothedModel.highGainDb, highGain, primed);
    const float smoothTrimDb = smoothModelValue(smoothedModel.outputTrimDb, outputTrimDb, primed);
    smoothedModel.primed = true;

    const BiquadCoeffs lowC  = makeLowShelf(sr, smoothLowFc, smoothLowGain);
    const BiquadCoeffs mudC = convertCoeffs(dspcommon::makePeakingEq(sr, smoothMudCenter, mudCutQ, smoothMudCut));
    const BiquadCoeffs presenceC = convertCoeffs(dspcommon::makePeakingEq(sr, smoothPresenceCenter, presenceQ, smoothPresence));
    const BiquadCoeffs highC = makeHighShelf(sr, smoothHighFc, smoothHighGain);
    const float outputTrim = juce::Decibels::decibelsToGain(smoothTrimDb);

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
