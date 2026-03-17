#pragma once

#include "../../../framework/VxSuiteAudioProcessStage.h"
#include "../../../framework/VxSuiteFft.h"
#include "VxDeverbRt60Estimator.h"
#include "VxDeverbWpeStage.h"

#include <juce_dsp/juce_dsp.h>

#include <vector>
#include <memory>

namespace vxsuite::deverb {

/**
 * Single-channel (or per-channel stereo) dereverberation using
 * Habets (2009) Late-Reverberant Spectral Variance (LRSV) estimation.
 *
 * ── Algorithm ────────────────────────────────────────────────────────────────
 * Based on Polack's statistical room-impulse-response model: the late
 * reverberation at each STFT bin behaves as exponentially-decaying white
 * Gaussian noise.  The late-reverberant power at frame m, bin k is estimated
 * as:
 *
 *   Γ_late(m,k) = κ · exp(−2δT) · |Y(m−T_frames, k)|²
 *
 * where:
 *   δ  = ln(1000) / RT60  =  6.908 / RT60   [decay rate, s⁻¹]
 *   T  = kTBoundaryS      ≈  50 ms           [early/late boundary]
 *   κ  = 1.0              (conservative; see Habets §III-B for adaptive κ)
 *
 * A Wiener gain is applied per bin, preserving phase:
 *
 *   G(m,k) = sqrt( max( 1 − amount·Γ_late / |Y|² , kFloor² ) )
 *
 * Per-bin IIR temporal smoothing suppresses musical-noise artefacts.
 *
 * ── Signal-agnostic ──────────────────────────────────────────────────────────
 * Unlike WPE, this estimator makes no speech-specific source assumption.
 * It handles voice and polyphonic/mixed audio equally well.
 *
 * ── STFT parameters (auto-scaled to sample rate) ─────────────────────────────
 *   FFT size  : 1024 @ 44.1/48 kHz  (21.3 ms),  2048 @ 88.2/96 kHz  (21.3 ms)
 *   Hop size  : FFT/4  (75 % overlap, COLA-satisfying periodic Hann window)
 *   Latency   : FFT − Hop  =  3 × Hop  ≈ 16 ms @ 48 kHz
 *
 * ── RT60 estimation ──────────────────────────────────────────────────────────
 * A simple per-channel decay-slope tracker provides a running RT60 estimate
 * that the LRSV coefficient adapts to.  Accuracy is ±30 %, which is
 * sufficient because the exp(−2δT) term changes slowly with RT60 error.
 * The user's "reduce" strength parameter scales the subtraction independently.
 *
 * References:
 *   E. A. P. Habets, S. Gannot, I. Cohen (2009) "Late Reverberant Spectral
 *   Variance Estimation Based on a Statistical Model", IEEE Signal Processing
 *   Letters, 16(9):770–773.
 *
 *   K. Lebart, J.-M. Boucher, P. N. Denbigh (2001) "A New Method Based on
 *   Spectral Subtraction for Speech Dereverberation", Acta Acustica, 87:359–366.
 */
class SpectralProcessor final : public AudioProcessStage {
public:
    SpectralProcessor();
    ~SpectralProcessor() override;

    /** Set the channel count that will be processed after prepare(). */
    void setChannelCount(int numChannels);
    void setRt60PresetSeconds(float rt60Seconds);
    void clearRt60Preset();
    void setDeterministicReset(bool shouldUseDefaultRt60) noexcept;
    float getTrackedRt60Seconds(int channel) const noexcept;
    void setOverSubtract(float newOverSubtract) noexcept;
    float getOverSubtract() const noexcept { return overSubtract; }
    void setDebugNoCepstral(bool shouldBypass) noexcept { debugNoCepstral = shouldBypass; }
    bool isDebugNoCepstral() const noexcept { return debugNoCepstral; }

    /** Voice mode: enables the per-channel WPE dereverberation stage. */
    bool  voiceMode  = false;
    float wpeAmount  = 1.0f;

    // ── AudioProcessStage interface ───────────────────────────────────────────

    /**
     * Must be called before processing.  Allocates all per-channel state so
     * that processInPlace() makes zero heap allocations on the audio thread.
     *
     * @param sampleRate    Host sample rate (Hz).  Values < 1000 fall back to 48000.
     * @param maxBlockSize  Maximum buffer size the host will deliver.  The OLA
     *                      accumulator is sized accordingly.
     */
    void prepare(double sampleRate, int maxBlockSize) override;

    /** Zeros all history buffers and resets the latency-fill state. */
    void reset() override;

    /**
     * Reported algorithmic latency: (fftSize − hopSize) samples.
     * ≈ 16 ms at 48 kHz.  The host (Reaper) compensates via PDC.
     */
    int getLatencySamples() const override { return latencySamples; }

    /**
     * Process a buffer of audio in-place.
     *
     * @param buffer   Audio buffer; processed channel by channel (stereo-safe).
     * @param amount   Dereverberation strength, 0 = dry (pass-through),
     *                 1 = full LRSV subtraction.  Scales Γ_late before the
     *                 Wiener gain computation, so the floor guarantee holds at
     *                 all amounts.
     * @param options  Ignored in this implementation (reserved for future
     *                 voice-mode guard, speech focus, etc.).
     */
    bool processInPlace(juce::AudioBuffer<float>& buffer,
                        float                    amount,
                        const ProcessOptions&    options) override;

private:
    // ── Per-channel DSP state ─────────────────────────────────────────────────
    struct ChannelState {
        /** Circular ring buffer holding the last fftSize input samples. */
        std::vector<float> inFifo;

