#pragma once

#include "../../../framework/VxStudioAudioProcessStage.h"
#include "../../../framework/VxStudioFft.h"
#include "../../../framework/VxStudioSpectralHelpers.h"

#include <array>
#include <atomic>
#include <thread>
#include <vector>
#include <juce_dsp/juce_dsp.h>

namespace vxsuite::denoiser {

/**
 * Self-contained spectral denoiser  -  VX Suite framework.
 *
 * ── Algorithm ────────────────────────────────────────────────────────────────
 * OM-LSA (Cohen 2003) with Decision-Directed a priori SNR estimation.
 * Martin (2001) minimum statistics blind noise floor tracking, with
 * IMCRA-style presence-weighted smoothing alpha.
 *
 * Per-frame extras:
 *   • Bark-domain transient detection (24 bands, flux ratio, 3-frame hold)
 *   • Harmonic comb protection (voice mode  -  locks gain across harmonics)
 *   • ERB-adaptive frequency smoothing (variable kernel: 1–10 bins)
 *   • Phase-vocoder synthesis (std::remainder wrapPi  -  no loop drift)
 *
 * ── Stereo ───────────────────────────────────────────────────────────────────
 * M/S processing: mid is denoised, side is latency-aligned and energy-scaled.
 * Side gain ratio is one-pole smoothed (τ ≈ 200 ms) to prevent per-block
 * pumping artifacts.
 *
 * ── STFT ─────────────────────────────────────────────────────────────────────
 *   FFT:      1024 points
 *   Hop:       256 samples (75 % overlap)
 *   Window:   sqrt-Hann (WOLA  -  applied at both analysis and synthesis)
 *   Latency:  fftSize samples ≈ 21 ms @ 48 kHz (one hop added for async buffer)
 *
 * ── ProcessOptions wiring ────────────────────────────────────────────────────
 *   isVoiceMode       → enables harmonic comb protection + LF stability
 *   sourceProtect     → harmonic floor gain (more protect = higher floor)
 *   lateTailAggression → suppression strength scalar
 *   guardStrictness   → transient protection depth
 *
 * ── Realtime notes ───────────────────────────────────────────────────────────
 * STFT frames are processed synchronously at hop boundaries so OLA output is
 * sample-aligned with the plugin latency reported to the host.
 */
class DenoiserDsp final : public AudioProcessStage {
public:
    DenoiserDsp()  = default;
    ~DenoiserDsp() override;

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void resetFifoState();  // Lightweight reset: clears STFT state without resetting noise floor (phrase boundary sync)
    int  getLatencySamples() const override { return latencySamples; }
    bool processInPlace(juce::AudioBuffer<float>& buffer,
                        float                    amount,
                        const ProcessOptions&    options) override;
    bool drain(juce::AudioBuffer<float>& buffer) noexcept override;

    float getSignalPresence() const noexcept { return signalPresence.load(std::memory_order_relaxed); }
    float getNoiseFloorDb() const noexcept   { return noiseFloorDb.load(std::memory_order_relaxed); }
    float getGainReductionDb() const noexcept { return smoothedGrDb.load(std::memory_order_relaxed); }

private:
    [[nodiscard]] bool hasValidProcessingState(int numChannels, int numSamples) const noexcept;

    // ── STFT dimensions (computed in prepare() — 1024@44.1/48k, 2048@88.2/96k) ─
    int kFftOrder = 10;
    int kFftSize  = 1024;
    int kHop      = 256;
    int kBins     = 513;

    // ── Algorithm constants ───────────────────────────────────────────────────
    static constexpr float kEps      = 1.0e-12f;
    static constexpr float kQAbsence = 0.5f;    // OM-LSA noise prior q
    static constexpr float kGH0      = 0.001f;  // gain under H0 (−60 dB)
    static constexpr float kMsAlpha  = 0.80f;   // Martin noise-frame smoother
    static constexpr float kBmin     = 1.66f;   // Martin bias correction

    // ── STFT infrastructure ───────────────────────────────────────────────────
    vxsuite::RealFft fft;
    int    latencySamples = 0;
    int    olaAccumSize   = 0;
    double sr             = 48000.0;

    std::vector<float> window;       // sqrt-Hann, length kFftSize
    std::vector<float> olaAcc;       // OLA ring accumulator (audio thread only)

    std::vector<float> inFifo;       // input ring buffer, length kFftSize (audio thread only)
    std::vector<float> frameBuffer;  // linear snapshot for SPSC push, length kFftSize
    std::vector<float> fftBuf;       // FFT workspace, length kFftSize*2 (inference thread only)

    int inFifoWritePos = 0;
    int hopFillCount   = 0;
    int olaWritePos    = 0;  // audio thread only — advanced when OLA frames arrive
    int olaReadPos     = 0;  // audio thread only

