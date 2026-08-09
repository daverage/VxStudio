#pragma once

#include <juce_core/juce_core.h>

#include "VxWidthMicroPitch.h"

#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

// ADT doubling engine, one generated voice (VXWIDTH_BUILD.md §8.2-8.7).
// Two of these (Voice A, Voice B) are combined by VxWidthProcessor into
// DoubleMid/DoubleSide. Deliberately NOT implementing §8.8's harmonic/
// residual/transient decomposition here - that's a later build-order step;
// this voice uses a single block-rate transient-risk value (already
// computed by the framework's VoiceAnalysisSnapshot) for §8.2's
// "transient-dependent processing reduction" instead of the fuller 3-way
// split the spec describes as a later differentiator.
namespace vxsuite::width {

// Band-limited stochastic trajectory (§8.4): NOT a sine LFO. Holds a random
// target for a randomised interval, then glides toward it with one-pole
// smoothing - continuous, bounded, non-periodic. Deterministic per-instance
// seed (§17: no shared global RNG, no runtime-random-per-block reseeding).
class StochasticWalker {
public:
    void prepare(const double sampleRate, const std::uint32_t seed,
                 const float minIntervalSeconds, const float maxIntervalSeconds,
                 const float smoothingSeconds) noexcept {
        sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
        rng.seed(seed);
        minIntervalSamples = std::max(1, static_cast<int>(minIntervalSeconds * static_cast<float>(sr)));
        maxIntervalSamples = std::max(minIntervalSamples, static_cast<int>(maxIntervalSeconds * static_cast<float>(sr)));
        smoothingAlpha = std::exp(-1.0f / std::max(1.0f, smoothingSeconds * static_cast<float>(sr)));
        reset();
    }

    void reset() noexcept {
        value = 0.0f;
        target = 0.0f;
        samplesUntilRetarget = 0;
    }

    // Returns a continuous value in [-1, 1].
    float process() noexcept {
        if (--samplesUntilRetarget <= 0) {
            target = dist(rng);
            std::uniform_int_distribution<int> intervalDist(minIntervalSamples, maxIntervalSamples);
            samplesUntilRetarget = intervalDist(rng);
        }
        value += (target - value) * (1.0f - smoothingAlpha);
        return value;
    }

private:
    double sr = 48000.0;
    std::mt19937 rng;
    std::uniform_real_distribution<float> dist { -1.0f, 1.0f };
    int minIntervalSamples = 4800;
    int maxIntervalSamples = 19200;
    float smoothingAlpha = 0.999f;
    float value = 0.0f;
    float target = 0.0f;
    int samplesUntilRetarget = 0;
};

// Causal fractional delay line, 4-point Catmull-Rom interpolation (§8.3).
// Sized once in prepare(); no allocation after.
class FractionalDelayLine {
public:
    void prepare(const double sampleRate, const float maxDelayMs) {
        const float srSafe = static_cast<float>(sampleRate > 1000.0 ? sampleRate : 48000.0);
        const int size = std::max(16, static_cast<int>(maxDelayMs * 0.001f * srSafe) + 8);
        ring.assign(static_cast<size_t>(size), 0.0f);
        writePos = 0;
    }

    void reset() noexcept {
        std::fill(ring.begin(), ring.end(), 0.0f);
        writePos = 0;
    }