        /**
         * OLA output accumulator.  Size = maxBlockSize + 2 × fftSize to
         * guarantee that writes from Phase 1 never overrun reads in Phase 2
         * regardless of host block size.
         */
        std::vector<float> olaAccum;

        /** JUCE FFT workspace: 2 × fftSize interleaved complex floats. */
        std::vector<float> fftBuf;

        /**
         * Magnitude-squared history ring.  Layout: [tHistFrames × numBins].
         * The slot at histWriteIdx always contains the OLDEST frame
         * (= T_boundary seconds ago): it is read before being overwritten
         * with the current frame's power.  This avoids a separate "delay-by-N"
         * index calculation.
         */
        std::vector<float> magSqHist;

        /** Per-bin smoothed Wiener gain (IIR, suppresses musical noise). */
        std::vector<float> gainSmooth;

        /** Scratch buffer for log-domain cepstral/frequency smoothing. */
        std::vector<float> logGain;

        /** Per-channel WPE dereverberation stage (voice mode). */
        WpeStage wpeStage;

        int   inFifoWritePos = 0; ///< next write slot in inFifo ring
        int   hopFillCount   = 0; ///< samples accumulated since last FFT frame
        int   olaWritePos    = 0; ///< next OLA write position (absolute, mod accum size)
        int   olaReadPos     = 0; ///< next OLA read  position (absolute, mod accum size)
        int   histWriteIdx   = 0; ///< next slot to overwrite in magSqHist ring
    };

    // ── Internal methods ──────────────────────────────────────────────────────

    /** (Re-)initialise one channel state to a clean latency-filled condition. */
    void allocateAndResetChannel(ChannelState& ch) const;

    /** Ensure all channel state is allocated before realtime processing starts. */
    void allocateChannels(int numChannels);

    /**
     * STFT frame processing for one channel.
     * Called every hopSize samples.  Reads inFifo, writes to olaAccum.
     */
    void processFrame(ChannelState& ch, float amount, float lrsvCoeff) noexcept;

    /**
     * Compute the LRSV decay coefficient from a per-channel RT60 estimate.
     *   coefficient = exp(−13.816 × T_boundary / RT60)
     */
    static float lrsvCoeffFromRt60(float rt60Seconds) noexcept;

    // ── STFT configuration (set in prepare, never changed during playback) ────
    vxsuite::RealFft fft;
    std::vector<float>              window;   ///< periodic Hann, length fftSize
    std::vector<float>              olaNorm;  ///< overlap norm for windowed OLA

    int    fftSize       = 1024;
    int    hopSize       = 256;
    int    numBins       = 513;   ///< fftSize / 2 + 1
    int    tHistFrames   = 9;     ///< ceil(kTBoundaryS × sr / hopSize)
    int    olaAccumSize  = 0;     ///< maxBlockSize + 2 × fftSize
    int    latencySamples = 0;    ///< fftSize − hopSize

    double sr            = 48000.0;
    int    preparedChannels = 0;
    int    speechBinLo   = 4;    ///< bin index for ~200 Hz (voice protection lower bound)
    int    speechBinHi   = 85;   ///< bin index for ~4000 Hz (voice protection upper bound)
    float  rt60PresetSeconds = 0.0f;
    bool   useDeterministicReset = false;
    float  overSubtract = kDefaultOverSubtract;
    bool   debugNoCepstral = false;

    std::vector<ChannelState> chans; ///< one entry per audio channel

    // ── Löllmann blind RT60 estimator (shared across channels) ───────────────
    LollmannRt60Estimator rt60Estimator;

    // ── WPE scratch (pre-allocated in prepare(), no audio-thread allocation) ─
    std::vector<float> wpeReScratch;
    std::vector<float> wpeImScratch;

    // ── Algorithm constants ───────────────────────────────────────────────────

    /** Minimum Wiener gain (−20 dB floor).  Prevents complete bin nulling. */
    static constexpr float kFloor        = 0.10f;

    /** Minimum Wiener gain for speech bins in voice mode (~−9 dB).
     *  Applied to bins in the 200–4000 Hz range to protect vocal fundamentals
     *  and formants — consistent with Polish's voice-preserve policy. */
    static constexpr float kVoiceFloor   = 0.35f;

    /**
     * Per-frame gain IIR coefficient.
     * α = 0.85 → ~6 frames smoothing @ 256-sample hop, 48 kHz.
     * Increase toward 0.95 for more aggressive musical-noise suppression
     * at the cost of smearing fast transients.
     */
    static constexpr float kGainAlpha    = 0.85f;

    /**
     * LRSV energy ratio scalar κ (Habets §III-B).
     * κ = 1.0 is conservative (slight over-subtraction for close mics).
     * Adaptive κ estimation (from DRR) would improve close-mic behaviour.
     */
    static constexpr float kKappa        = 1.0f;

    /** Early / late reverb boundary in seconds (standard: 50 ms). */
    static constexpr float kTBoundaryS   = 0.050f;

    /** Default RT60 used before the blind estimator has enough data. */
    static constexpr float kDefaultRt60  = 0.50f;
    static constexpr float kDefaultOverSubtract = 2.0f;
    static constexpr int kCepLifter = 6;

    /**
     * OLA normalisation gain for periodic Hann window at 75 % overlap.
     * The overlap-add of periodic Hann windows sums to 2.0 per sample
     * (verified analytically and numerically for any N).  JUCE's inverse FFT
     * already normalises by 1/N, so the only remaining factor is 1/2.
     */
    static constexpr float kOlaGain      = 1.0f;
};

} // namespace vxsuite::deverb
