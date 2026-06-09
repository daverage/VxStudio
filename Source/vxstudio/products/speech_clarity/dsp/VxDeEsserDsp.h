#pragma once

#include "VxCleanupMode.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>

namespace vxsuite {
namespace speech_clarity {

// Dual-band de-esser.
// Detects lower (4.2–6.5 kHz) and upper (6.5–10.5 kHz) sibilance bands
// against a body reference (700–3500 Hz), then subtracts only the excess.
class DeEsserDsp {
public:
    struct Params {
        float strength = 0.0f;            // 0-1: max reduction
        float detectionIntensity = 0.0f;  // 0-1: upstream sibilance likelihood
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void setMode(CleanupMode newMode) noexcept;
    void process(juce::AudioBuffer<float>& buffer, const Params& params);

    float getLastLowerReductionDb() const noexcept { return lastLowerReductionDb; }
    float getLastUpperReductionDb() const noexcept { return lastUpperReductionDb; }

private:
    struct BiquadState {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;
    };

    struct ChannelState {
        // Body-band filter chain (HP at 700 Hz → LP at 3500 Hz)
        BiquadState bodyHp;
        BiquadState bodyLp;

        // Lower sibilance band (HP at 4200 Hz → LP at 6500 Hz)
        BiquadState lowerHp;
        BiquadState lowerLp;

        // Upper sibilance band (HP at 6500 Hz → LP at 10500 Hz)
        BiquadState upperHp;
        BiquadState upperLp;

        // Envelopes
        float bodyEnv  = 0.0f;
        float lowerEnv = 0.0f;
        float upperEnv = 0.0f;

        // Gains (1.0 = no reduction)
        float lowerGain = 1.0f;
        float upperGain = 1.0f;
    };

    static void  designHighPass (double sr, float hz, float q, BiquadState& s) noexcept;
    static void  designLowPass  (double sr, float hz, float q, BiquadState& s) noexcept;
    static float processBiquad  (float x, BiquadState& s) noexcept;
    static float clamp01        (float x) noexcept;
    static float smoothStep     (float edge0, float edge1, float x) noexcept;

    void updateModeCoefficients();
    void redesignFilters();

    std::vector<ChannelState> channels;

    CleanupMode mode       = CleanupMode::Speech;
    double      sampleRate = 48000.0;

    float envAttCoeff  = 0.0f;
    float envRelCoeff  = 0.0f;

    float gainAttCoeff = 0.0f;
    float gainRelCoeff = 0.0f;

    // Mode-dependent reduction limits and score thresholds
    float lowerMaxDb       = -7.0f;
    float upperMaxDb       = -10.0f;
    float lowerScoreLow    = 0.35f;
    float lowerScoreHigh   = 1.20f;
    float upperScoreLow    = 0.30f;
    float upperScoreHigh   = 1.10f;
    float lowerGenModeBias = 1.0f;  // 0.75 in General mode

    // Metering
    float lastLowerReductionDb = 0.0f;
    float lastUpperReductionDb = 0.0f;
};

} // namespace speech_clarity
} // namespace vxsuite
