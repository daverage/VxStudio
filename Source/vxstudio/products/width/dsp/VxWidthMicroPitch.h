#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <vector>

// EXPERIMENTAL (2026-08-07, ADT micro-pitch investigation, off by default -
// see VxWidthAdtVoice.h's `microPitchEnabled` runtime flag, default false,
// no user control exposed). Time-domain micro-transposition suitable ONLY
// for a few cents of shift, not chorus-range detune - deliberately not a
// phase vocoder (no large-window FFT latency) and deliberately not reusing
// the ADT delay line's own Doppler modulation (timing and pitch stay
// conceptually separate per the design brief this was built against).
//
// Classic two-tap variable-speed-delay pitch shifter (Zolzer, "DAFX":
// pitch shifting via a modulated delay line read at a slightly different
// rate than it's written). Two read taps, phase-locked exactly half a
// window apart, each amplitude-weighted by a TRIANGULAR window that is
// zero at the tap's wrap point and peaks at the window's midpoint; because
// the two taps are a half-window apart, their triangular weights sum to
// exactly 1.0 at every instant (w(d) + w(d + W/2 mod W) == 1), so the
// crossfade is constant-power by construction, not tuned by ear.
//
// For a shift of only a few cents, the per-sample delay drift rate
// (ratio-1) is tiny (~0.0006-0.006 samples/sample), so a tap only wraps
// (and crossfades) roughly once every several seconds at a ~20ms window -
// far too slow to read as a periodic LFO/chorus artefact, which is exactly
// why this technique is appropriate here but would sound wrong for a
// larger, chorus-style shift (frequent, audible wrap-crossfades).
namespace vxsuite::width {

class MicroPitchShifter {
public:
    void prepare(const double sampleRate, const float windowMs) {
        sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
        windowSamples = juce::jmax(4.0f, windowMs * 0.001f * static_cast<float>(sr));
        const int ringSize = static_cast<int>(windowSamples) * 2 + 8; // headroom for both taps + interpolation
        ring.assign(static_cast<size_t>(ringSize), 0.0f);
        writePos = 0;
        reset();
    }

    void reset() noexcept {
        std::fill(ring.begin(), ring.end(), 0.0f);
        writePos = 0;
        phase = 0.0f; // tap A's delay-phase within [0, windowSamples); tap B is always phase + W/2
    }

    // pitchRatio: 1.0 = no shift; e.g. 2^(cents/1200) for a `cents` shift.
    float process(const float x, const float pitchRatio) noexcept {
        if (ring.empty())
            return x;
        const int ringSize = static_cast<int>(ring.size());

        ring[static_cast<size_t>(writePos)] = x;

        // Advance the read-delay phase at the rate implied by the ratio -
        // ratio>1 (pitch up) shrinks the delay over time (reads catch up
        // to the write pointer, wrap+crossfade, repeat); ratio<1 grows it.
        phase -= (pitchRatio - 1.0f);
        if (phase < 0.0f) phase += windowSamples;
        if (phase >= windowSamples) phase -= windowSamples;
        float phaseB = phase + windowSamples * 0.5f;
        if (phaseB >= windowSamples) phaseB -= windowSamples;

        const float gainA = triangleWindow(phase);
        const float gainB = triangleWindow(phaseB);

        const float sampleA = readInterpolated(writePos, phase, ringSize);
        const float sampleB = readInterpolated(writePos, phaseB, ringSize);

        writePos = (writePos + 1) % ringSize;
        return gainA * sampleA + gainB * sampleB;
    }

private:
    [[nodiscard]] float triangleWindow(const float d) const noexcept {
        // Triangle peaking at d=W/2, zero at d=0 and d=W. With a second tap
        // exactly W/2 out of phase, the two windows sum to 1 for all d.
        const float normalized = d / windowSamples; // 0..1
        return 1.0f - std::abs(2.0f * normalized - 1.0f);
    }

    [[nodiscard]] float readInterpolated(const int writePosition, const float delaySamples, const int ringSize) const noexcept {
        float pos = static_cast<float>(writePosition) - delaySamples;
        while (pos < 0.0f) pos += static_cast<float>(ringSize);
        const int i0 = static_cast<int>(pos) % ringSize;
        const float frac = pos - std::floor(pos);
        const int i1 = (i0 + 1) % ringSize;
        const float y0 = ring[static_cast<size_t>(i0)];
        const float y1 = ring[static_cast<size_t>(i1)];
        return y0 + (y1 - y0) * frac; // linear - sufficient for a few-cents shift
    }

    double sr = 48000.0;
    float windowSamples = 960.0f; // ~20ms @ 48kHz, set properly in prepare()
    std::vector<float> ring;
    int writePos = 0;
    float phase = 0.0f;
};

} // namespace vxsuite::width
