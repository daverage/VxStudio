#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace vxsuite::proximity {

/**
 * Zero-latency proximity effect DSP.
 *
 * Two cascaded biquad shelves per channel:
 *   1. Low-shelf  — simulates bass buildup from close mic placement (proximity effect)
 *   2. High-shelf — adds upper presence/air that characterises a close placement
 *
 * Both shelves are parameterised by two scalar amounts (0–1) and a voice/general flag.
 * When either amount is zero the corresponding filter collapses to the identity, so
 * bypass transparency is guaranteed.
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
     * @param closerAmount  0–1 strength of low-shelf (proximity effect).
     * @param airAmount     0–1 strength of high-shelf (presence/air).
     * @param isVoice       true = Vocal tuning (lower fc, vocal presence band).
     *                      false = General tuning (wider fc range, upper air).
     */
    void processInPlace(juce::AudioBuffer<float>& buffer,
                        int numSamples,
                        float closerAmount,
                        float airAmount,
                        bool isVoice) noexcept;

private:
    struct BiquadCoeffs {
        float b0 = 1.f, b1 = 0.f, b2 = 0.f;
        float a1 = 0.f, a2 = 0.f;
    };

    struct BiquadState {
        float w1 = 0.f, w2 = 0.f;
    };

    struct ChannelState {
        BiquadState lowShelf;
        BiquadState highShelf;
    };

    static BiquadCoeffs makeLowShelf (double sr, float fcHz, float gainDb) noexcept;
    static BiquadCoeffs makeHighShelf(double sr, float fcHz, float gainDb) noexcept;
    static void         applyBiquad  (const BiquadCoeffs& c, BiquadState& s,
                                      float* data, int numSamples) noexcept;

    std::vector<ChannelState> chans;
    double sr = 48000.0;
};

} // namespace vxsuite::proximity