    float process(const float x, const float delaySamples) noexcept {
        const int size = static_cast<int>(ring.size());
        ring[static_cast<size_t>(writePos)] = x;

        float pos = static_cast<float>(writePos) - delaySamples;
        while (pos < 0.0f)
            pos += static_cast<float>(size);
        const int i1 = static_cast<int>(pos) % size;
        const float frac = pos - std::floor(pos);
        const int i0 = (i1 - 1 + size) % size;
        const int i2 = (i1 + 1) % size;
        const int i3 = (i1 + 2) % size;

        const float y0 = ring[static_cast<size_t>(i0)];
        const float y1 = ring[static_cast<size_t>(i1)];
        const float y2 = ring[static_cast<size_t>(i2)];
        const float y3 = ring[static_cast<size_t>(i3)];
        const float t = frac, t2 = t * t, t3 = t2 * t;
        const float result = 0.5f * ((2.0f * y1) + (-y0 + y2) * t
            + (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3) * t2
            + (-y0 + 3.0f * y1 - 3.0f * y2 + y3) * t3);

        writePos = (writePos + 1) % size;
        return result;
    }

private:
    std::vector<float> ring;
    int writePos = 0;
};

// One generated voice: fractional delay + stochastic delay/gain/tilt
// movement + transient-dependent level reduction (§8.2).
//
// The delay walker's own one-pole smoothing bounds neither the delay's rate
// of change nor the resulting Doppler pitch shift (changing delay
// continuously IS a pitch shift: rate-of-change of delay, as a fraction of
// realtime, equals the frequency ratio the ear hears minus one). A
// full-range walker retarget converging over its ~0.35-0.5s smoothing time
// can therefore swing tens of ms of delay fast enough to produce well over
// 100 cents of instantaneous pitch shift - audible warble, not subtle ADT.
// `currentDelayMs` is slewed toward the walker's output at an explicit max
// rate (ms/s) derived from a max-Doppler-cents budget per tightness band,
// so delay RANGE (walker target) and delay VELOCITY (Doppler) are governed
// separately as they should be.
class AdtVoice {
public:
    // Broad low/high split cutoff for the §8.6 tilt - matches the ~700Hz
    // crossover OnePoleTilt uses elsewhere in this product (VxWidthSpectralShaping.h).
    static constexpr float kTiltCutoffHz = 700.0f;
    // §8.3 Loose-band ceiling: centerMs(31.5) + halfRangeMs(13.5) = 45ms -
    // the maximum delay this voice can ever reach. Shared so dependents
    // (SideOrthogonalizer's Mid-history buffer) can size themselves
    // correctly instead of duplicating/guessing this number.
    static constexpr float kMaxPossibleDelayMs = 45.0f;

    // microPitchCenterCentsIn/microPitchRangeCentsIn: this voice's EXPERIMENTAL
    // micro-pitch walker range (only has any effect if setMicroPitchEnabled(true)
    // is called - default disabled, see that method's comment). Deliberately
    // NOT symmetric between Voice A and Voice B at the call site
    // (VxWidthProcessor.cpp) - asymmetric per-voice centre/range, not a
    // mirrored +/-detune, per the design brief this was built against.
    void prepare(const double sampleRate, const std::uint32_t voiceSeed,
                 const float microPitchCenterCentsIn = 0.0f, const float microPitchRangeCentsIn = 2.0f) {
        sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
        delayLine.prepare(sr, 50.0f); // above the Loose-band 45ms ceiling (§8.3)
        // Distinct seeds per walker so delay/gain/tilt movement is independent,
        // and distinct per voice (voiceSeed) so Voice A and B don't correlate.
        delayWalker.prepare(sr, voiceSeed ^ 0xA1u, 0.6f, 1.6f, 0.35f);
        gainWalker.prepare(sr, voiceSeed ^ 0xB2u, 0.7f, 2.0f, 0.40f);
        tiltWalker.prepare(sr, voiceSeed ^ 0xC3u, 0.9f, 2.4f, 0.50f);
        // EXPERIMENTAL: independent of the delay/gain/tilt walkers above (own
        // seed offset, own slow non-periodic trajectory) - deliberately NOT
        // derived from or synchronised with the delay walker, so pitch
        // differentiation is never a side-effect of timing movement.
        microPitchWalker.prepare(sr, voiceSeed ^ 0xE5u, 2.5f, 6.0f, 1.2f);
        microPitchShifter.prepare(sr, kMicroPitchWindowMs);
        microPitchCenterCents = microPitchCenterCentsIn;
        microPitchRangeCents = microPitchRangeCentsIn;
        reset();
    }

    void reset() noexcept {
        delayLine.reset();
        delayWalker.reset();
        gainWalker.reset();
        tiltWalker.reset();
        tiltLowpassState = 0.0f;
        currentDelayMsState = -1.0f; // re-primed on first process() call
        microPitchWalker.reset();
        microPitchShifter.reset();
        lastMicroPitchCents = 0.0f;
        smoothedMicroPitchCents = 0.0f;
    }

