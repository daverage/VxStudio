#pragma once

#include <cstdint>
#include <vector>

namespace vxsuite::tune {

// Epoch-synchronous TD-PSOLA pitch shifter (build spec F1).
//
// Analysis epochs are tracked on a low-passed mono mix at the spacing given
// by the detector's period hints, aligned to waveform peaks. Synthesis
// re-spaces two-period Hann grains at period/ratio and overlap-adds; because
// grains are re-spaced rather than resampled, the spectral envelope
// (formants) is intrinsically preserved for the small ratios this product
// uses.
//
// Streaming rules:
// - Grain placement never waits for future input: each synthesis epoch uses
//   the newest analysis epoch whose full window is already available,
//   falling back to older epochs (grain repetition) rather than stalling.
//   That keeps latency at a fixed ceil(sr/80) samples (12.5 ms @ 48 kHz).
// - At zero shift, synthesis epochs snap onto analysis epochs + latency, so
//   consecutive equal-period Hann grains tile to exactly 1 (COLA) and the
//   output is the plain delayed input.
// - Unvoiced or uncovered stretches blend to a latency-aligned passthrough
//   over ~5 ms; the same grain schedule drives every channel, so the stereo
//   image cannot wobble.
//
// Realtime-safe after prepare(): no allocation, locks, or I/O in process().
class PsolaShifter {
public:
    void prepare(double sampleRate, int maxBlockSamples, int numChannels);
    void reset();

    // Target shift in cents (+ = up); smoothed internally per grain.
    void setShiftCents(float cents) noexcept { targetCents = cents; }

    // Detector feed, once per analysis hop.
    void setPeriodHint(float periodSamples, float voicedConfidence) noexcept;

    void process(float* const* channels, int numChannels, int numSamples) noexcept;

    int latencySamples() const noexcept { return latency; }

private:
    void appendInput(const float* const* channels, int numChannels,
                     int offset, int count) noexcept;
    void trackEpochs() noexcept;
    void placeGrains(int numChannels) noexcept;
    void placeOneGrain(std::int64_t synthCentre, std::int64_t analysisEpoch,
                       float subSampleShift, int halfLength, float gain,
                       int numChannels) noexcept;
    void emitOutput(float* const* channels, int numChannels,
                    int offset, int count) noexcept;

    double sr = 48000.0;
    int latency = 600;
    int maxPeriod = 686;          // detection floor (~70 Hz)
    int mask = 0;                 // ring size - 1 (power of two)
    int channelsPrepared = 0;

    std::vector<std::vector<float>> inRing;   // per channel
    std::vector<std::vector<float>> outRing;  // per channel, overlap-add
    // Overlap-add gain normalisation: Hann windows at 2T grain / T hop are
    // COLA-exact (sum to 1) only at ratio 1. At any other ratio the
    // synthesis spacing is no longer T, so the window sum drifts from
    // unity and the output level pulses with it. This tracks the actual
    // window-sum-so-far per sample so emitOutput can divide it back out.
    std::vector<float> weightRing;
    std::vector<float> lpRing;                // mono low-passed, epoch search
    float lp1 = 0.0f, lp2 = 0.0f;             // low-pass state
    // Cutoff tracks ~2.2x the detected fundamental (clamped to [150, 900]
    // Hz) rather than a fixed 900 Hz: a fixed cutoff still passes several
    // harmonics at a typical fundamental, and formant structure in that
    // band gives the peak search multiple similar-height candidates per
    // period. Picking a different one cycle-to-cycle is a small epoch
    // position error that is nonetheless a huge phase error at high
    // harmonics on overlap-add (confirmed by measuring HF loss + floor
    // rise against real vocal material — that mechanism is the
    // "bubbling" artifact).
    float lpCoeff = 0.1f;

    std::int64_t inputCount = 0;   // absolute samples received
    std::int64_t emitCursor = 0;   // next output sample to emit

    // Detector hints, conditioned for rendering: hysteresis on the voiced
    // decision (raw confidence flicker must not gate the wet/dry blend),
    // per-hop period rate limiting (octave-error frames must not whipsaw
    // the epoch grid), and epoch invalidation after sustained silence.
    float hintPeriod = 0.0f;       // samples; 0 = unvoiced
    float hintConfidence = 0.0f;
    // Smoothed period: the raw per-hop detector estimate has cycle-to-cycle
    // noise even on a steady real note. Grain half-length is set from each
    // epoch's period, so unsmoothed jitter gives neighbouring grains
    // slightly different window lengths - breaking overlap-add unity gain
    // independent of epoch *position* accuracy. This was the residual
    // inharmonic-floor contributor left after tightening epoch alignment
    // (confirmed: floor stayed ~2 dB above dry even after that fix, on the
    // user's real vocal). Large jumps (real note changes, octave escapes)
    // still pass through immediately via the existing rate-limit clamp.
    float smoothedPeriod = 0.0f;
    bool voicedActive = false;
    int softHops = 0;              // medium confidence: flywheel, don't drop
    int hardHops = 0;              // hard unvoiced evidence

    // Analysis epoch tracker
    static constexpr int kMaxEpochs = 64;
    struct Epoch {
        std::int64_t position = 0;
        float period = 0.0f;
    };
    Epoch epochs[kMaxEpochs] {};
    int epochCount = 0;            // total ever written (ring index = count % kMaxEpochs)
    // PLL-style epoch grid: the phase accumulates the fractional hinted
    // period; peak alignment may only NUDGE it (clamped small correction).
    // Independent peak re-search per period jitters on real voices (shifting
    // waveform shape) and the jittered grid is the audible bubble.
    double epochPhase = -1.0;      // next predicted epoch position; <0 = unseeded
    // Waveform-similarity (NCC) epoch confirmation: each new epoch is
    // matched against the PREVIOUS confirmed cycle's waveform shape, not
    // against a single amplitude peak. Amplitude peak-picking on a
    // low-passed signal is fooled whenever formant structure gives the
    // period more than one similar-height lobe - it can jump to a
    // different lobe cycle-to-cycle, and that small position error is a
    // large phase error at high harmonics on overlap-add (measured
    // directly against real vocal material as high-frequency loss and
    // inharmonic floor rise). Matching the whole cycle's shape against the
    // last confirmed one is far less ambiguous: a wrong-lobe candidate's
    // surrounding waveform generally does not match even when its peak
    // amplitude does. The reference is simply the last confirmed epoch's
    // window read back out of lpRing - no separate template buffer needed.
    std::int64_t lastConfirmedEpochPos = -1;
    float lastVoicedPeriod = 0.0f;
    std::int64_t lastUsedEpochPos = -1;   // grain-selection continuity

    // Synthesis state. The next grain centre accumulates fractionally —
    // truncating the per-grain spacing to integers biases the rendered
    // pitch sharp by several cents.
    double nextSynthCentre = 0.0;
    std::int64_t coverEnd = -1;    // furthest sample any grain has written
    float targetCents = 0.0f;
    float smoothedCents = 0.0f;

    // Wet/dry blend (grain coverage + voicing), per emitted sample
    float mix = 0.0f;
    float mixStep = 0.01f;

    // Sustained zero shift routes to the bit-exact delay path (keeps the
    // Listen contract: no intervention -> silence), with the OLA kept warm
    // for an artifact-free crossfade back in.
    bool parkedNow = false;
    int parkedRun = 0;
    int parkHoldSamples = 2400;
};

} // namespace vxsuite::tune
