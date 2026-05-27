#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace vxsuite::proximity {

/**
 * Physics-based proximity effect DSP.
 *
 * Models the actual proximity effect of a directional microphone:
 * - Distance-dependent bass boost (cardioid/supercardioid)
 * - Corner frequency determined by distance and mic polar pattern
 * - Three cascaded tonal stages per channel:
 *   1. Low-shelf    — physics-derived proximity bass rise
 *   2. Mud bell     — mild compensation to maintain usability
 *   3. High-shelf   — user-controlled capsule air
 *
 * The proximity parameter (0–1) maps logarithmically to physical distance:
 * - 0.0 = 50 cm (far, arm's length)
 * - 1.0 = 2 cm (very close, on capsule)
 *
 * Designed to be owned by VXProximityAudioProcessor and called from processProduct().
 */
class ProximityDsp {
public:
    void setChannelCount(int numChannels);
    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;

    /**
     * Process buffer in-place.
     *
     * @param buffer        Audio buffer; modified in-place.
     * @param numSamples    Number of valid samples (may be < buffer capacity).
     * @param closerAmount  0–1 physical distance (0=far, 1=very close).
     * @param airAmount     0–1 strength of high-shelf (capsule brightness).
     * @param isVoice       true = Vocal cardioid tuning.
     *                      false = General supercardioid/figure-8 tuning.
     * @param vocalFocus    0–1 content adaptation hint (unused in physics model).
     */
    void processInPlace(juce::AudioBuffer<float>& buffer,
                        int numSamples,
                        float closerAmount,
                        float airAmount,
                        bool isVoice,
                        float vocalFocus = 0.0f) noexcept;

private:
    struct BiquadCoeffs {
        float b0 = 1.f, b1 = 0.f, b2 = 0.f;
        float a1 = 0.f, a2 = 0.f;
    };

    struct BiquadState {
        float w1 = 0.f, w2 = 0.f;
    };

    struct ChannelState {
        BiquadState proximityShelf;
        BiquadState mudBell;
        BiquadState airShelf;
        float analysisLow = 0.f;
        float analysisMid = 0.f;
        float analysisAir = 0.f;
    };

    struct SmoothedModelState {
        float proximityFcHz = 100.f;
        float proximityGainDb = 0.f;
        float mudCenterHz = 300.f;
        float mudGainDb = 0.f;
        float airFcHz = 7600.f;
        float airGainDb = 0.f;
        float outputTrimDb = 0.f;
        bool primed = false;
    };

    static BiquadCoeffs makeLowShelf (double sr, float fcHz, float gainDb) noexcept;
    static BiquadCoeffs makeHighShelf(double sr, float fcHz, float gainDb) noexcept;
    static void         applyBiquad  (const BiquadCoeffs& c, BiquadState& s,
                                      float* data, int numSamples) noexcept;

    std::vector<ChannelState> chans;
    double sr = 48000.0;
    SmoothedModelState smoothedModel;
};

} // namespace vxsuite::proximity
