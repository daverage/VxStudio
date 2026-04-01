#pragma once

#include "../../framework/VxStudioVoiceContext.h"
#include "../../framework/VxStudioVoiceAnalysis.h"
#include "VxPolishTonalAnalysis.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite::polish {

struct AnalysisEvidence {
    float inputEnv = 0.0f;
    float lowMidRatio = 0.0f;
    float presenceRatio = 0.0f;
    float airRatio = 0.0f;
    float mudExcess = 0.0f;
    float harshExcess = 0.0f;
    float sizzleExcess = 0.0f;
    float highTrouble = 0.0f;
    float speechConfidence = 0.0f;
    float artifactRisk = 0.0f;
    float proximityContext = 0.0f;
    float speechLoudnessDb = -120.0f;
    float noiseFloorDb = -96.0f;
};

inline AnalysisEvidence deriveAnalysisEvidence(const TonalAnalysisState& tonal,
                                               const vxsuite::VoiceAnalysisSnapshot& analysis,
                                               const vxsuite::VoiceContextSnapshot& voiceContext) noexcept {
    AnalysisEvidence evidence {};
    evidence.inputEnv = std::max(tonal.inputEnv, 1.0e-6f);
    evidence.lowMidRatio = juce::jlimit(0.0f, 1.5f, tonal.lowMidEnv / evidence.inputEnv);
    evidence.presenceRatio = juce::jlimit(0.0f, 1.5f, tonal.presenceEnv / evidence.inputEnv);
    evidence.airRatio = juce::jlimit(0.0f, 1.5f, tonal.airEnv / evidence.inputEnv);
    evidence.mudExcess = juce::jlimit(0.0f, 1.0f, (evidence.lowMidRatio - 0.20f) / 0.22f);
    evidence.harshExcess = juce::jlimit(0.0f, 1.0f, (evidence.presenceRatio - 0.16f) / 0.20f);
    evidence.sizzleExcess = juce::jlimit(0.0f, 1.0f, (evidence.airRatio - 0.08f) / 0.16f);
    evidence.highTrouble = juce::jlimit(0.0f, 1.0f,
                                        0.55f * evidence.harshExcess
                                      + 0.45f * evidence.sizzleExcess);
    evidence.speechConfidence = juce::jlimit(0.0f, 1.0f,
                                             0.28f * analysis.speechPresence
                                           + 0.18f * analysis.speechStability
                                           + 0.20f * analysis.protectVoice
                                           + 0.12f * voiceContext.speechPresence
                                           + 0.12f * voiceContext.vocalDominance
                                           + 0.10f * voiceContext.intelligibility);
    evidence.artifactRisk = juce::jlimit(0.0f, 1.0f,
                                         0.36f * analysis.transientRisk
                                       + 0.18f * analysis.tailLikelihood
                                       + 0.16f * (1.0f - analysis.speechStability)
                                       + 0.12f * voiceContext.transientRisk
                                       + 0.10f * voiceContext.phraseActivity
                                       + 0.08f * voiceContext.intelligibility);
    evidence.proximityContext = juce::jlimit(0.0f, 1.0f,
                                             0.42f
                                           + 0.18f * (1.0f - analysis.directness)
                                           + 0.18f * voiceContext.centerConfidence
                                           + 0.12f * voiceContext.vocalDominance
                                           + 0.10f * voiceContext.phraseActivity);
    const float speechWeightedEnv = std::max(1.0e-6f,
                                             0.68f * evidence.inputEnv
                                           + 0.32f * std::max(voiceContext.speechBandEnergy, 0.0f));
    evidence.speechLoudnessDb = juce::Decibels::gainToDecibels(speechWeightedEnv, -120.0f);
    evidence.noiseFloorDb = juce::jlimit(-96.0f, -36.0f, tonal.noiseFloorDb);
    return evidence;
}

} // namespace vxsuite::polish
