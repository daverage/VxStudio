#pragma once

#include "VxCleanupMode.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <array>
#include <cmath>
#include <cstdint>

namespace vxsuite {
namespace speech_clarity {

// Two-lane click repair: HardClick (short ticks/edits) and MouthClick (lip/saliva/wet mouth).
// Uses lookahead interpolation, not gain ducking.
class DeClickDsp {
public:
    struct Params {
        float strength = 0.0f;            // 0-1 repair amount/aggressiveness
        float detectionIntensity = 1.0f;  // 0-1 upstream hint/sensitivity
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset() noexcept;
    void setMode(CleanupMode newMode) noexcept;
    int getLatencySamples() const noexcept;

    float getLastHardClickRepair() const noexcept  { return lastHardClickRepair; }
    float getLastMouthClickRepair() const noexcept { return lastMouthClickRepair; }
    int   getRecentRepairCount() const noexcept    { return recentRepairCount; }

    void process(juce::AudioBuffer<float>& buffer, const Params& params);

private:
    struct BiquadState {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;
    };

    struct LaneState {
        bool  candidateActive = false;
        int64_t candidateStart = -1;
        int   candidateLength = 0;
        bool  pendingRepair = false;
        int64_t pendingStart = -1;
        int64_t pendingEnd = -1;
        int   cooldown = 0;
    };

    struct ChannelState {
        std::vector<float> ring;

        // Hard-click lane filters/envelopes
        BiquadState hardHp;
        float hardFastEnv = 0.0f;
        float hardSlowEnv = 0.0f;
        float hardSlopeFast = 0.0f;
        float hardSlopeSlow = 0.0f;
        float hardPrevX = 0.0f;

        // Mouth-click lane filters/envelopes
        BiquadState mouthHp;
        float mouthFastEnv = 0.0f;
        float mouthSlowEnv = 0.0f;

        // Low-band guard
        BiquadState lowLp;
        float lowEnv = 0.0f;
        float fullEnv = 0.0f;

        LaneState hard;
        LaneState mouth;

        int writeIndex = 0;
        int64_t absoluteIndex = 0;
        int64_t lastRepairStart = -1;
        int64_t lastRepairEnd = -1;
    };

    static float clamp01(float x) noexcept;
    static float smoothStep(float edge0, float edge1, float x) noexcept;
    static void  designHighPass(double sr, float hz, float q, BiquadState& s) noexcept;
    static void  designLowPass (double sr, float hz, float q, BiquadState& s) noexcept;
    static float processBiquad (float x, BiquadState& s) noexcept;
    static float boundedRepairInterp(float y0, float y1, float y2, float y3, float t) noexcept;
    static float median9       (std::array<float, 9>& values) noexcept;

    float getRingSample(const ChannelState& c, int64_t idx) const noexcept;
    void  setRingSample(ChannelState& c, int64_t idx, float value) noexcept;

    bool repairRegion(ChannelState& c, int64_t start, int64_t end,
                      float amount, bool longBlend) noexcept;

    void processLane(LaneState& lane, int64_t curAbs, float score,
                     float startThresh, float continueThresh,
                     int maxSamples, int minSamples, int cooldownSamp) noexcept;

    static inline float hardCoeff(float env, float rect, float coeff) noexcept {
        return coeff * env + (1.0f - coeff) * rect;
    }

    void updateModeCoefficients();
    void redesignFilters();

    std::vector<ChannelState> channels;

    CleanupMode mode = CleanupMode::Speech;

    double sampleRate    = 48000.0;
    int    ringSize      = 0;
    int    lookaheadSamples = 0;

    // Hard-click lane settings
    float hardHpCutoffHz  = 2200.0f;
    int   hardMaxSamples  = 0;
    int   hardPreMargin   = 0;
    int   hardPostMargin  = 0;
    int   hardCooldown    = 0;
    float hardFastCoeff   = 0.0f;
    float hardSlowCoeff   = 0.0f;
    float hardSlopeFast   = 0.0f;
    float hardSlopeSlow   = 0.0f;

    // Mouth-click lane settings
    float mouthHpCutoffHz = 1200.0f;
    int   mouthMaxSamples = 0;
    int   mouthMinSamples = 0;
    int   mouthPreMargin  = 0;
    int   mouthPostMargin = 0;
    int   mouthCooldown   = 0;
    float mouthFastCoeff  = 0.0f;
    float mouthSlowCoeff  = 0.0f;

    // Low-band guard cutoff
    float lowLpCutoffHz = 300.0f;

    // Metering (non-atomic — audio-thread only, UI polls periodically)
    float lastHardClickRepair  = 0.0f;
    float lastMouthClickRepair = 0.0f;
    int   recentRepairCount    = 0;
};

} // namespace speech_clarity
} // namespace vxsuite
