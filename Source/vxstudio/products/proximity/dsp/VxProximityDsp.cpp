#include "VxProximityDsp.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace vxsuite::proximity {

// ── Biquad coefficient builders ───────────────────────────────────────────────
// Audio EQ Cookbook shelf formulas (Zölzer / RBJ).  Q = 1/sqrt(2) for both.
// When gainDb = 0 → A = 1 → coefficients collapse to identity (bypass-safe).

static constexpr float kQ = 0.7071067811865476f; // 1 / sqrt(2)
static constexpr float kSpeedOfSound = 343.0f;  // m/s at 20°C

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

// ── Physics-based proximity model ──────────────────────────────────────────────
//
// Proximity effect: distance-dependent bass rise in directional microphones
//
// Physics:
//   fc = v_sound / (2π × distance_meters)  [first-order proximity frequency]
//   gain_dB = 20 × log10(d_ref / d) × pattern_factor
//
// Where:
//   d_ref = 0.50 m (reference / arm's length)
//   d_near = 0.02 m (minimum / on capsule)
//   pattern_factor = 0.50 (cardioid vocal), 0.80 (supercardioid general)
//
// The 'closer' parameter [0, 1] maps logarithmically to distance:
//   d = d_ref × exp(-closer × ln(d_ref/d_near))

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
    (void)vocalFocus; // Unused in physics model

    // Physics constants for distance mapping
    const float d_ref = 0.50f;   // 50 cm reference
    const float d_near = 0.02f;  // 2 cm minimum (on capsule)
    const float ln_ratio = std::log(d_ref / d_near);
    const float distance = d_ref * std::exp(-closer * ln_ratio);
    const float patternFactor = isVoice ? 0.50f : 0.80f;

    // Distance-derived proximity parameters
    const float proximityFcHz = juce::jlimit(80.0f, 3000.0f,
        kSpeedOfSound / (2.0f * juce::MathConstants<float>::pi * distance));
    const float proximityGainDb = juce::jlimit(0.0f, isVoice ? 16.0f : 18.0f,
        20.0f * std::log10(d_ref / distance) * patternFactor);

    // Mud compensation: proportional bell cut to prevent excessive mud
    const float mudCutCenter = proximityFcHz * (isVoice ? 2.0f : 2.2f);
    const float mudCutDb = -0.45f * proximityGainDb;

    // Air shelf: user-controlled brightness at high frequency
    const float highFc = isVoice ? 7600.f : 11000.f;
    const float highGain = (isVoice ? 7.5f : 9.0f) * air;

    // Content analysis for adaptive gain scaling
    const float lowCoeff = onePoleCoeff(sr, isVoice ? 180.0f : 200.0f);
    const float midCoeff = onePoleCoeff(sr, isVoice ? 1000.0f : 1200.0f);
    const float airCoeff = onePoleCoeff(sr, isVoice ? 6800.0f : 7600.0f);

    float lowEnergy = 0.0f;
    float midEnergy = 0.0f;
    float airEnergy = 0.0f;
    int energyCount = 0;

    for (int ch = 0; ch < channels; ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        auto& state = chans[static_cast<size_t>(ch)];
        float lowSense = state.analysisLow;
        float midSense = state.analysisMid;
        float airSense = state.analysisAir;

        for (int i = 0; i < numSamples; ++i) {
            const float x = data[i];
            lowSense = lowCoeff * lowSense + (1.0f - lowCoeff) * x;
            midSense = midCoeff * midSense + (1.0f - midCoeff) * x;
            airSense = airCoeff * airSense + (1.0f - airCoeff) * x;

            lowEnergy += std::abs(lowSense);
            midEnergy += std::abs(midSense - lowSense);
            airEnergy += std::abs(x - airSense);
            ++energyCount;
        }
        state.analysisLow = lowSense;
        state.analysisMid = midSense;
        state.analysisAir = airSense;
    }

    const float invEnergyCount = 1.0f / static_cast<float>(std::max(1, energyCount));
    lowEnergy *= invEnergyCount;
    midEnergy *= invEnergyCount;
    airEnergy *= invEnergyCount;
    const float totalEnergy = std::max(1.0e-6f, lowEnergy + midEnergy + airEnergy);
    const float lowShare = lowEnergy / totalEnergy;
    const float midShare = midEnergy / totalEnergy;

    // Adaptive proximity gain scaling based on content
    const float mudRisk = juce::jlimit(0.0f, 1.0f, midShare / 0.35f);
    const float adaptiveProximityGain = proximityGainDb *
        juce::jlimit(0.85f, 1.08f, 0.90f + 0.12f * lowShare - 0.10f * mudRisk);

    // Output trim for stable loudness
    const float outputTrimDb = -juce::jlimit(0.0f, 4.0f,
        0.18f * adaptiveProximityGain * (0.68f + 0.32f * lowShare)
      + 0.08f * std::abs(mudCutDb) * (0.45f + 0.55f * midShare)
      + 0.10f * highGain * (0.50f + 0.50f * airEnergy));

    // Smooth filter parameters frame-to-frame
    auto smoothModelValue = [](float& current, const float target, const bool primed) noexcept {
        if (!primed) {
            current = target;
            return current;
        }
        current += 0.18f * (target - current);
        return current;
    };

    const bool primed = smoothedModel.primed;
    const float smoothProximityFc = smoothModelValue(smoothedModel.proximityFcHz, proximityFcHz, primed);
    const float smoothProximityGain = smoothModelValue(smoothedModel.proximityGainDb, adaptiveProximityGain, primed);
    const float smoothMudCenter = smoothModelValue(smoothedModel.mudCenterHz, mudCutCenter, primed);
    const float smoothMudGain = smoothModelValue(smoothedModel.mudGainDb, mudCutDb, primed);
    const float smoothHighGain = smoothModelValue(smoothedModel.airGainDb, highGain, primed);
    const float smoothTrimDb = smoothModelValue(smoothedModel.outputTrimDb, outputTrimDb, primed);
    smoothedModel.primed = true;

    // Build filter coefficients
    const BiquadCoeffs proximityC = makeLowShelf(sr, smoothProximityFc, smoothProximityGain);
    const BiquadCoeffs mudC = makeLowShelf(sr, smoothMudCenter, smoothMudGain);
    const BiquadCoeffs airC = makeHighShelf(sr, highFc, smoothHighGain);
    const float outputTrim = juce::Decibels::decibelsToGain(smoothTrimDb);

    // Apply filters and trim per channel
    for (int ch = 0; ch < channels; ++ch) {
        float* data = buffer.getWritePointer(ch);
        auto& state = chans[static_cast<size_t>(ch)];
        applyBiquad(proximityC, state.proximityShelf, data, numSamples);
        applyBiquad(mudC,       state.mudBell,       data, numSamples);
        applyBiquad(airC,       state.airShelf,      data, numSamples);
        if (std::abs(outputTrim - 1.0f) > 1.0e-4f)
            juce::FloatVectorOperations::multiply(data, outputTrim, numSamples);
    }
}

} // namespace vxsuite::proximity
