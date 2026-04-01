#include "VxStudioVoiceContext.h"

#include <cmath>

namespace vxsuite {

namespace {

inline float clamp01(const float x) noexcept {
    return juce::jlimit(0.0f, 1.0f, x);
}

} // namespace

float VoiceContextState::onePoleAlpha(const double sampleRate, const float cutoffHz) noexcept {
    if (sampleRate <= 0.0 || cutoffHz <= 0.0f)
        return 0.0f;
    return std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz / static_cast<float>(sampleRate));
}

float VoiceContextState::timeAlpha(const double sampleRate, const float seconds) noexcept {
    const float srSafe = static_cast<float>(sampleRate > 1000.0 ? sampleRate : 48000.0);
    const float samples = std::max(1.0f, seconds * srSafe);
    return std::exp(-1.0f / samples);
}

void VoiceContextState::prepare(const double sampleRate, const int /*maxSamplesPerBlock*/) {
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    coeff150 = onePoleAlpha(sr, 150.0f);
    coeff2000 = onePoleAlpha(sr, 2000.0f);
    coeff4000 = onePoleAlpha(sr, 4000.0f);
    alphaBand = timeAlpha(sr, 0.080f);
    alphaPhrase = timeAlpha(sr, 0.350f);
    alphaScore = timeAlpha(sr, 0.180f);
    reset();
}

void VoiceContextState::reset() {
    low150 = 0.0f;
    low2000 = 0.0f;
    low4000 = 0.0f;
    envFull = 0.0f;
    envSpeech = 0.0f;
    envHigh = 0.0f;
    envLow = 0.0f;
    phraseEnv = 0.0f;
    speechActive = false;
    current = {};
}

void VoiceContextState::update(const juce::AudioBuffer<float>& input, const VoiceAnalysisSnapshot& analysis) {
    const int channels = input.getNumChannels();
    const int samples = input.getNumSamples();
    if (channels <= 0 || samples <= 0) {
        current = {};
        return;
    }

    float fullAccum = 0.0f;
    float speechAccum = 0.0f;
    float highAccum = 0.0f;
    float lowAccum = 0.0f;

    for (int i = 0; i < samples; ++i) {
        float mono = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            mono += input.getSample(ch, i);
        mono /= static_cast<float>(channels);

        low150 = coeff150 * low150 + (1.0f - coeff150) * mono;
        low2000 = coeff2000 * low2000 + (1.0f - coeff2000) * mono;
        low4000 = coeff4000 * low4000 + (1.0f - coeff4000) * mono;

        const float speechBand = low4000 - low150;
        const float highBand = mono - low2000;

        fullAccum += std::abs(mono);
        speechAccum += std::abs(speechBand);
        highAccum += std::abs(highBand);
        lowAccum += std::abs(low150);
    }

    const float invSamples = 1.0f / static_cast<float>(samples);
    const float full = fullAccum * invSamples;
    const float speech = speechAccum * invSamples;
    const float high = highAccum * invSamples;
    const float low = lowAccum * invSamples;

    envFull = alphaBand * envFull + (1.0f - alphaBand) * full;
    envSpeech = alphaBand * envSpeech + (1.0f - alphaBand) * speech;
    envHigh = alphaBand * envHigh + (1.0f - alphaBand) * high;
    envLow = alphaBand * envLow + (1.0f - alphaBand) * low;

    const float safeFull = std::max(envFull, 1.0e-5f);
    const float speechShare = clamp01(envSpeech / safeFull);
    const float highShare = clamp01(envHigh / (envSpeech + envHigh + 1.0e-5f));
    const float lowShare = clamp01(envLow / safeFull);

    const float vocalDominance = clamp01(0.42f * analysis.speechPresence
                                       + 0.24f * analysis.speechStability
                                       + 0.18f * speechShare
                                       + 0.08f * analysis.centerConfidence
                                       + 0.08f * (1.0f - highShare));
    const float buriedSpeech = clamp01(analysis.speechPresence
                                     * clamp01((0.58f - speechShare) / 0.32f
                                             + 0.30f * highShare
                                             + 0.26f * lowShare
                                             + 0.14f * analysis.transientRisk));
    const float intelligibility = clamp01(0.40f * analysis.speechBandEnergy
                                        + 0.30f * analysis.speechStability
                                        + 0.20f * (1.0f - analysis.tailLikelihood)
                                        + 0.10f * analysis.centerConfidence);

    const bool nowSpeechActive = analysis.speechPresence > 0.40f || speechShare > 0.22f;
    float phraseStart = 0.0f;
    float phraseEnd = 0.0f;
    if (!speechActive && nowSpeechActive)
        phraseStart = 1.0f;
    if (speechActive && !nowSpeechActive)
        phraseEnd = 1.0f;
    speechActive = nowSpeechActive;

    phraseEnv = alphaPhrase * phraseEnv + (1.0f - alphaPhrase) * (nowSpeechActive ? 1.0f : 0.0f);

    current.speechPresence += (1.0f - alphaScore) * (analysis.speechPresence - current.speechPresence);
    current.speechStability += (1.0f - alphaScore) * (analysis.speechStability - current.speechStability);
    current.speechBandEnergy += (1.0f - alphaScore) * (analysis.speechBandEnergy - current.speechBandEnergy);
    current.centerConfidence += (1.0f - alphaScore) * (analysis.centerConfidence - current.centerConfidence);
    current.transientRisk += (1.0f - alphaScore) * (analysis.transientRisk - current.transientRisk);
    current.vocalDominance += (1.0f - alphaScore) * (vocalDominance - current.vocalDominance);
    current.buriedSpeech += (1.0f - alphaScore) * (buriedSpeech - current.buriedSpeech);
    current.intelligibility += (1.0f - alphaScore) * (intelligibility - current.intelligibility);
    current.phraseActivity += (1.0f - alphaScore) * (phraseEnv - current.phraseActivity);
    current.phraseStart += (1.0f - alphaScore) * (phraseStart - current.phraseStart);
    current.phraseEnd += (1.0f - alphaScore) * (phraseEnd - current.phraseEnd);
}

} // namespace vxsuite
