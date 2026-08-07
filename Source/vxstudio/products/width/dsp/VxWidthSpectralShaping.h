#pragma once

#include <juce_core/juce_core.h>
#include <cmath>

// Small shared one-pole building blocks for Focus (§10) and low-frequency
// protection (§10.3). Deliberately time-domain, not STFT/multiband - §10.1
// explicitly allows "minimum-phase crossovers" and "spectral weighting
// curves" without requiring a full analysis-band system for this build step.
namespace vxsuite::width {

// Broad low/high tilt around a fixed cutoff - used to steer where the
// Focus-shaped signal concentrates spectral energy (§10: "Focus controls
// where in the spectrum the spatial effect is concentrated" - shapes the
// WET/generated content's source, not the dry signal).
class OnePoleTilt {
public:
    void prepare(const double sampleRate, const float cutoffHz) noexcept {
        const float srSafe = static_cast<float>(sampleRate > 1000.0 ? sampleRate : 48000.0);
        alpha = std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz / srSafe);
        reset();
    }
    void reset() noexcept { lowpassState = 0.0f; }

    // tiltDb: negative emphasises low/body, positive emphasises high/air.
    //
    // Splits the tilt symmetrically across both bands (+tiltDb/2 on one,
    // -tiltDb/2 on the other) rather than only ever touching the high band.
    // The original one-sided-shelf version (`x + highPart*(gain(tiltDb)-1)`)
    // left the low band's gain fixed at 1 always, so negative tilt (Body)
    // was a pure CUT with no compensating boost while positive tilt (Air)
    // was a pure BOOST - not an energy-neutral tilt at all. That directly
    // fed Focus's downstream consumers (the decorrelator + ADT voices, both
    // driven off this tilted signal): measured total generated Side energy
    // swung ~9x from Focus=0 to Focus=100 at a FIXED Width, so turning Focus
    // alone visibly changed how wide the image read even with Width
    // untouched (user report, 2026-08-07 - "the width lines seem to be
    // linked to focus"). Symmetric split keeps broadband RMS roughly
    // constant across the full tilt range, so Focus repositions the effect
    // instead of scaling it.
    float process(const float x, const float tiltDb) noexcept {
        lowpassState += (x - lowpassState) * (1.0f - alpha);
        const float lowPart = lowpassState;
        const float highPart = x - lowpassState;
        const float highGain = juce::Decibels::decibelsToGain(tiltDb * 0.5f);
        const float lowGain  = juce::Decibels::decibelsToGain(-tiltDb * 0.5f);
        return lowPart * lowGain + highPart * highGain;
    }

private:
    float alpha = 0.99f;
    float lowpassState = 0.0f;
};

// Fixed-cutoff highpass (x - lowpass(x)) - always-on sub-bass protection for
// GENERATED content only (§10.3: "strongly restrain generated Side in
// sub-bass", independent of the Focus setting).
class OnePoleHighpass {
public:
    void prepare(const double sampleRate, const float cutoffHz) noexcept {
        const float srSafe = static_cast<float>(sampleRate > 1000.0 ? sampleRate : 48000.0);
        alpha = std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz / srSafe);
        reset();
    }
    void reset() noexcept { lowpassState = 0.0f; }

    float process(const float x) noexcept {
        lowpassState += (x - lowpassState) * (1.0f - alpha);
        return x - lowpassState;
    }

private:
    float alpha = 0.999f;
    float lowpassState = 0.0f;
};

} // namespace vxsuite::width
