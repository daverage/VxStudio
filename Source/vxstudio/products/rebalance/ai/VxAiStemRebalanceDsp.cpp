#include "VxAiStemRebalanceDsp.h"

#include <algorithm>
#include <cmath>

namespace vxsuite::rebalance::ai {

namespace {
constexpr int kStrengthIndex = 5;
constexpr double kModelSampleRate = 44100.0;

float clamp01(const float value) noexcept {
    return juce::jlimit(0.0f, 1.0f, value);
}

float lerp(const float a, const float b, const float t) noexcept {
    return a + (b - a) * t;
}
} // namespace

void StemRebalanceDsp::prepare(const double sampleRate, const int, const int numChannels) {
    sampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    preparedChannels = juce::jlimit(1, kStemFrameChannels, numChannels);
    for (int i = 0; i < kControlCount; ++i) {
        const float defaultValue = i == kStrengthIndex ? 1.0f : 0.5f;
        targetControlValues[static_cast<size_t>(i)] = defaultValue;
        currentControlValues[static_cast<size_t>(i)] = defaultValue;
        controlSmoothers[static_cast<size_t>(i)].reset(sampleRateHz, i == kStrengthIndex ? 0.025 : 0.018);
        controlSmoothers[static_cast<size_t>(i)].setCurrentAndTargetValue(defaultValue);
    }
    reset();
}

void StemRebalanceDsp::reset() {
    stemQueue = {};
    queueReadIndex = 0;
    queueWriteIndex = 0;
    queueCount = 0;
    lastAcceptedSequence = 0;
    renderedSequence = 0;
    frameReadPosition = 0.0;
    boundaryFadeRemaining = 0;
    for (float& sample : lastOutputSample)
        sample = 0.0f;
    for (float& sample : boundaryStartSample)
        sample = 0.0f;
    debugSnapshot = {};
}

void StemRebalanceDsp::setControlTargets(const std::array<float, kControlCount>& normalizedValues) {
    for (int i = 0; i < kControlCount; ++i)
        targetControlValues[static_cast<size_t>(i)] = normalizedValues[static_cast<size_t>(i)];
}

void StemRebalanceDsp::setRecordingType(const RecordingType newType) noexcept {
    recordingType = newType;
}

void StemRebalanceDsp::setStemFrame(const StemFrame& frame) noexcept {
    if (!frame.available || frame.sequenceNumber == 0 || frame.sequenceNumber <= lastAcceptedSequence)
        return;

    stemQueue[static_cast<size_t>(queueWriteIndex)] = frame;
    queueWriteIndex = (queueWriteIndex + 1) % kStemQueueSize;
    if (queueCount < kStemQueueSize) {
        ++queueCount;
    } else {
        queueReadIndex = (queueReadIndex + 1) % kStemQueueSize;
        frameReadPosition = 0.0;
    }
    lastAcceptedSequence = frame.sequenceNumber;
}

StemRebalanceDsp::DebugSnapshot StemRebalanceDsp::getDebugSnapshot() const noexcept {
    return debugSnapshot;
}

void StemRebalanceDsp::process(juce::AudioBuffer<float>& buffer) {
    const int numSamples = buffer.getNumSamples();
    const int numChannels = std::min(buffer.getNumChannels(), preparedChannels);
    if (numSamples <= 0 || numChannels <= 0)
        return;

    if (!hasUsableFrame()) {
        for (int sample = 0; sample < numSamples; ++sample) {
            for (int ch = 0; ch < std::min(buffer.getNumChannels(), kStemFrameChannels); ++ch) {
                lastOutputSample[ch] = buffer.getSample(ch, sample);
            }
        }
        return;
    }

    for (int i = 0; i < kControlCount; ++i)
        controlSmoothers[static_cast<size_t>(i)].setTargetValue(targetControlValues[static_cast<size_t>(i)]);

    const double sourceAdvance = kModelSampleRate / sampleRateHz;

    for (int sample = 0; sample < numSamples; ++sample) {
        while (queueCount > 0 && frameReadPosition >= static_cast<double>(kStemFrameSamples)) {
            frameReadPosition -= static_cast<double>(kStemFrameSamples);
            queueReadIndex = (queueReadIndex + 1) % kStemQueueSize;
            --queueCount;
            for (int ch = 0; ch < kStemFrameChannels; ++ch)
                boundaryStartSample[ch] = lastOutputSample[ch];
            boundaryFadeRemaining = kBoundaryFadeSamples;
        }

        if (queueCount <= 0) {
            for (int ch = 0; ch < numChannels; ++ch) {
                const float dry = buffer.getSample(ch, sample);
                const float value = boundaryFadeRemaining > 0
                    ? lerp(lastOutputSample[ch], dry,
                           1.0f - static_cast<float>(boundaryFadeRemaining) / static_cast<float>(kBoundaryFadeSamples))
                    : dry;
                lastOutputSample[ch] = value;
                buffer.setSample(ch, sample, value);
            }
            if (boundaryFadeRemaining > 0)
                --boundaryFadeRemaining;
            continue;
        }

        const auto& activeFrame = stemQueue[static_cast<size_t>(queueReadIndex)];
        renderedSequence = activeFrame.sequenceNumber;
        const float trust = activeFrame.confidence * recordingTrustScale();

        for (int i = 0; i < kControlCount; ++i)
            currentControlValues[static_cast<size_t>(i)] = controlSmoothers[static_cast<size_t>(i)].getNextValue();

        const int index0 = juce::jlimit(0, kStemFrameSamples - 1, static_cast<int>(std::floor(frameReadPosition)));
        const int index1 = std::min(index0 + 1, kStemFrameSamples - 1);
        const float frac = static_cast<float>(frameReadPosition - std::floor(frameReadPosition));

        const float strength = currentControlValues[static_cast<size_t>(kStrengthIndex)];
        std::array<float, kSourceCount> gains {};
        for (int source = 0; source < kSourceCount; ++source) {
            const float sliderSigned = (currentControlValues[static_cast<size_t>(source)] - 0.5f) * 2.0f;
            const float targetGain = juce::jlimit(0.0f, 2.0f, 1.0f + sliderSigned * strength);
            const float changeDepth = targetGain < 1.0f
                ? 1.0f
                : trust * sourcePreserveBlend(source);
            gains[static_cast<size_t>(source)] = lerp(1.0f, targetGain, juce::jlimit(0.0f, 1.0f, changeDepth));
        }

        const bool applyBoundaryFade = boundaryFadeRemaining > 0;
        const float boundaryFade = applyBoundaryFade
            ? 1.0f - static_cast<float>(boundaryFadeRemaining) / static_cast<float>(kBoundaryFadeSamples)
            : 1.0f;

        for (int ch = 0; ch < numChannels; ++ch) {
            const auto& mix = activeFrame.mixture[static_cast<size_t>(ch)];
            float value = lerp(mix[static_cast<size_t>(index0)], mix[static_cast<size_t>(index1)], frac);
            for (int source = 0; source < kSourceCount; ++source) {
                const auto& stem = activeFrame.stems[static_cast<size_t>(source)][static_cast<size_t>(ch)];
                const float stemValue = lerp(stem[static_cast<size_t>(index0)], stem[static_cast<size_t>(index1)], frac);
                value += stemValue * (gains[static_cast<size_t>(source)] - 1.0f);
            }
            if (applyBoundaryFade && ch < kStemFrameChannels)
                value = lerp(boundaryStartSample[ch], value, boundaryFade);
            lastOutputSample[ch] = value;
            buffer.setSample(ch, sample, value);
        }

        for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
            buffer.setSample(ch, sample, 0.0f);

        frameReadPosition += sourceAdvance;
        if (boundaryFadeRemaining > 0)
            --boundaryFadeRemaining;
    }

    publishDebugFrame();
}

float StemRebalanceDsp::recordingTrustScale() const noexcept {
    switch (recordingType) {
        case RecordingType::live: return 0.86f;
        case RecordingType::phoneRough: return 0.70f;
        case RecordingType::studio:
        default: return 1.0f;
    }
}

float StemRebalanceDsp::sourcePreserveBlend(const int source) const noexcept {
    if (recordingType == RecordingType::studio)
        return 1.0f;

    if (source == otherSource)
        return recordingType == RecordingType::phoneRough ? 0.62f : 0.78f;

    if (source == guitarSource)
        return recordingType == RecordingType::phoneRough ? 0.70f : 0.86f;

    return recordingType == RecordingType::phoneRough ? 0.82f : 0.92f;
}

void StemRebalanceDsp::publishDebugFrame() noexcept {
    debugSnapshot = {};
    debugSnapshot.frameCounter = static_cast<int>(renderedSequence);
    if (queueCount <= 0)
        return;

    const auto& frame = stemQueue[static_cast<size_t>(queueReadIndex)];
    debugSnapshot.overallConfidence = frame.confidence * recordingTrustScale();

    std::array<double, kSourceCount> energy {};
    for (int source = 0; source < kSourceCount; ++source) {
        for (int ch = 0; ch < kStemFrameChannels; ++ch) {
            for (int sample = 0; sample < kStemFrameSamples; ++sample) {
                const float value = frame.stems[static_cast<size_t>(source)][static_cast<size_t>(ch)][static_cast<size_t>(sample)];
                energy[static_cast<size_t>(source)] += static_cast<double>(value) * static_cast<double>(value);
            }
        }
    }

    const double totalEnergy = std::max(1.0e-12, energy[0] + energy[1] + energy[2] + energy[3] + energy[4]);
    int dominant = otherSource;
    double dominantEnergy = 0.0;
    for (int source = 0; source < kSourceCount; ++source) {
        debugSnapshot.dominantCoverage[static_cast<size_t>(source)] =
            static_cast<float>(energy[static_cast<size_t>(source)] / totalEnergy);
        if (energy[static_cast<size_t>(source)] > dominantEnergy) {
            dominantEnergy = energy[static_cast<size_t>(source)];
            dominant = source;
        }
    }

    for (int i = 0; i < kDebugBins; ++i) {
        debugSnapshot.dominantSources[static_cast<size_t>(i)] = dominant;
        debugSnapshot.confidence[static_cast<size_t>(i)] = debugSnapshot.overallConfidence;
        debugSnapshot.dominantMasks[static_cast<size_t>(i)] =
            static_cast<float>(dominantEnergy / totalEnergy);
        debugSnapshot.otherMasks[static_cast<size_t>(i)] =
            debugSnapshot.dominantCoverage[static_cast<size_t>(otherSource)];
    }
}

} // namespace vxsuite::rebalance::ai
