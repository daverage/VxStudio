#pragma once
#include <vector>
#include <array>

namespace vxsuite::deverb {

/**
 * Blind RT60 estimator based on Löllmann et al. (2012/2018).
 *
 * Operates at a fixed internal rate of 16 kHz (decimation is handled
 * internally).  Call pushSamples() from the audio thread each block;
 * the estimator updates asynchronously at its own frame rate.
 *
 * getEstimatedRt60() is safe to call from the audio thread — it returns
 * the last committed estimate (written on the same audio thread).
 *
 * Algorithm outline:
 *   1. Decimate input to 16 kHz via a simple integer decimator.
 *   2. Decompose into kNumBands 1/3-octave subbands via IIR filterbank.
 *   3. Accumulate frames of kFrameSamples; divide into kSubFrames sub-frames.
 *   4. In each sub-frame, compute instantaneous RMS energy.
 *   5. Detect monotonically decreasing energy sequences spanning ≥3 sub-frames.
 *   6. On detected decay: compute closed-form ML slope estimate (O(N) arithmetic).
 *   7. Convert slope to RT60; clamp to [kRt60Min, kRt60Max].
 *   8. Add to histogram; track peak.
 *   9. Smooth: rt60_smooth = γ·rt60_smooth + (1−γ)·histogram_peak.
 */
class LollmannRt60Estimator {
public:
    LollmannRt60Estimator();

    /**
     * Initialise for a given host sample rate.  Computes the decimation
     * ratio and resets all state.  Call before streaming.
     */
    void prepare(double hostSampleRate);

    /** Reset all history and estimator state. */
    void reset();

    /**
     * Push a block of mono audio samples.  The estimator decimates and
     * processes internally; call this once per processBlock from any
     * single channel (e.g. channel 0).
     *
     * @param samples  Pointer to float samples (read-only).
     * @param count    Number of samples in the block.
     */
    void pushSamples(const float* samples, int count);

    /**
     * Returns the current smoothed RT60 estimate in seconds.
     * Returns kDefaultRt60 until the estimator has collected enough data.
     * Safe to call from the audio thread.
     */
    float getEstimatedRt60() const;

    // ── Debug / measure tool accessors ─────────────────────────────────────
    /** Force the estimate to a fixed value (bypasses internal tracking). */
    void setFixedRt60(float seconds);
    void clearFixedRt60();

private:
    // ── Decimation ──────────────────────────────────────────────────────────
    int   decimRatio   = 3;         // ceil(hostSampleRate / kInternalRate)
    int   decimCounter = 0;
    float decimAccum   = 0.0f;

    // ── Subband filterbank (one biquad per band) ───────────────────────────
    static constexpr int kNumBands = 9; // 1/3-octave bands: 125–800 Hz
    struct BiquadState  { float x1=0, x2=0; };
    struct BiquadCoeffs { float b0, b2, a1, a2; }; // b1=0 for bandpass
    std::array<BiquadCoeffs, kNumBands> bandCoeffs;
    std::array<BiquadState,  kNumBands> bandState;

    void computeBandCoeffs(); // populates bandCoeffs from kInternalRate

    // ── Frame accumulation ──────────────────────────────────────────────────
    static constexpr int   kInternalRate    = 16000;
    static constexpr float kFrameDurationS  = 0.3072f;             // ≈ 307 ms
    static constexpr int   kFrameSamples    = static_cast<int>(kInternalRate * kFrameDurationS); // 4915
    static constexpr int   kSubFrames       = 9;
    static constexpr int   kSubFrameSamples = kFrameSamples / kSubFrames; // 546

    // Energy accumulator per subframe per band
    std::array<std::vector<float>, kNumBands> subFrameEnergy; // [band][subframe]
    std::array<int,                kNumBands> subFrameCount;  // samples in current subframe
    std::array<int,                kNumBands> subFrameIdx;    // current subframe index

    // ── ML closed-form slope estimator ─────────────────────────────────────
    /**
     * Given N log-energy values assumed to follow a linear decay,
     * returns the ML estimate of the slope (nepers/subframe) using the
     * closed-form Cramér-Rao-efficient solution.
     */
    static float mlSlopeEstimate(const float* logEnergy, int N);

    /** Convert a log-energy decay slope to RT60 (seconds). */
    static float slopeToRt60(float slopePerSubFrame);

    // ── Histogram accumulator ───────────────────────────────────────────────
    static constexpr int   kHistBins  = 120;
    static constexpr float kRt60Min   = 0.05f;
    static constexpr float kRt60Max   = 8.00f;
    static constexpr int   kHistDepth = 800; // keep last 800 estimates

    std::vector<int>   histogram;       // kHistBins counts
    std::vector<float> historyBuffer;   // ring of last kHistDepth RT60 values
    int                historyWritePos  = 0;
    int                historyCount     = 0;

    void  addEstimate(float rt60);
    float histogramPeak() const;

    // ── Output smoothing ────────────────────────────────────────────────────
    static constexpr float kSmoothGamma = 0.95f;
    static constexpr float kDefaultRt60 = 0.50f;

    float smoothedRt60 = kDefaultRt60;
    bool  hasEstimate  = false;
    float outputRt60   = kDefaultRt60;

    // Debug override
    float fixedRt60 = 0.0f;
    bool  useFixed  = false;

    // ── Per-band processing ─────────────────────────────────────────────────
    void processBandSample(int band, float sample);
    void onSubFrameComplete(int band);
    void onFrameComplete(int band);
};

} // namespace vxsuite::deverb
