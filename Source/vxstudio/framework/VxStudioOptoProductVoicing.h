#pragma once

#include "VxStudioFinishDsp.h"
#include "VxStudioModePolicy.h"
#include "VxStudioProcessOptions.h"
#include "VxStudioVoiceContext.h"

#include <juce_core/juce_core.h>

namespace vxsuite::opto {

enum class ProductVariant {
    standalone,
    intelligentFinish
};

enum class BehaviourMode {
    autoFollowProgram = 0,
    compress = 1,
    limit = 2
};

struct RenderConfig {
    ProcessOptions options {};
    finish::Dsp::Params dspParams {};
};

inline RenderConfig buildRenderConfig(const ProductVariant variant,
                                      const bool voiceMode,
                                      const float primaryAmount,
                                      const float bodyAmount,
                                      const float gainAmount,
                                      const BehaviourMode behaviourMode,
                                      const bool stereoLink,
                                      const ModePolicy& policy,
                                      const VoiceContextSnapshot& voiceContext) noexcept {
    RenderConfig config {};

    const float vocalPriority = voiceMode
        ? vxsuite::clamp01(0.38f * voiceContext.vocalDominance
                         + 0.26f * voiceContext.intelligibility
                         + 0.18f * voiceContext.phraseActivity
                         + 0.10f * voiceContext.speechPresence
                         + 0.08f * voiceContext.centerConfidence)
        : 0.0f;

    const bool isStandalone = variant == ProductVariant::standalone;
    const float outputGainRangeDb = isStandalone ? 9.0f : 12.0f;

    config.options.isVoiceMode = voiceMode;
    config.options.sourceProtect = voiceMode
        ? vxsuite::clamp01((isStandalone ? 0.64f : 0.62f)
                         + (isStandalone ? 0.36f : 0.38f) * bodyAmount
                         + (isStandalone ? 0.12f : 0.10f) * vocalPriority)
        : vxsuite::clamp01((isStandalone ? 0.30f : 0.28f)
                         + (isStandalone ? 0.50f : 0.52f) * bodyAmount);
    config.options.guardStrictness = voiceMode
        ? vxsuite::clamp01((isStandalone ? 0.60f : 0.58f)
                         + (isStandalone ? 0.40f : 0.42f) * bodyAmount
                         + (isStandalone ? 0.15f : 0.12f) * vocalPriority)
        : vxsuite::clamp01((isStandalone ? 0.35f : 0.33f)
                         + (isStandalone ? 0.50f : 0.52f) * bodyAmount);
    config.options.speechFocus = voiceMode
        ? juce::jmax(isStandalone ? 0.75f : 0.73f, policy.speechFocus + 0.13f * vocalPriority)
        : juce::jmax(isStandalone ? 0.20f : 0.18f, policy.speechFocus);
    config.options.voiceProtect = voiceMode ? (isStandalone ? 0.85f : 0.82f)
                                            : (isStandalone ? 0.60f : 0.58f);
    config.options.lateTailAggression = policy.lateTailAggression;

    int contentMode = voiceMode ? 0 : 1;
    if (behaviourMode == BehaviourMode::compress)
        contentMode = 0;
    else if (behaviourMode == BehaviourMode::limit)
        contentMode = 1;

    config.dspParams.contentMode = contentMode;
    config.dspParams.peakReduction = vxsuite::clamp01(primaryAmount
                                * (voiceMode
                                    ? ((isStandalone ? 1.05f : 1.0f)
                                       - (isStandalone ? 0.08f : 0.10f) * vocalPriority
                                       + (isStandalone ? 0.04f : 0.06f) * voiceContext.buriedSpeech)
                                    : (isStandalone ? 1.08f : 1.0f)));
    config.dspParams.outputGainDb = juce::jmap(gainAmount, 0.0f, 1.0f, -outputGainRangeDb, outputGainRangeDb);
    config.dspParams.body = bodyAmount;
    config.dspParams.stereoLink = stereoLink;

    return config;
}

} // namespace vxsuite::opto
