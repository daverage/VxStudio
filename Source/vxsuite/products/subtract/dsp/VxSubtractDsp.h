#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "../../../framework/VxSuiteAudioProcessStage.h"
#include "../../../framework/VxSuiteFft.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite::subtract {

class SubtractDsp : public vxsuite::AudioProcessStage {
public:
    SubtractDsp() = default;
    ~SubtractDsp() override = default;

    void prepare(double sampleRate, int maxBlockSize) override;
    void reset() override;
    void resetStreamingState();
    bool finalizeLearnedProfile();
    void clearLearnedProfile();
    int  getLatencySamples() const override { return static_cast<int>(fftSize - hop); }

    void  setLearning(bool shouldLearn) { learning = shouldLearn; }
    bool  isLearning()  const { return learning; }
    bool  hasLearnedProfile() const { return learnedProfileReady; }
    float getLearnProgress() const;
    float getLearnObservedSeconds() const;
    float getLearnConfidence() const noexcept { return learnedProfileConfidence; }

    bool getLearnedProfileData(std::vector<float>& outProfile, float& outConfidence) const;
    void restoreLearnedProfile(const std::vector<float>& profile, float confidence);

    bool  processInPlace(juce::AudioBuffer<float>&, float amount,
                         const vxsuite::ProcessOptions&) override;

    float lastSpeechProbability() const { return signalPresenceAvg; }

private:
    static constexpr size_t fftOrder = 11;
    static constexpr size_t fftSize  = (1u << fftOrder);
    static constexpr size_t hop      = fftSize / 4;
    static constexpr size_t bins     = fftSize / 2 + 1;

    vxsuite::RealFft fft;

    double sr                  = 48000.0;
    int    maxBlockSizePrepared = 1;
    float  signalPresenceAvg   = 0.5f;

    bool learning            = false;
    bool learningPrev        = false;
    bool learnedProfileReady = false;
    float learnedSensitivity = 0.0f;
    float learnedProfileConfidence = 0.0f;
    float learnQualityAccum = 0.0f;
    int learnQualityFrames = 0;

    std::vector<float> monoIn, monoOut, alignedMidDry;
    std::vector<float> frame, frameBuffer, window, olaAcc;
    std::vector<float> currPow, prevMag, tonalnessByBin;
    std::vector<float> erbFloor;
    std::vector<float> barkMaskFloor;
    std::vector<float> harmonicFloor;
    std::vector<float> lowBandStability;
    std::vector<float> prevInputPhase;
    std::vector<float> prevOutputPhase;
    std::vector<float> phaseAdvance;
    std::vector<int> binToBark;
    std::array<std::vector<size_t>, 24> barkBandBins;
    std::array<float, 24> barkFluxAvg {};
    std::array<int, 24> barkTransientHold {};

    // Manual learn
    std::vector<float> noisePowFrozen;
    std::vector<float> learnAccum;
    std::vector<float> learnAccumSq;
    std::vector<std::vector<float>> learnHistory;
    int learnFrames = 0, learnTargetFrames = 0;

    // Martin (2001) minimum statistics
    static constexpr float MS_alpha = 0.80f;
    static constexpr float Bmin     = 1.66f;
    int minStatsD = 8;
    int minStatsL = 6;

    struct MinStatsBin {
        float smoothPow = 1.0e-8f, subWinMin = 1.0e-8f, globalMin = 1.0e-8f;
        int   frameCount = 0, subWinIdx = 0;
        std::vector<float> subWindows;
    };

    std::vector<MinStatsBin> msState;
    std::vector<float>       noisePowBlind;

    // OM-LSA
    static constexpr float q_absence = 0.5f;
    static constexpr float gH0_val   = 0.001f;

    std::vector<float> xiDD, presenceProb, cleanPowPrev;

    // Anti-flicker
    std::vector<int> binSuppressCount;

    // Gain buffers
    std::vector<float> gainTarget, gainSmooth, gainSmoothedFreq;

    // Audio ring FIFOs
    std::vector<float> inQueue, outQueue;
    size_t inQueueCap  = 0;
    size_t inRead  = 0, inWrite  = 0, inCount  = 0;
    size_t outQueueCap = 0;
    size_t outRead = 0, outWrite = 0, outCount = 0;

    // Stereo side-delay alignment for M/S reconstruction.
    std::vector<float> sideDelay;
    size_t sideDelayCap = 0;
    size_t sideDelayRead = 0, sideDelayWrite = 0, sideDelayCount = 0;

    // Mid dry delay for latency-aligned energy/delta calculation.
    std::vector<float> midDryDelay;
    size_t midDryDelayCap = 0;
    size_t midDryDelayRead = 0, midDryDelayWrite = 0, midDryDelayCount = 0;

    static constexpr int maxExtraChannels = 6;
    struct ExtraChannelDelay {
        std::vector<float> buffer;
        size_t readPos = 0;
        size_t writePos = 0;
        size_t available = 0;
    };
    std::array<ExtraChannelDelay, maxExtraChannels> extraChannelDelays;

    // Transient detector
    float prevFrameEnergy = 1.0e-8f;

    float attackCoeff = 0.80f, releaseCoeff = 0.97f;
    bool phaseHistoryReady = false;

    void  pushInputSample (float x);
    bool  popInputSample  (float& x);
    void  pushOutputSample(float x);
    bool  popOutputSample (float& x);

    void  setQueueSizes        (int maxBlockSize);
    void  updateSmoothingCoeffs();
    void  updateMinStats       (size_t k, float p, float presenceHint);
    float activeNoise          (size_t k) const;

    static float safe(float x);
};

} // namespace vxsuite::subtract
