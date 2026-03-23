#pragma once

#include "../../../framework/VxSuiteProduct.h"
#include "VxLevelerDetector.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace vxsuite::leveler {

class Dsp final {
public:
    enum class MixState {
        neutral,
        voiceLeading,
        guitarDominant,
        voiceBuried
    };

    struct Params final {
        float level = 0.0f;
        float control = 0.0f;
        bool voiceMode = false;
    };

    struct MixDecision final {
        float levelBias = 0.0f;
        float speechLift = 0.0f;
        float transientTame = 0.0f;
    };

    struct Tuning final {
        float mixTargetBlendBase = 0.309803f;
        float mixTargetBlendLevelWeight = 0.239203f;
        float mixDeadbandBase = 1.03139f;
        float mixDeadbandLevelWeight = 0.16f;
        float mixNormalizeShortThresholdBase = 0.518176f;
        float mixNormalizeShortThresholdLevelWeight = 0.12f;
        float mixNormalizeBaselineThresholdBase = 0.443115f;
        float mixNormalizeBaselineThresholdLevelWeight = 0.08f;
        float mixNormalizeShortScaleBase = 0.540052f;
        float mixNormalizeShortScaleLevelWeight = 0.18f;
        float mixNormalizeBaselineScaleBase = 0.785077f;
        float mixNormalizeBaselineScaleLevelWeight = 0.14f;
        float mixNormalizeMaxDb = 5.51886f;
        float mixNormalizeSpikePenalty = 0.259307f;
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void setParams(const Params& p) noexcept { params = p; }
    void setTuning(const Tuning& t) noexcept { tuning = t; }
    void reset();
    void process(juce::AudioBuffer<float>& buffer, const DetectorSnapshot& detector);
    [[nodiscard]] int latencySamples() const noexcept { return delaySamples; }

    float getLiftActivity() const noexcept { return liftActivity; }
    float getLevelActivity() const noexcept { return levelActivity; }
    float getTameActivity() const noexcept { return tameActivity; }

private:
    struct ChannelState {
        float lp150 = 0.0f;
        float lp2000 = 0.0f;
        float lp4000 = 0.0f;
    };

    static float lowpassCoeff(double sampleRate, float cutoffHz) noexcept;
    static float timeCoeff(double sampleRate, float seconds) noexcept;
    static float stepToward(float current, float target, float maxDelta) noexcept;
    static MixState detectState(const DetectorSnapshot& detector) noexcept;
    static MixDecision decide(MixState state,
                              const DetectorSnapshot& detector,
                              float level,
                              float control) noexcept;
    static MixDecision blendDecision(const MixDecision& a,
                                     const MixDecision& b,
                                     float amount) noexcept;

    Params params {};
    Tuning tuning {};
    double sr = 48000.0;
    std::vector<ChannelState> channels;
    std::vector<float> delayLine;
    int delaySamples = 0;
    int delayWriteIndex = 0;

    float coeff150 = 0.0f;
    float coeff2000 = 0.0f;
    float coeff4000 = 0.0f;

    // Preserved vocal engine state.
    float levelEnv = 0.0f;
    float anchorEnv = 0.0f;
    float highEnv = 0.0f;
    float levellerGain = 1.0f;
    float overrideGain = 1.0f;
    float speechLiftGain = 1.0f;
    float overrideLiftGain = 1.0f;
    float highTameGain = 1.0f;
    float overrideTameGain = 1.0f;
    float vocalPhraseAnchor = 0.0f;
    MixState activeState = MixState::neutral;
    MixState targetState = MixState::neutral;
    float stateTransition = 1.0f;

    // Separate general loudness engine state.
    float generalMomentary = 0.0f;
    float generalShort = 0.0f;
    float generalBaseline = 0.0f;
    float generalWetShort = 0.0f;
    float generalWetBaseline = 0.0f;
    float generalRideGainDb = 0.0f;
    float generalNormalizeGainDb = 0.0f;
    float generalSpikeGain = 1.0f;
    float generalHighEnv = 0.0f;

    float liftActivity = 0.0f;
    float levelActivity = 0.0f;
    float tameActivity = 0.0f;
};

} // namespace vxsuite::leveler
