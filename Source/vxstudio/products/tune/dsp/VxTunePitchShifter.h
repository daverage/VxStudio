#pragma once

#include <vector>

namespace vxsuite::tune {

// Real-time monophonic pitch shifter v0: dual-tap modulated delay line with
// equal-power crossfade ("doppler" shifter). Consumes a correction curve in
// cents and knows nothing about notes, targets, or decisions (architecture
// doc §5.9). TD-PSOLA with formant preservation replaces this behind the
// same interface later (rule 6); for transparent correction (< 1 semitone)
// this is already artifact-light.
//
// Behaviour at zero shift: the tap phase parks on a single-tap position, so
// the output is a bit-exact pure delay of latencySamples() — the failure-
// safe passthrough contract, apart from the constant reported latency.
//
// Latency: half the crossfade window (~10.7 ms at 48 kHz), constant.
class PitchShifter {
public:
    void prepare(double sampleRate, int maxBlockSamples);
    void reset();

    // Target shift for this block, in cents (+ = up). Smoothed internally.
    void setShiftCents(float cents) noexcept { targetCents = cents; }

    void process(float* samples, int numSamples) noexcept;

    int latencySamples() const noexcept { return windowSamples / 2; }

private:
    float readTap(float delaySamples) const noexcept;

    std::vector<float> ring;
    int ringSize = 0;
    int writeIndex = 0;
    int windowSamples = 1024;

    float phase = 0.5f;          // tap phase in [0,1); 0.5 = parked single tap
    float targetCents = 0.0f;
    float smoothedCents = 0.0f;
    float smoothAlpha = 0.01f;
};

} // namespace vxsuite::tune
