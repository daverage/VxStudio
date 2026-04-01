#pragma once

#include "../../../framework/VxStudioAudioProcessStage.h"
#include "../../../framework/VxStudioFft.h"
#include "../../../framework/VxStudioSpectralHelpers.h"

#include <array>
#include <vector>
#include <juce_dsp/juce_dsp.h>

namespace vxsuite::denoiser {

/**
 * Self-contained spectral denoiser — VX Suite framework.
 *
 * ── Algorithm ────────────────────────────────────────────────────────────────
 * OM-LSA (Cohen 2003) with Decision-Directed a priori SNR estimation.
 * Martin (2001) minimum statistics blind noise floor tracking, with
 * IMCRA-style presence-weighted smoothing alpha.
 *
 * Per-frame extras:
 *   • Bark-domain transient detection (24 bands, flux ratio, 3-frame hold)
 *   • Harmonic comb protection (voice mode — locks gain across harmonics)
 *   • ERB-adaptive frequency smoothing (variable kernel: 1–10 bins)
 *   • Phase-vocoder synthesis (std::remainder wrapPi — no loop drift)
 *
 * ── Stereo ───────────────────────────────────────────────────────────────────
 * M/S processing: mid is denoised, side is latency-aligned and energy-scaled.
 * Side gain ratio is one-pole smoothed (τ ≈ 200 ms) to prevent per-block
 * pumping artifacts.
 *
 * ── STFT ─────────────────────────────────────────────────────────────────────
 *   FFT:      1024 points
 *   Hop:       256 samples (75 % overlap)
 *   Window:   sqrt-Hann (WOLA — applied at both analysis and synthesis)
 *   Latency:  fftSize − hop = 768 samples ≈ 16 ms @ 48 kHz
 *
 * ── ProcessOptions wiring ────────────────────────────────────────────────────
 *   isVoiceMode       → enables harmonic comb protection + LF stability
 *   sourceProtect     → harmonic floor gain (more protect = higher floor)
 *   lateTailAggression → suppression strength scalar
 *   guardStrictness   → transient protection depth
 */
class DenoiserDsp final : public AudioProcessStage {
public:
    DenoiserDsp()  = default;
    ~DenoiserDsp() override = default;

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() override;
    int  getLatencySamples() const override { return latencySamples; }
    bool processInPlace(juce::AudioBuffer<float>& buffer,
                        float                    amount,
                        const ProcessOptions&    options) override;

    float getSignalPresence() const noexcept { return signalPresence; }

private:
    // ── STFT constants ────────────────────────────────────────────────────────
    static constexpr int kFftOrder = 10;
    static constexpr int kFftSize  = 1 << kFftOrder;    // 1024
    static constexpr int kHop      = kFftSize / 4;      // 256 (75 % overlap)
    static constexpr int kBins     = kFftSize / 2 + 1;  // 513

    // ── Algorithm constants ───────────────────────────────────────────────────
    static constexpr float kEps      = 1.0e-12f;
    static constexpr float kQAbsence = 0.5f;    // OM-LSA noise prior q
    static constexpr float kGH0      = 0.001f;  // gain under H0 (−60 dB)
    static constexpr float kMsAlpha  = 0.80f;   // Martin noise-frame smoother
    static constexpr float kBmin     = 1.66f;   // Martin bias correction

    // ── STFT infrastructure ───────────────────────────────────────────────────
    vxsuite::RealFft fft;
    int    latencySamples = kFftSize - kHop;
    int    olaAccumSize   = 0;
    double sr             = 48000.0;

    std::vector<float> window;       // sqrt-Hann, length kFftSize
    std::vector<float> olaAcc;       // OLA ring accumulator

    std::vector<float> inFifo;       // input ring buffer, length kFftSize
    std::vector<float> frameBuffer;  // linear snapshot, length kFftSize
    std::vector<float> fftBuf;       // FFT workspace, length kFftSize*2