    // tightness01: 0=Tight, 1=Loose (§4.3/§8.3/§8.7 band mapping).
    // transientRisk: 0..1, reduces voice level during onsets (§8.2, §9 preview).
    float process(const float midInput, const float tightness01, const float transientRisk) noexcept {
        // §8.3 delay bands (ms): Tight 5-15, Natural 10-28, Loose 18-45.
        // Piecewise-linear interpolation across the three named anchors.
        float centerMs, halfRangeMs;
        if (tightness01 <= 0.5f) {
            const float t = tightness01 / 0.5f;
            centerMs = juce::jmap(t, 10.0f, 19.0f);
            halfRangeMs = juce::jmap(t, 5.0f, 9.0f);
        } else {
            const float t = (tightness01 - 0.5f) / 0.5f;
            centerMs = juce::jmap(t, 19.0f, 31.5f);
            halfRangeMs = juce::jmap(t, 9.0f, 13.5f);
        }
        const float delayTargetMs = centerMs + delayWalker.process() * halfRangeMs;

        // Max Doppler budget per tightness band (Tight/Natural/Loose anchors,
        // same piecewise-linear shape as the delay bands above): cents ->
        // ms/s via cents = 1200*log2(1+rate) ~= 1731.23*rate for small rate,
        // rate = d(delayMs)/dt / 1000 -> maxSlopeMsPerSec = maxCents *
        // (1000/1731.23).
        float maxCentsBudget;
        if (tightness01 <= 0.5f) {
            const float t = tightness01 / 0.5f;
            maxCentsBudget = juce::jmap(t, 3.0f, 6.0f);
        } else {
            const float t = (tightness01 - 0.5f) / 0.5f;
            maxCentsBudget = juce::jmap(t, 6.0f, 10.0f);
        }
        constexpr float kMsPerSecPerCent = 0.577705f;
        const float maxDeltaMsPerSample = (maxCentsBudget * kMsPerSecPerCent) / static_cast<float>(sr);

        if (currentDelayMsState < 0.0f) {
            currentDelayMsState = delayTargetMs; // first call: prime, no slew needed
            lastDeltaMs = 0.0f;
        } else {
            const float diff = delayTargetMs - currentDelayMsState;
            lastDeltaMs = juce::jlimit(-maxDeltaMsPerSample, maxDeltaMsPerSample, diff);
            currentDelayMsState += lastDeltaMs;
        }
        const float delaySamples = currentDelayMsState * 0.001f * static_cast<float>(sr);

        // §8.7: gain variation range grows with looseness.
        const float gainRangeDb = juce::jmap(tightness01, 0.2f, 1.0f);
        const float transientReduction = 1.0f - 0.6f * juce::jlimit(0.0f, 1.0f, transientRisk);
        const float gainDb = gainWalker.process() * gainRangeDb;
        const float gain = juce::Decibels::decibelsToGain(gainDb) * transientReduction;

        const float delayed = delayLine.process(midInput, delaySamples);

        // Micro-pitch stage (permanent, always-on - see setMicroPitchEnabled()'s
        // own comment). Operates on the ALREADY-delayed signal,
        // via an entirely separate mechanism (MicroPitchShifter) from the
        // delay line's own Doppler modulation above - timing and pitch stay
        // conceptually independent, not one masquerading as the other.
        float pitchStageOutput = delayed;
        if (microPitchEnabled) {
            const float walkerValue = microPitchWalker.process(); // -1..1, non-periodic
            const float micropitchCents = microPitchCenterCents + walkerValue * microPitchRangeCents;
            lastMicroPitchCents = micropitchCents;
            constexpr float kSmoothingAlpha = 0.9999f; // several-second time constant for the telemetry average
            smoothedMicroPitchCents = kSmoothingAlpha * smoothedMicroPitchCents + (1.0f - kSmoothingAlpha) * micropitchCents;
            const float ratio = std::pow(2.0f, micropitchCents / 1200.0f);
            pitchStageOutput = microPitchShifter.process(delayed, ratio);
        }

        // §8.6: broad spectral tilt, +/-0.2..1.0dB, via a one-pole
        // lowpass/highpass split rather than narrow resonant EQ. Cutoff-
        // derived alpha (not a bare 0.995f constant) so the low/high split
        // sits at the same ~700Hz-ish crossover regardless of sample rate,
        // matching how OnePoleTilt/OnePoleHighpass compute alpha elsewhere
        // in this product (VxWidthSpectralShaping.h). Range scaled by
        // Tightness (VXWIDTH_AUDIT.md #5 fix, 2026-08-08) - same
        // juce::jmap(tightness01, ...) shape as gainRangeDb above, so
        // spectral independence actually grows with looseness like the
        // header comment always claimed, instead of being a fixed +/-1dB
        // regardless of Tightness.
        const float tiltRangeDb = juce::jmap(tightness01, 0.2f, 1.0f);
        const float tiltAlpha = std::exp(-2.0f * juce::MathConstants<float>::pi * kTiltCutoffHz / static_cast<float>(sr));
        tiltLowpassState += (pitchStageOutput - tiltLowpassState) * (1.0f - tiltAlpha);
        const float highPart = pitchStageOutput - tiltLowpassState;
        const float tiltDb = tiltWalker.process() * tiltRangeDb; // -1..1 -> +/-tiltRangeDb max
        const float tiltGainDelta = (juce::Decibels::decibelsToGain(tiltDb) - 1.0f) * 0.5f;
        const float tilted = pitchStageOutput + highPart * tiltGainDelta;

        return tilted * gain;
    }

