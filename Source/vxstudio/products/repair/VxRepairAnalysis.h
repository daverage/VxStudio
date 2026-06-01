#pragma once

#include "../../framework/VxStudioFft.h"

#include <atomic>
#include <mutex>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite::repair {

struct RepairAssessment {
    float noiseScore    = 0.0f;  // 0 = clean, 1 = heavy broadband noise
    float reverbScore   = 0.0f;  // 0 = dry,   1 = heavy reverb
    float humMudScore   = 0.0f;  // 0 = clean, 1 = strong hum / low-mid mud

    float suggestedNoiseStrength   = 0.0f;  // pre-set Denoiser knob value
    float suggestedReverbStrength  = 0.0f;  // pre-set Deverb knob value
    float suggestedCleanupStrength = 0.0f;  // pre-set Cleanup knob value

    bool noiseActive   = false;
    bool reverbActive  = false;
    bool cleanupActive = false;

    float confidence = 0.0f;  // 0 = no audio seen, 1 = full 5-second analysis
};

// Collects ~5 seconds of audio and produces a RepairAssessment.
// Call startCollection() from the UI thread, then feed audio via process()
// on the audio thread. Poll isComplete() / getProgress() from the UI thread.
// getAssessment() is safe to call from any thread once isComplete() is true.
class RepairAnalyser {
public:
    static constexpr float kCollectionSeconds = 5.0f;
    static constexpr float kActiveThreshold   = 0.10f;  // enable tool if score >= 10%

    void prepare(double sampleRate, int maxBlockSize);
    void reset();
    void startCollection();
    void process(const juce::AudioBuffer<float>& buffer, int numSamples);

    float getProgress()   const noexcept { return progress.load(std::memory_order_relaxed); }
    bool  isComplete()    const noexcept { return complete.load(std::memory_order_relaxed); }
    bool  isCollecting()  const noexcept { return collecting.load(std::memory_order_relaxed); }
    RepairAssessment getAssessment() const noexcept;
    // Restore a previously saved assessment (e.g. from plugin state) without re-analysing.
    void restoreAssessment(const RepairAssessment& a) noexcept;

    static float scoreToStrengthStatic(float score) noexcept;  // public; calls private impl

private:
    void processFrame() noexcept;
    void finalise()     noexcept;
    static float scoreToStrength(float score) noexcept;

    static constexpr int kFftOrder    = 11;
    static constexpr int kFftSize     = 1 << kFftOrder;    // 2048
    static constexpr int kHop         = 512;
    static constexpr int kBins        = kFftSize / 2 + 1;  // 1025

    // Bin ranges at 48 kHz (bin = freq * kFftSize / sr)
    static constexpr int kHumHiBin    = 15;   // 0–352 Hz
    static constexpr int kLowMidHiBin = 50;   // 352–1172 Hz
    static constexpr int kSpeechHiBin = 290;  // 1172–6797 Hz

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
    std::vector<float> humBandEnergy;
    std::vector<float> speechBandEnergy;
    int framesCollected = 0;

    std::atomic<bool>  collecting { false };
    std::atomic<float> progress   { 0.0f  };
    std::atomic<bool>  complete   { false  };

    mutable std::mutex  assessmentMutex;
    RepairAssessment    assessment;
};

} // namespace vxsuite::repair
