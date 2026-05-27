#pragma once

#include "../../../framework/VxStudioModePolicy.h"
#include "../../../framework/VxStudioProcessOptions.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <vector>

namespace vxsuite::clarity {

struct Params {
    float clean = 0.0f;
    float focus = 0.5f;
    bool voiceMode = false;
    bool sidechainPresent = false;
    float monoScore = 0.0f;
    float compressionScore = 0.0f;
    float tiltScore = 0.0f;
    float separationConfidence = 1.0f;
    float speechFocus = 0.85f;
    float bodyRecovery = 0.65f;
    float guardStrictness = 0.80f;
    float sourceProtect = 0.85f;
    float sidechainNoiseFloorDb = -80.0f;
};

class ClarityDsp {
public:
    static constexpr int kBandCount = 6;
    static constexpr int kCrossCount = kBandCount - 1;

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void process(juce::AudioBuffer<float>& buffer,
                 const juce::AudioBuffer<float>* sidechainBuffer,
                 const Params& params,
                 const ProcessOptions& options = {}) noexcept;

private:
    struct OnePoleLowpass {
        float coeff = 0.0f;
        float z = 0.0f;

        void setCutoff(double sampleRate, float cutoffHz) noexcept;
        float process(float sample) noexcept;
        void reset() noexcept { z = 0.0f; }
    };

    struct ChannelState {
        std::array<OnePoleLowpass, kCrossCount> audioSplits {};
    };

    struct AnalysisState {
        std::array<OnePoleLowpass, kCrossCount> mainSplits {};
        std::array<OnePoleLowpass, kCrossCount> sideSplits {};
        float previousMainSample = 0.0f;
        float previousSideSample = 0.0f;
    };

    void configureFilters(double sampleRate) noexcept;
    void resetFilters() noexcept;
    void analyzeBlock(const juce::AudioBuffer<float>& buffer,
                      const juce::AudioBuffer<float>* sidechainBuffer,
                      const Params& params,
                      std::array<float, kBandCount>& outTargetsDb,
                      float& outSidechainPresence) noexcept;
    void processChannel(size_t channelIndex,
                        float* data,
                        int numSamples,
                        const std::array<float, kBandCount>& bandGains) noexcept;

    static float clamp01(float value) noexcept;
    static float safeRmsFromEnergy(float energy, int samples) noexcept;

    double sr = 48000.0;
    std::vector<ChannelState> channels;
    AnalysisState analysis;
    std::array<float, kBandCount> longDensity {};
    std::array<float, kBandCount> shortConflict {};
    std::array<float, kBandCount> bandGainDb {};
    std::array<float, kBandCount> targetGainDb {};
    float sidechainPresence = 0.0f;
    float lastBlockRms = 0.0f;
    float lastBlockVariance = 0.0f;
};

} // namespace vxsuite::clarity
