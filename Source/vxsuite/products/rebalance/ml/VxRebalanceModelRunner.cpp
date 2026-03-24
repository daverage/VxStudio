#include "VxRebalanceModelRunner.h"

#include <array>

namespace vxsuite::rebalance::ml {

void ModelRunner::prepare(const double sampleRate, const int maxBlockSize) {
    sampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    maxBlockSizePrepared = std::max(1, maxBlockSize);
    featureBuffer.prepare(sampleRateHz, maxBlockSizePrepared);
    confidenceTracker.prepare(sampleRateHz);

    const auto modelFile = findModelAsset();
    const bool foundModel = modelFile.existsAsFile();
    detectedLayout = foundModel ? detectModelLayout(modelFile) : ModelLayout::none;
    modelDiscovered.store(foundModel, std::memory_order_relaxed);
    status.store(static_cast<int>(foundModel ? Status::umx4ModelDetectedRuntimePending
                                             : Status::heuristicFallback),
                 std::memory_order_relaxed);
    reset();
}

void ModelRunner::reset() {
    featureBuffer.reset();
    confidenceTracker.reset();
    latestSnapshot = {};
    latestBlendConfidence.store(0.0f, std::memory_order_relaxed);
}

void ModelRunner::analyseBlock(const juce::AudioBuffer<float>& buffer,
                               const Dsp::RecordingType recordingType,
                               const vxsuite::SignalQualitySnapshot& signalQuality) {
    const auto features = featureBuffer.analyseBlock(buffer);
    const float blendConfidence = confidenceTracker.update(features, recordingType, signalQuality);
    latestBlendConfidence.store(blendConfidence, std::memory_order_relaxed);

    latestSnapshot.available = false;
    latestSnapshot.confidence = 0.0f;
    latestSnapshot.derivedGuitarFromOther = detectedLayout == ModelLayout::umx4DerivedGuitar;
    if (status.load(std::memory_order_relaxed) == static_cast<int>(Status::mlMasksActive))
        latestSnapshot.confidence = blendConfidence;
}

juce::String ModelRunner::statusText() const {
    const auto current = static_cast<Status>(status.load(std::memory_order_relaxed));
    switch (current) {
        case Status::mlMasksActive:
            return detectedLayout == ModelLayout::umx4DerivedGuitar
                ? "V2.0 UMX4 masks active"
                : "ML masks active";
        case Status::umx4ModelDetectedRuntimePending:
            return detectedLayout == ModelLayout::umx4DerivedGuitar
                ? "V2.0 UMX4 model found - runtime pending"
                : "ML model found - runtime pending";
        case Status::heuristicFallback:
            break;
    }
    return "V2.0 heuristic fallback";
}

juce::File ModelRunner::bundleResourcesDirectory() const {
    auto current = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();
    while (current.exists() && current != current.getParentDirectory()) {
        if (current.getFileName() == "Contents") {
            const auto resources = current.getChildFile("Resources");
            if (resources.isDirectory())
                return resources;
            break;
        }
        current = current.getParentDirectory();
    }
    return {};
}

juce::File ModelRunner::findModelAsset() const {
    static constexpr std::array<const char*, 7> kModelNames {
        "VXRebalanceUmx4.onnx",
        "vx_rebalance_umx4.onnx",
        "vx_rebalance_v2_0.onnx",
        "VXRebalanceMasks.onnx",
        "vx_rebalance_masks.onnx",
        "vx_rebalance_v2.onnx",
        "vx_rebalance_5head.onnx"
    };

    if (const auto bundleResources = bundleResourcesDirectory(); bundleResources.isDirectory()) {
        for (const auto* name : kModelNames) {
            const auto bundled = bundleResources.getChildFile(name);
            if (bundled.existsAsFile())
                return bundled;
        }
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    for (const auto* name : kModelNames) {
        const juce::File candidates[] = {
            cwd.getChildFile("assets/rebalance/models/" + juce::String(name)),
            cwd.getChildFile("../assets/rebalance/models/" + juce::String(name))
        };
        for (const auto& candidate : candidates) {
            if (candidate.existsAsFile())
                return candidate;
        }
    }

    return {};
}

ModelRunner::ModelLayout ModelRunner::detectModelLayout(const juce::File& modelFile) const noexcept {
    const auto name = modelFile.getFileNameWithoutExtension().toLowerCase();
    if (name.contains("umx4") || name.contains("v2_0") || name.contains("4head"))
        return ModelLayout::umx4DerivedGuitar;
    if (name.contains("5head") || name.contains("five"))
        return ModelLayout::fiveStem;
    return ModelLayout::umx4DerivedGuitar;
}

} // namespace vxsuite::rebalance::ml
