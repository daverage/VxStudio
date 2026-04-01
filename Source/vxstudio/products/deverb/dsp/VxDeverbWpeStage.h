#pragma once
#include <vector>
#include <complex>

namespace vxsuite::deverb {

/**
 * Frame-online single-channel WPE dereverberation stage.
 *
 * Operates in the STFT domain — it receives and returns complex spectra
 * directly, without its own FFT.  It is designed to be called from
 * SpectralProcessor::processFrame() after the forward FFT and before
 * the inverse FFT.
 *
 * Memory:
 *   Per bin: G[K] complex filter (2K floats), R_inv[K×K] complex matrix
 *   (2K² floats), lambda (1 float), plus K+delta frames of history (2(K+delta) floats).
 *   At K=10, delta=3, F=513: ~125 KB.
 *
 * Call prepare() before streaming; processSpectrum() once per STFT frame.
 * Zero heap allocations after prepare().
 */
class WpeStage {
public:
    WpeStage();

    /**
     * Allocate all state for the given STFT configuration.
     * @param numBins   Positive-frequency bin count (fftSize/2 + 1).
     * @param K         Prediction filter taps (default 10).
     * @param delta     Prediction delay in frames (default 3).
     * @param alpha     RLS forgetting factor (default 0.995).
     * @param beta      PSD smoothing factor (default 0.80).
     */
    void prepare(int numBins, int K = 10, int delta = 3,
                 float alpha = 0.995f, float beta = 0.80f);

    /** Zero all filter, correlation, and history state. */
    void reset();

    /**
     * Process one STFT frame in-place.
     *
     * @param re    Real parts of positive-frequency bins [0..numBins−1].
     *              Modified in-place with WPE output.
     * @param im    Imaginary parts.  Modified in-place.
     * @param amount  Wet mix 0–1.  0 = bypass (history updated, no state update),
     *                1 = full WPE output.
     */
    void processSpectrum(float* re, float* im, float amount) noexcept;

private:
    using Cx = std::complex<float>;

    int   numBins_ = 0;
    int   K_       = 10;
    int   delta_   = 3;
    float alpha_   = 0.995f;
    float beta_    = 0.80f;

    // History ring: [histDepth_ × numBins_] complex
    // histDepth_ = K_ + delta_
    int             histDepth_ = 0;
    int             histWrite_ = 0;
    std::vector<Cx> history_;   // [histDepth_ × numBins_], row-major

    // Per-bin state
    std::vector<Cx>    G_;       // [numBins_ × K_] filter coefficients
    std::vector<Cx>    R_inv_;   // [numBins_ × K_ × K_] inverse correlation
    std::vector<float> lambda_;  // [numBins_] variance estimate

    // Per-frame scratch (pre-allocated, no audio-thread allocation)
    std::vector<Cx> yDelayed_; // [K_] per-bin delayed observation vector
    std::vector<Cx> kVec_;     // [K_] Kalman gain vector
    std::vector<Cx> tmpK_;     // [K_] scratch: R_inv · ỹ
    std::vector<Cx> yHRinv_;   // [K_] scratch: ỹ^H · R_inv[:, c]

    // Inline accessors into flat arrays
    Cx& hist(int frame, int bin)    { return history_[static_cast<size_t>(frame) * static_cast<size_t>(numBins_) + static_cast<size_t>(bin)]; }
    Cx& g(int bin, int tap)         { return G_[static_cast<size_t>(bin) * static_cast<size_t>(K_) + static_cast<size_t>(tap)]; }
    Cx& rinv(int bin, int r, int c) { return R_inv_[static_cast<size_t>(bin) * static_cast<size_t>(K_) * static_cast<size_t>(K_) + static_cast<size_t>(r) * static_cast<size_t>(K_) + static_cast<size_t>(c)]; }
};

} // namespace vxsuite::deverb
