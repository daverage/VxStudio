#pragma once

#include "VxCleanupMode.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

namespace vxsuite {
namespace speech_clarity {

// Dynamic-HPF plosive controller.
// Detects low/mid ratio, then crossfades toward a stronger HPF only during the burst.
// Does not broadband duck.
class DePolosiveDsp {
public:
    struct Params {
        float strength = 0.0f;            // 0-1: max low-band reduction
        float detectionIntensity = 0.0f;  // 0-1: upstream plosive likelihood
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void setMode(CleanupMode newMode) noexcept;
    void process(juce::AudioBuffer<float>& buffer, const Params& params);

    float getLastReductionDb()   const noexcept { return lastReductionDb; }
    float getLastPlosiveScore()  const noexcept { return lastPlosiveScore; }

private:
    struct BiquadState {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;
    };

    struct ChannelState {
        // Detector filters
        BiquadState detLp;   // low-band detector LP at ~180 Hz
        BiquadState detBpHp; // mid-band: HP stage at ~250 Hz
        BiquadState detBpLp; // mid-band: LP stage at ~1500 Hz

        // Processing filters (normal and strong HPF)
        BiquadState normalHpf;
        BiquadState strongHpf;

        // Envelopes
        float lowEnv  = 0.0f;
        float midEnv  = 0.0f;
        float lowOnsetEnv = 0.0f;

        // Smoothed score and crossfade
        float score   = 0.0f;
        float xfade   = 0.0f;

        // Hold counter
        int holdCounter = 0;

        // Smoothed strong HPF cutoff (Hz), updated per control block
        float currentStrongHz = 140.0f;
    };

    static void  designHighPass(double sr, float hz, float q, BiquadState& s) noexcept;
    static void  designLowPass (double sr, float hz, float q, BiquadState& s) noexcept;
    static float processBiquad (float x, BiquadState& s) noexcept;
    static float clamp01       (float x) noexcept;
    static float smoothStep    (float edge0, float edge1, float x) noexcept;

    void updateModeCoefficients();
    void redesignProcessingFilters();

    std::vector<ChannelState> channels;

    CleanupMode mode       = CleanupMode::Speech;
    double      sampleRate = 48000.0;

    // Detector envelope timing
    float attCoeff = 0.0f;
    float relCoeff = 0.0f;

    // Onset fast tracker
    float onsetAttCoeff = 0.0f;

    // Gain/xfade smoothing
    float xfadeAttCoeff = 0.0f;
    float xfadeRelCoeff = 0.0f;

    // Mode-dependent processing settings
    float normalHpfHz    = 70.0f;
    float strongHpfMinHz = 140.0f;
    float strongHpfMaxHz = 260.0f;

    // Hold time in samples
    int holdSamples = 0;

    // Metering
    float lastReductionDb  = 0.0f;
    float lastPlosiveScore = 0.0f;
};

} // namespace speech_clarity
} // namespace vxsuite
