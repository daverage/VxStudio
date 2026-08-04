#pragma once

#include "../../framework/VxStudioFft.h"

#include <array>
#include <atomic>
#include <mutex>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite::repair {

struct RepairAssessment {
    float noiseScore    = 0.0f;  // 0 = clean, 1 = heavy broadband noise
    float reverbScore   = 0.0f;  // 0 = dry,   1 = heavy reverb
    float humMudScore   = 0.0f;  // 0 = clean, 1 = strong hum / low-mid mud
    float clickScore    = 0.0f;  // 0 = clean, 1 = frequent clicks / transient artifacts

    float suggestedNoiseStrength   = 0.0f;  // pre-set Denoiser knob value
    float suggestedReverbStrength  = 0.0f;  // pre-set Deverb knob value
    float suggestedCleanupStrength = 0.0f;  // pre-set Cleanup knob value
    float suggestedClickStrength   = 0.0f;  // pre-set Click knob value

    bool noiseActive   = false;
    bool reverbActive  = false;
    bool cleanupActive = false;
    bool clickActive   = false;

    float confidence = 0.0f;  // 0 = no audio seen, 1 = full 5-second analysis
};

// Collects ~5 seconds of audio and produces a RepairAssessment.
// startCollection() and process() are realtime-safe (audio thread). When the
// collection window fills, process() flags finalise as pending; the heavy
// scoring pass (sorts, vector copies, assessment lock) runs when the message
// thread calls finaliseIfPending(). Poll isComplete() / getProgress() from the
// UI thread. getAssessment() takes a lock — never call it from the audio
// thread; the audio thread reads finalNoiseScore() instead.
class RepairAnalyser {
public:
    static constexpr float kCollectionSeconds = 5.0f;
    static constexpr float kActiveThreshold   = 0.10f;  // enable tool if score >= 10%

    void prepare(double sampleRate, int maxBlockSize);
    void reset();
    // phase2NoiseScoreOverride >= 0 makes finalise() replace the measured noise
    // score with this value (Phase 2 runs on denoised audio, so its own noise
    // measurement is meaningless and the Phase 1 score is merged in instead).
    void startCollection(float phase2NoiseScoreOverride = -1.0f);
    void process(const juce::AudioBuffer<float>& buffer, int numSamples);
    // Message thread only: runs the deferred finalise pass if one is pending.
    void finaliseIfPending();

    float getProgress()   const noexcept { return progress.load(std::memory_order_relaxed); }
    bool  isComplete()    const noexcept { return complete.load(std::memory_order_acquire); }
    bool  isCollecting()  const noexcept { return collecting.load(std::memory_order_acquire); }
    // Audio-thread-safe read of the finalised noise score (valid once isComplete()).
    float finalNoiseScore() const noexcept { return lastNoiseScore.load(std::memory_order_relaxed); }
    RepairAssessment getAssessment() const noexcept;
    // Restore a previously saved assessment (e.g. from plugin state) without re-analysing.
    void restoreAssessment(const RepairAssessment& a) noexcept;

    static float scoreToStrengthStatic(float score) noexcept;  // public; calls private impl

    // Live spectrum snapshot for display during collection (24 log-spaced bands, 0..1 normalised).
    // Safe to call from the UI thread at any time.
    static constexpr int kDisplayBands = 24;
    void getDisplayBands(std::array<float, kDisplayBands>& out) const noexcept;

private:
    void processFrame() noexcept;
    void finalise();
    static float scoreToStrength(float score) noexcept;

    static constexpr int kFftOrder    = 11;
    static constexpr int kFftSize     = 1 << kFftOrder;    // 2048
    static constexpr int kHop         = 512;
    static constexpr int kBins        = kFftSize / 2 + 1;  // 1025

    // Bin ranges at 48 kHz (bin = freq * kFftSize / sr)
    // Speech reference band: 1–4 kHz  (bins 43–171)
    // Sibilance band:        4.5–9 kHz (bins 192–384)
    static constexpr int kSpeechLoBin  = 43;   // ~1 kHz
    static constexpr int kSpeechHiBin2 = 171;  // ~4 kHz
    static constexpr int kSibLoBin     = 192;  // ~4.5 kHz
    static constexpr int kSibHiBin     = 384;  // ~9 kHz

    RealFft fft;
    double  sr            = 48000.0;
    int     targetFrames  = 0;

    std::vector<float> window;
    std::vector<float> inFifo;
    std::vector<float> fftBuf;
    int fifoWritePos = 0;
    int hopFill      = 0;

    // Per-frame stats accumulated during collection
    std::vector<float> frameRms;
    std::vector<float> sibBandRatio;   // sibilance-band / speech-band energy ratio per frame
    std::vector<float> frameCrestDb;   // 20*log10(peak/rms) per frame for click detection
    int framesCollected = 0;

    std::atomic<bool>  collecting      { false };
    std::atomic<float> progress        { 0.0f  };
    std::atomic<bool>  complete        { false };
    std::atomic<bool>  finalisePending { false };
    std::atomic<float> lastNoiseScore  { 0.0f  };
    float noiseScoreOverride = -1.0f;  // written before collecting=true, read by finalise()

    mutable std::mutex  assessmentMutex;
    RepairAssessment    assessment;

    // Display spectrum  -  updated non-blockingly in processFrame, read by UI thread.
    mutable std::mutex spectrumMutex;
    std::array<float, kDisplayBands> displayBands {};
    float spectrumPeakHold = 1.0e-6f;  // slow-decaying peak for normalisation
};

} // namespace vxsuite::repair
