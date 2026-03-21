#pragma once

#include "../../../framework/VxSuiteVoiceAnalysis.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite::leveler {

struct DetectorSnapshot {
    float speechPresence = 0.0f;
    float speechDominance = 0.0f;
    float instrumentDominance = 0.0f;
    float buriedSpeech = 0.0f;
    float brightness = 0.0f;
    float transientStrength = 0.0f;
    float stereoSpread = 0.0f;
};

class Detector final {
public:
    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    DetectorSnapshot analyse(const juce::AudioBuffer<float>& input,
                             const vxsuite::VoiceAnalysisSnapshot& voiceAnalysis);

private:
    static float onePoleCoeff(double sampleRate, float cutoffHz) noexcept;
    static float timeAlpha(double sampleRate, float seconds) noexcept;

    double sr = 48000.0;
    int maxBlock = 0;

    float low150 = 0.0f;
    float low2000 = 0.0f;
    float low4000 = 0.0f;
    float envFullFast = 0.0f;
    float envFullSlow = 0.0f;
    float envLow = 0.0f;
    float envSpeech = 0.0f;
    float envHigh = 0.0f;
    float avgAbsDiff = 0.0f;
    float prevMono = 0.0f;
    float smoothStereoSpread = 0.0f;

    float smoothSpeechPresence = 0.0f;
    float smoothSpeechDominance = 0.0f;
    float smoothInstrumentDominance = 0.0f;
    float smoothBuriedSpeech = 0.0f;
    float smoothBrightness = 0.0f;
    float smoothTransient = 0.0f;

    float coeff150 = 0.0f;
    float coeff2000 = 0.0f;
    float coeff4000 = 0.0f;
    float alphaFast = 0.0f;
    float alphaSlow = 0.0f;
    float alphaBand = 0.0f;
    float alphaScore = 0.0f;
};

} // namespace vxsuite::leveler
