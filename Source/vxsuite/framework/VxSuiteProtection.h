#pragma once

#include "VxSuiteModePolicy.h"
#include "VxSuiteVoiceAnalysis.h"

namespace vxsuite {

struct ProtectionSnapshot {
    float protectVoice = 0.0f;
    float speechConfidence = 0.0f;
    float tailFocus = 0.0f;
    float transientCaution = 0.0f;
    float stereoProtect = 0.0f;
};

inline ProtectionSnapshot deriveProtection(const ModePolicy& modePolicy,
                                           const VoiceAnalysisSnapshot& analysis) noexcept {
    ProtectionSnapshot protection;

    const float speechConfidence = juce::jlimit(0.0f,
                                                1.0f,
                                                0.45f * analysis.speechPresence
                                                    + 0.30f * analysis.speechStability
                                                    + 0.25f * analysis.centerConfidence);
    const float tailFocus = juce::jlimit(0.0f,
                                         1.0f,
                                         0.55f * analysis.tailLikelihood
                                             + 0.25f * (1.0f - analysis.directness)
                                             + 0.20f * (1.0f - analysis.transientRisk));
    const float transientCaution = juce::jlimit(0.0f,
                                                1.0f,
                                                0.65f * analysis.transientRisk
                                                    + 0.35f * (1.0f - analysis.speechStability));

    protection.speechConfidence = speechConfidence;
    protection.tailFocus = tailFocus;
    protection.transientCaution = transientCaution;
    protection.stereoProtect = juce::jlimit(0.0f,
                                            1.0f,
                                            0.55f * modePolicy.stereoWidthProtect
                                                + 0.45f * analysis.centerConfidence);
    protection.protectVoice = juce::jlimit(0.0f,
                                           1.0f,
                                           0.50f * analysis.protectVoice
                                               + 0.25f * speechConfidence
                                               + 0.25f * modePolicy.sourceProtect
                                               - 0.20f * tailFocus
                                               + 0.10f * transientCaution);
    return protection;
}

} // namespace vxsuite
