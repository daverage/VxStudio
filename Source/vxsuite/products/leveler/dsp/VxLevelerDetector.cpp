#include "VxLevelerDetector.h"

#include <algorithm>
#include <cmath>

namespace vxsuite::leveler {

namespace {

inline float clamp01(const float x) noexcept {
    return juce::jlimit(0.0f, 1.0f, x);
}

} // namespace

void Detector::prepare(const double sampleRate, const int maxBlockSize) {
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    maxBlock = std::max(1, maxBlockSize);
    coeff150 = onePoleCoeff(sr, 150.0f);
    coeff2000 = onePoleCoeff(sr, 2000.0f);
    coeff4000 = onePoleCoeff(sr, 4000.0f);
    alphaFast = timeAlpha(sr, 0.010f);
    alphaSlow = timeAlpha(sr, 0.120f);
    alphaBand = timeAlpha(sr, 0.070f);
    alphaScore = timeAlpha(sr, 0.160f);
    reset();
}

void Detector::reset() {
    low150 = 0.0f;
    low2000 = 0.0f;
    low4000 = 0.0f;
    envFullFast = 0.0f;
    envFullSlow = 0.0f;
    envLow = 0.0f;
    envSpeech = 0.0f;
    envHigh = 0.0f;
    avgAbsDiff = 0.0f;
    prevMono = 0.0f;
    smoothSpeechPresence = 0.0f;
    smoothSpeechDominance = 0.0f;
    smoothInstrumentDominance = 0.0f;
    smoothBuriedSpeech = 0.0f;
    smoothBrightness = 0.0f;
    smoothTransient = 0.0f;
    smoothStereoSpread = 0.0f;
}

DetectorSnapshot Detector::analyse(const juce::AudioBuffer<float>& input,
                                   const vxsuite::VoiceAnalysisSnapshot& voiceAnalysis) {
    const int numChannels = input.getNumChannels();
    const int numSamples = input.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return {};

    float blockDiffAccum = 0.0f;
    double midEnergy = 0.0;
    double sideEnergy = 0.0;
    for (int i = 0; i < numSamples; ++i) {
        float mono = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            mono += input.getSample(ch, i);
        mono /= static_cast<float>(numChannels);
        if (numChannels >= 2) {
            const float left = input.getSample(0, i);
            const float right = input.getSample(1, i);
            const float mid = 0.5f * (left + right);
            const float side = 0.5f * (left - right);
            midEnergy += static_cast<double>(mid) * mid;
            sideEnergy += static_cast<double>(side) * side;
        }

        low150 = coeff150 * low150 + (1.0f - coeff150) * mono;
        low2000 = coeff2000 * low2000 + (1.0f - coeff2000) * mono;
        low4000 = coeff4000 * low4000 + (1.0f - coeff4000) * mono;

        const float speechBand = low4000 - low150;
        const float highBand = mono - low2000;

        const float absMono = std::abs(mono);
        const float absLow = std::abs(low150);
        const float absSpeech = std::abs(speechBand);
        const float absHigh = std::abs(highBand);
        const float absDiff = std::abs(mono - prevMono);
        prevMono = mono;

        envFullFast = alphaFast * envFullFast + (1.0f - alphaFast) * absMono;
        envFullSlow = alphaSlow * envFullSlow + (1.0f - alphaSlow) * absMono;
        envLow = alphaBand * envLow + (1.0f - alphaBand) * absLow;
        envSpeech = alphaBand * envSpeech + (1.0f - alphaBand) * absSpeech;
        envHigh = alphaBand * envHigh + (1.0f - alphaBand) * absHigh;
        blockDiffAccum += absDiff;
    }

    const float avgDiff = blockDiffAccum / static_cast<float>(numSamples);
    avgAbsDiff = 0.8f * avgAbsDiff + 0.2f * avgDiff;

    const float fullEnv = std::max(envFullSlow, 1.0e-5f);
    const float lowShare = clamp01(envLow / (fullEnv + 1.0e-5f));
    const float speechShare = clamp01(envSpeech / (fullEnv + 1.0e-5f));
    const float brightness = clamp01(envHigh / (envSpeech + envHigh + 1.0e-5f));
    const float transientFromEnv = clamp01((envFullFast - envFullSlow) / (fullEnv + 1.0e-5f));
    const float transientFromDiff = clamp01(avgAbsDiff / (fullEnv + 1.0e-5f) * 0.5f);
    const float transient = clamp01(0.55f * transientFromEnv
                                  + 0.25f * transientFromDiff
                                  + 0.20f * voiceAnalysis.transientRisk);
    const float stereoSpread = numChannels >= 2
        ? clamp01(static_cast<float>(std::sqrt(sideEnergy / std::max(1.0e-9, midEnergy + sideEnergy))))
        : 0.0f;

    const float speechPresence = clamp01(0.40f * voiceAnalysis.speechPresence
                                       + 0.25f * voiceAnalysis.protectVoice
                                       + 0.20f * voiceAnalysis.speechBandEnergy
                                       + 0.15f * clamp01((speechShare - 0.10f) / 0.35f));

    const float speechDominance = clamp01(0.42f * speechPresence
                                        + 0.18f * voiceAnalysis.speechStability
                                        + 0.18f * speechShare
                                        + 0.12f * (1.0f - brightness)
                                        + 0.10f * voiceAnalysis.directness
                                        - 0.10f * lowShare
                                        - 0.20f * transient);

    const float instrumentDominance = clamp01(0.24f * brightness
                                            + 0.24f * transient
                                            + 0.24f * lowShare
                                            + 0.16f * clamp01(avgAbsDiff / (fullEnv + 1.0e-5f))
                                            + 0.12f * (1.0f - speechPresence));

    const float buriedSpeech = clamp01(speechPresence
                                     * clamp01((instrumentDominance - speechDominance + 0.26f) / 0.72f)
                                     * clamp01((0.52f - speechShare) / 0.28f
                                             + 0.35f * brightness
                                             + 0.45f * lowShare
                                             + 0.20f));

    smoothSpeechPresence += (1.0f - alphaScore) * (speechPresence - smoothSpeechPresence);
    smoothSpeechDominance += (1.0f - alphaScore) * (speechDominance - smoothSpeechDominance);
    smoothInstrumentDominance += (1.0f - alphaScore) * (instrumentDominance - smoothInstrumentDominance);
    smoothBuriedSpeech += (1.0f - alphaScore) * (buriedSpeech - smoothBuriedSpeech);
    smoothBrightness += (1.0f - alphaScore) * (brightness - smoothBrightness);
    smoothTransient += (1.0f - alphaScore) * (transient - smoothTransient);
    smoothStereoSpread += (1.0f - alphaScore) * (stereoSpread - smoothStereoSpread);

    DetectorSnapshot snapshot;
    snapshot.speechPresence = smoothSpeechPresence;
    snapshot.speechDominance = smoothSpeechDominance;
    snapshot.instrumentDominance = smoothInstrumentDominance;
    snapshot.buriedSpeech = smoothBuriedSpeech;
    snapshot.brightness = smoothBrightness;
    snapshot.transientStrength = smoothTransient;
    snapshot.stereoSpread = smoothStereoSpread;
    return snapshot;
}

float Detector::onePoleCoeff(const double sampleRate, const float cutoffHz) noexcept {
    const float srSafe = static_cast<float>(sampleRate > 1000.0 ? sampleRate : 48000.0);
    return std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz / srSafe);
}

float Detector::timeAlpha(const double sampleRate, const float seconds) noexcept {
    const float srSafe = static_cast<float>(sampleRate > 1000.0 ? sampleRate : 48000.0);
    const float samples = std::max(1.0f, seconds * srSafe);
    return std::exp(-1.0f / samples);
}

} // namespace vxsuite::leveler
