#pragma once

#include "VxStudioAnalysisEvidence.h"

#include <cmath>

namespace vxsuite::corrective {

struct ReadabilityGuard {
    float persistentDensity = 0.0f;
    float shortDensity = 0.0f;
    float densityPersistence = 0.0f;
    float selfMaskLowMid = 0.0f;
    float selfMaskHigh = 0.0f;
    float readabilityPriority = 0.0f;
    float bodyPriority = 0.0f;
    float articulationRisk = 0.0f;
    float bodyLossRisk = 0.0f;
    float cumulativeRisk = 0.0f;
    float tonalDriftRisk = 0.0f;
};

inline ReadabilityGuard deriveReadabilityGuard(const AnalysisEvidence& evidence,
                                               const vxsuite::VoiceAnalysisSnapshot& analysis,
                                               const vxsuite::VoiceContextSnapshot& voiceContext,
                                               const float focus,
                                               const bool voiceMode,
                                               const float persistentLowMidDensity,
                                               const float shortLowMidDensity,
                                               const float persistentPresenceDensity,
                                               const float shortPresenceDensity,
                                               const float cleanup,
                                               const float body,
                                               const float deMud,
                                               const float deEss,
                                               const float breath,
                                               const float plosive,
                                               const float troubleSmooth) noexcept {
    ReadabilityGuard guard {};
    guard.persistentDensity = juce::jlimit(0.0f, 1.0f,
        0.68f * persistentLowMidDensity + 0.32f * shortLowMidDensity);
    guard.shortDensity = juce::jlimit(0.0f, 1.0f,
        0.62f * shortLowMidDensity + 0.38f * shortPresenceDensity);
    guard.densityPersistence = juce::jlimit(0.0f, 1.0f,
        1.0f - 1.65f * std::abs(shortLowMidDensity - persistentLowMidDensity)
             - 0.75f * std::abs(shortPresenceDensity - persistentPresenceDensity));

    const float articulationShadow = juce::jlimit(0.0f, 1.0f,
        0.52f * evidence.highTrouble
      + 0.24f * evidence.sizzleExcess
      + 0.14f * (1.0f - voiceContext.intelligibility)
      + 0.10f * analysis.transientRisk);

    guard.selfMaskLowMid = juce::jlimit(0.0f, 1.0f,
        (guard.persistentDensity - evidence.presenceRatio + 0.05f) / 0.24f);
    guard.selfMaskHigh = juce::jlimit(0.0f, 1.0f,
        0.58f * articulationShadow
      + 0.22f * (1.0f - guard.densityPersistence)
      + 0.20f * juce::jlimit(0.0f, 1.0f,
          (guard.shortDensity - evidence.airRatio + 0.04f) / 0.22f));

    guard.readabilityPriority = juce::jlimit(0.0f, 1.0f,
        0.38f + 0.40f * focus
      + 0.14f * (voiceMode ? 1.0f : 0.0f)
      + 0.08f * evidence.speechConfidence);
    guard.bodyPriority = juce::jlimit(0.0f, 1.0f,
        0.52f + 0.22f * body
      + 0.14f * (1.0f - focus)
      + 0.12f * evidence.proximityContext);

    guard.articulationRisk = juce::jlimit(0.0f, 1.0f,
        0.34f * guard.selfMaskLowMid
      + 0.24f * guard.selfMaskHigh
      + 0.18f * (1.0f - voiceContext.intelligibility)
      + 0.14f * voiceContext.buriedSpeech
      + 0.10f * analysis.transientRisk);

    guard.bodyLossRisk = juce::jlimit(0.0f, 1.0f,
        0.42f * evidence.mudExcess
      + 0.20f * (1.0f - guard.densityPersistence)
      + 0.18f * guard.selfMaskLowMid
      + 0.10f * (1.0f - guard.bodyPriority)
      + 0.10f * (voiceMode ? 0.15f : 0.0f));

    const float stageLoad = juce::jlimit(0.0f, 1.0f,
        0.24f * cleanup
      + 0.16f * deMud
      + 0.16f * deEss
      + 0.14f * breath
      + 0.12f * plosive
      + 0.18f * troubleSmooth);
    const float overlapLoad = juce::jlimit(0.0f, 1.0f,
        0.22f * (deMud * deEss + deEss * troubleSmooth + breath * troubleSmooth + plosive * deMud)
      + 0.16f * (deMud * breath + deEss * plosive + deEss * breath));
    guard.cumulativeRisk = juce::jlimit(0.0f, 1.0f,
        0.42f * stageLoad
      + 0.26f * overlapLoad
      + 0.20f * guard.articulationRisk
      + 0.12f * guard.bodyLossRisk);

    guard.tonalDriftRisk = juce::jlimit(0.0f, 1.0f,
        0.36f * evidence.highTrouble
      + 0.20f * evidence.sizzleExcess
      + 0.16f * deEss
      + 0.14f * troubleSmooth
      + 0.08f * breath
      + 0.06f * juce::jlimit(0.0f, 1.0f, deMud - guard.persistentDensity));

    return guard;
}

} // namespace vxsuite::corrective
