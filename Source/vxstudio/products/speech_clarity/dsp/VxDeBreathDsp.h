#pragma once

#include "VxCleanupMode.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

namespace vxsuite {
namespace speech_clarity {

// Region-state-machine breath attenuator.
// Requires a confirmed minimum breath duration before applying any reduction.
class DeBreathDsp {
public:
    struct Params {
        float strength = 0.0f;            // 0-1: max breath reduction
        float detectionIntensity = 0.0f;  // 0-1: upstream breath likelihood
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void setMode(CleanupMode newMode) noexcept;
    void process(juce::AudioBuffer<float>& buffer, const Params& params);

    float getLastReductionDb() const noexcept { return lastReductionDb; }
    float getLastBreathScore() const noexcept { return lastBreathScore; }
    bool  isBreathActive()     const noexcept { return anyChannelActive; }

private:
    enum class BreathState { Idle, Candidate, Active, Release };

    struct BiquadState {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;
    };

    struct ChannelState {
        BiquadState breathHp;
        BiquadState breathLp;
        BiquadState lowLp;

        float fullEnv        = 0.0f;
        float breathEnv      = 0.0f;
        float lowEnv         = 0.0f;
        float speechRefEnv   = 0.0f;

        BreathState state      = BreathState::Idle;
        int candidateSamples   = 0;
        int activeSamples      = 0;
        int releaseSamples     = 0;
        float regionScore      = 0.0f;
        float currentGain      = 1.0f;
    };

    static void  designHighPass(double sr, float hz, float q, BiquadState& s) noexcept;
    static void  designLowPass (double sr, float hz, float q, BiquadState& s) noexcept;
    static float processBiquad (float x, BiquadState& s) noexcept;
    static float clamp01       (float x) noexcept;
    static float smoothStep    (float edge0, float edge1, float x) noexcept;

    void updateModeCoefficients();

    std::vector<ChannelState> channels;

    CleanupMode mode = CleanupMode::Speech;
    double      sampleRate = 48000.0;

    // Envelope followers
    float envAttCoeff = 0.0f;
    float envRelCoeff = 0.0f;

    // Gain smoothing
    float gainAttCoeff = 0.0f;
    float gainRelCoeff = 0.0f;

    // Speech reference (slow tracker)
    float speechRefAttCoeff = 0.0f;
    float speechRefRelCoeff = 0.0f;

    // Mode-dependent timing/limits (computed in updateModeCoefficients)
    int   minBreathSamples           = 0;
    int   maxBreathSamples           = 0;
    float candidateStartThreshold    = 0.55f;
    float candidateContinueThreshold = 0.30f;
    float maxReductionDb             = -15.0f;

    // Metering
    float lastReductionDb  = 0.0f;
    float lastBreathScore  = 0.0f;
    bool  anyChannelActive = false;
};

} // namespace speech_clarity
} // namespace vxsuite
