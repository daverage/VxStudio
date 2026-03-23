#pragma once

#include "VxSuiteVoiceAnalysis.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite {

struct VoiceContextSnapshot {
    float speechPresence = 0.0f;
    float speechStability = 0.0f;
    float speechBandEnergy = 0.0f;
    float centerConfidence = 0.0f;
    float transientRisk = 0.0f;
    float vocalDominance = 0.0f;
    float buriedSpeech = 0.0f;
    float phraseActivity = 0.0f;
    float phraseStart = 0.0f;
    float phraseEnd = 0.0f;
    float intelligibility = 0.0f;
};

class VoiceContextState {
public:
    void prepare(double sampleRate, int maxSamplesPerBlock);
    void reset();
    void update(const juce::AudioBuffer<float>& input, const VoiceAnalysisSnapshot& analysis);
    VoiceContextSnapshot snapshot() const noexcept { return current; }

private:
    static float onePoleAlpha(double sampleRate, float cutoffHz) noexcept;
    static float timeAlpha(double sampleRate, float seconds) noexcept;

    double sr = 48000.0;
    float low150 = 0.0f;
    float low2000 = 0.0f;
    float low4000 = 0.0f;
    float envFull = 0.0f;
    float envSpeech = 0.0f;
    float envHigh = 0.0f;
    float envLow = 0.0f;
    float phraseEnv = 0.0f;
    bool speechActive = false;

    float coeff150 = 0.0f;
    float coeff2000 = 0.0f;
    float coeff4000 = 0.0f;
    float alphaBand = 0.0f;
    float alphaPhrase = 0.0f;
    float alphaScore = 0.0f;

    VoiceContextSnapshot current;
};

} // namespace vxsuite