    // Permanent, always-on (2026-08-07, graduated from an experimental,
    // user-facing toggle - see VxWidthProcessor.cpp's setAdtMicroPitchEnabled()
    // call site, no UI control, no APVTS parameter). Still a runtime switch
    // rather than a compile-time macro (default false at construction, only
    // ever flipped true by production's own unconditional call in
    // prepareSuite()/resetSuite()) - keeps this class's own tests able to
    // exercise both the enabled and disabled path without separate compiled
    // variants.
    void setMicroPitchEnabled(const bool b) noexcept { microPitchEnabled = b; }
    [[nodiscard]] bool isMicroPitchEnabled() const noexcept { return microPitchEnabled; }

    // Telemetry: instantaneous and long-term-smoothed (several-second time
    // constant) micro-pitch offset (cents) actually applied on the most
    // recent process() call - lets tests/debug tooling measure each voice's
    // real pitch behaviour independently, not just trust the configured
    // center/range.
    [[nodiscard]] float lastMicroPitchCentsApplied() const noexcept { return lastMicroPitchCents; }
    [[nodiscard]] float longTermMicroPitchCentsAverage() const noexcept { return smoothedMicroPitchCents; }

    // Instantaneous Doppler-equivalent pitch shift (in cents) implied by the
    // delay change applied on the most recent process() call. Debug/test
    // visibility only - lets tests PROVE the slew clamp above actually keeps
    // each tightness band inside its intended budget, rather than trusting
    // the clamp math alone.
    [[nodiscard]] float lastInstantaneousPitchShiftCents() const noexcept {
        const float ratePerSecond = lastDeltaMs * static_cast<float>(sr) / 1000.0f;
        return 1200.0f * std::log2(1.0f + std::abs(ratePerSecond));
    }

    // Current slewed delay (ms). A real production dependency, not a debug
    // accessor - SideOrthogonalizer's two dynamic taps track wherever each
    // ADT voice currently sits, since that's exactly where ADT's own
    // Mid-correlated energy concentrates (see VxWidthProcessor.cpp).
    [[nodiscard]] float currentDelayMs() const noexcept { return currentDelayMsState; }

private:
    // EXPERIMENTAL micro-pitch window - short enough for low latency/no
    // large-buffer footprint, long enough that a wrap-crossfade at
    // few-cents shift rates lands many seconds apart (see
    // VxWidthMicroPitch.h's header comment for why that's inaudible as
    // periodicity).
    static constexpr float kMicroPitchWindowMs = 20.0f;

    double sr = 48000.0;
    FractionalDelayLine delayLine;
    StochasticWalker delayWalker;
    StochasticWalker gainWalker;
    StochasticWalker tiltWalker;
    float tiltLowpassState = 0.0f;
    float currentDelayMsState = -1.0f;
    float lastDeltaMs = 0.0f;

    bool microPitchEnabled = false;
    StochasticWalker microPitchWalker;
    MicroPitchShifter microPitchShifter;
    float microPitchCenterCents = 0.0f;
    float microPitchRangeCents = 2.0f;
    float lastMicroPitchCents = 0.0f;
    float smoothedMicroPitchCents = 0.0f;
};

} // namespace vxsuite::width