    int inFifoWritePos = 0;
    int hopFillCount   = 0;
    int olaWritePos    = 0;
    int olaReadPos     = 0;

    // ── Lookup tables (set in prepare, never written on audio thread) ─────────
    std::vector<int>   binToBark;    // Bark band [0..23] per bin
    std::vector<float> phaseAdv;     // expected phase advance per hop per bin
    std::vector<float> erbKernelHW;  // ERB-adaptive smoother half-width (bins)
    std::vector<float> lfStab;       // LF stability weight [0,1]

    std::array<std::vector<int>, 24> barkBins;  // bin indices per Bark band

    // ── Per-bin processing state ──────────────────────────────────────────────
    std::vector<float> currPow;
    std::vector<float> prevMag;
    std::vector<float> tonalness;
    std::vector<float> prevPhaseIn;
    std::vector<float> prevPhaseOut;
    std::vector<float> gainTarget;
    std::vector<float> gainSmooth;
    std::vector<float> gainFreqSmooth;
    std::vector<float> harmonicFloor;
    std::vector<float> barkMaskFloor;
    std::vector<float> humTargetGain;
    std::vector<float> narrowbandTargetGain;
    std::vector<float> narrowbandConfidence;
    std::array<float, 2> humScores {};

    // ── Martin minimum statistics noise estimation ────────────────────────────
    std::vector<vxsuite::spectral::MinStatsBin> msState;
    std::vector<float>       noisePow;   // = Bmin * globalMin per bin
    int msD = 8, msL = 6;               // sub-window count / length (sr-scaled)
    bool firstFrame = true;

    // ── OM-LSA state ─────────────────────────────────────────────────────────
    std::vector<float> xiDD;         // Decision-Directed a priori SNR
    std::vector<float> presenceProb; // smoothed speech-presence probability
    std::vector<float> cleanPowPrev; // estimated clean power, previous frame

    // ── Anti-flicker ─────────────────────────────────────────────────────────
    std::vector<int> suppressCount;  // slow-release suppression counter
    int stftFrameCount = 0;          // monotonic frame index — used for slow-release gating

    // ── Bark transient detection ──────────────────────────────────────────────
    std::array<float, 24> barkFluxAvg {};
    std::array<int,   24> barkHold {};

    // ── Stereo M/S ───────────────────────────────────────────────────────────
    std::vector<float> sideDelayBuf;
    int   sideDelaySize  = 0;
    int   sideDelayWrite = 0;
    int   sideDelayRead  = 0;
    int   sideDelayCount = 0;
    float smoothedSideRatio = 1.0f;
    float prevSideScale     = 1.0f;  // sideScale from previous block — interpolated per-sample

    // Latency-aligned dry mid for side ratio computation
    std::vector<float> midDryDelayBuf;
    int   midDryDelaySize  = 0;
    int   midDryDelayWrite = 0;
    int   midDryDelayRead  = 0;
    int   midDryDelayCount = 0;

    // ── Signal state ─────────────────────────────────────────────────────────
    bool  phaseReady      = false;
    bool  fifoLive        = false; // false after reset() or early-exit; reset STFT on next active call
    float prevFrameEnergy = kEps;
    float attackCoeff     = 0.80f;
    float releaseCoeff    = 0.97f;
    float signalPresence  = 0.5f;

    // ── Scratch (per-block, no audio-thread allocation) ───────────────────────
    std::vector<float> monoOut;

    // ── Internal methods ──────────────────────────────────────────────────────
    void processFrame(float amount, const ProcessOptions& options) noexcept;
    void updateMinStats(int k, float p, float presence) noexcept;
    void applyHumAndNarrowbandSuppression(float amount, const ProcessOptions& options) noexcept;

    static float clamp01(float x) noexcept { return juce::jlimit(0.0f, 1.0f, x); }
    static float safe(float x) noexcept;
};

} // namespace vxsuite::denoiser