    // ── Lookup tables (set in prepare, never written on audio thread) ─────────
    std::vector<int>   binToBark;    // Bark band [0..23] per bin
    std::vector<float> phaseAdv;     // expected phase advance per hop per bin
    std::vector<float> erbKernelHW;  // ERB-adaptive smoother half-width (bins)
    std::vector<float> lfStab;       // LF stability weight [0,1]

    std::array<std::vector<int>, 24> barkBins;  // bin indices per Bark band

    // ── Per-bin processing state (inference thread only after prepare) ────────
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

    // ── Martin minimum statistics noise estimation (inference thread only) ────
    std::vector<vxsuite::spectral::MinStatsBin> msState;
    std::vector<float>       noisePow;   // = Bmin * globalMin per bin
    int msD = 8, msL = 6;               // sub-window count / length (sr-scaled)
    bool firstFrame = true;

    // ── OM-LSA state (inference thread only) ─────────────────────────────────
    std::vector<float> xiDD;         // Decision-Directed a priori SNR
    std::vector<float> presenceProb; // smoothed speech-presence probability
    std::vector<float> cleanPowPrev; // estimated clean power, previous frame

    // ── Anti-flicker (inference thread only) ─────────────────────────────────
    std::vector<int> suppressCount;  // slow-release suppression counter
    int stftFrameCount = 0;          // monotonic frame index  -  used for slow-release gating

    // ── Bark transient detection (inference thread only) ─────────────────────
    std::array<float, 24> barkFluxAvg {};
    std::array<int,   24> barkHold {};

    // ── Stereo M/S (audio thread only) ───────────────────────────────────────
    std::vector<float> sideDelayBuf;
    int   sideDelaySize  = 0;
    int   sideDelayWrite = 0;
    int   sideDelayRead  = 0;
    int   sideDelayCount = 0;
    float smoothedSideRatio = 1.0f;
    float prevSideScale     = 1.0f;

    // Latency-aligned dry mid for side ratio computation (audio thread only)
    std::vector<float> midDryDelayBuf;
    int   midDryDelaySize  = 0;
    int   midDryDelayWrite = 0;
    int   midDryDelayRead  = 0;
    int   midDryDelayCount = 0;

    // ── Signal state ─────────────────────────────────────────────────────────
    // Atomics: written by inference thread, read by audio thread via getters
    std::atomic<float> smoothedGrDb   { 0.0f };
    std::atomic<float> signalPresence { 0.5f };
    std::atomic<float> noiseFloorDb   { -80.0f };

    bool  phaseReady      = false;
    bool  fifoLive        = false;
    bool  suppressionActivePrev = false;
    float prevFrameEnergy = kEps;
    float attackCoeff     = 0.80f;
    float releaseCoeff    = 0.97f;

    // ── Scratch (per-block, no audio-thread allocation) ───────────────────────
    std::vector<float> monoOut;
    std::vector<float> olaOutputFrame;  // scratch for popping output frames

    // ── Async inference thread ────────────────────────────────────────────────
    // SPSC lock-free FIFO. push() writer-thread only; pop() reader-thread only.
    struct AsyncFifo {
        juce::AbstractFifo fifo { 1 };
        std::vector<float> storage;
        void reset(int capacity);
        void clear();
        int  available() const noexcept { return fifo.getNumReady(); }
        void push(const float* data, int count);
        void pop(float* dest, int count);
    };

    AsyncFifo inputSnapshotFifo;  // audio writes ordered kFftSize-frame; inference reads
    AsyncFifo outputFrameFifo;    // inference writes IFFT-windowed frame; audio OLA-accumulates

    std::thread           inferenceThread;
    std::atomic<int>      workSignal    { 0 };   // 0 = idle, 1 = frame(s) ready
    std::atomic<bool>     stopInference { false };

    // Params: written by audio thread before signalling, read by inference thread
    std::atomic<float> asyncAmount             { 0.0f };
    std::atomic<bool>  asyncIsVoiceMode        { false };
    std::atomic<float> asyncSpeechFocus        { 0.5f };
    std::atomic<float> asyncLateTailAggression { 0.5f };
    std::atomic<float> asyncSourceProtect      { 0.5f };
    std::atomic<float> asyncGuardStrictness    { 0.5f };

    bool isPrepared = false;  // true once prepare() completes; guards thread restart in reset()

    // ── Internal methods ──────────────────────────────────────────────────────
    // frameIn: kFftSize ordered samples (oldest first). outputFrame: kFftSize windowed IFFT output.
    void processFrame(const float* frameIn, float amount, const ProcessOptions& options,
                      float* outputFrame) noexcept;
    void updateMinStats(int k, float p, float presence) noexcept;
    void applyHumAndNarrowbandSuppression(float amount, const ProcessOptions& options) noexcept;

    void drainOutputFrames() noexcept;  // audio thread: pop ready frames from outputFrameFifo, OLA-accumulate
    void runInferenceLoop();
    void stopInferenceThread();

    static float clamp01(float x) noexcept { return juce::jlimit(0.0f, 1.0f, x); }
    static float safe(float x) noexcept;
};

} // namespace vxsuite::denoiser
