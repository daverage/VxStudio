#include "VxRebalanceConfidence.h"

#include <algorithm>
#include <cmath>

namespace vxsuite::rebalance::ml {

namespace {

inline float clamp01(const float value) noexcept {
    return juce::jlimit(0.0f, 1.0f, value);
}

inline float modeScale(const Dsp::RecordingType recordingType) noexcept {
    switch (recordingType) {
        case Dsp::RecordingType::live: return 0.88f;
        case Dsp::RecordingType::phoneRough: return 0.74f;
        case Dsp::RecordingType::studio:
            break;
    }
    return 1.0f;
}

} // namespace

void ConfidenceTracker::prepare(const double sampleRate) noexcept {
    sampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    reset();
}

void ConfidenceTracker::reset() noexcept {
    smoothedConfidence = 0.0f;
}

float ConfidenceTracker::update(const FeatureSnapshot& features,
                                const Dsp::RecordingType recordingType,
                                const vxsuite::SignalQualitySnapshot& signalQuality) noexcept {
    const float crestConfidence = clamp01((features.crest - 1.25f) / 4.5f);
    const float rmsConfidence = clamp01(features.rms / 0.12f);
    const float stereoConfidence = clamp01(1.0f - 0.65f * features.monoScore);
    const float qualityConfidence = clamp01(signalQuality.separationConfidence);
    const float target = clamp01(modeScale(recordingType)
        * (0.46f * qualityConfidence
           + 0.22f * crestConfidence
           + 0.18f * rmsConfidence
           + 0.14f * stereoConfidence));

    const float hopSeconds = static_cast<float>(Dsp::kHopSize) / static_cast<float>(std::max(1000.0, sampleRateHz));
    const float settleSeconds = recordingType == Dsp::RecordingType::phoneRough ? 0.18f : 0.12f;
    const float alpha = std::exp(-hopSeconds / settleSeconds);
    smoothedConfidence = alpha * smoothedConfidence + (1.0f - alpha) * target;
    return smoothedConfidence;
}

} // namespace vxsuite::rebalance::ml
