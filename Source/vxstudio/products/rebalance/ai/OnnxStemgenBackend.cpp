#include "OnnxStemgenBackend.h"

#if VXSTUDIO_REBALANCE_AI_HAS_STEMGENRT
#include "StemgenRT/Constants.h"
#endif

#include <algorithm>
#include <chrono>

namespace vxsuite::rebalance::ai {

OnnxStemgenBackend::OnnxStemgenBackend() {
#if VXSTUDIO_REBALANCE_AI_HAS_STEMGENRT
    for (auto& stem : overlapTail) {
        for (auto& channel : stem)
            channel.assign(static_cast<size_t>(audio_plugin::kCrossfadeSamples), 0.0f);
    }
#endif
}

OnnxStemgenBackend::~OnnxStemgenBackend() = default;

bool OnnxStemgenBackend::prepare(const double sampleRate,
                                 const int outputChunkSize,
                                 const int contextSize,
                                 const int numChannels,
                                 juce::String& error) {
#if VXSTUDIO_REBALANCE_AI_HAS_STEMGENRT
    if (std::abs(sampleRate - 44100.0) > 1.0) {
        error = "HS-TasNet model requires 44.1 kHz model-rate input";
        return false;
    }

    if (outputChunkSize != audio_plugin::kOutputChunkSize || contextSize != audio_plugin::kContextSize) {
        error = "ONNX HS-TasNet backend requires 512-sample chunks and 1024-sample context";
        return false;
    }

    if (numChannels < 1) {
        error = "ONNX HS-TasNet backend requires audio input";
        return false;
    }

    if (!runtime.isInitialized()) {
        error = "ONNX Runtime did not initialise";
        return false;
    }

    if (!runtime.isModelLoaded()) {
        juce::File modelFile;
#if defined(VXSTUDIO_REBALANCE_AI_MODEL_PATH)
        modelFile = juce::File { juce::String { VXSTUDIO_REBALANCE_AI_MODEL_PATH } };
#endif
        if (!modelFile.existsAsFile()) {
            modelFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                .getParentDirectory()
                .getParentDirectory()
                .getChildFile("Resources/model.onnx");
        }

        if (!modelFile.existsAsFile()) {
            error = "model.onnx not found";
            return false;
        }

        if (!modelFile.getSiblingFile("model.onnx.data").existsAsFile()) {
            error = "model.onnx.data not found";
            return false;
        }

        if (!runtime.loadModel(modelFile.getFullPathName(), error))
            return false;

        runtime.prepareForInference();
    }

    return true;
#else
    juce::ignoreUnused(sampleRate, outputChunkSize, contextSize, numChannels);
    error = "ONNX StemgenRT backend not linked";
    return false;
#endif
}

void OnnxStemgenBackend::reset() {
    lastProcessMsValue = 0.0;
    totalProcessMs = 0.0;
    maxProcessMsValue = 0.0;
    processCount = 0;

#if VXSTUDIO_REBALANCE_AI_HAS_STEMGENRT
    for (auto& stem : overlapTail) {
        for (auto& channel : stem)
            std::fill(channel.begin(), channel.end(), 0.0f);
    }
#endif
}

bool OnnxStemgenBackend::processFrame(const ChannelBuffers& context,
                                      const ChannelBuffers& input,
                                      const ChannelBuffers& lowFreq,
                                      const float normalizationGain,
                                      StemBuffers& outputStems,
                                      juce::String& error) {
#if VXSTUDIO_REBALANCE_AI_HAS_STEMGENRT
    const auto start = std::chrono::steady_clock::now();
    const bool ok = runtime.runInference(context, input, lowFreq, normalizationGain, outputStems, overlapTail);
    const auto end = std::chrono::steady_clock::now();

    lastProcessMsValue = std::chrono::duration<double, std::milli>(end - start).count();
    totalProcessMs += lastProcessMsValue;
    maxProcessMsValue = std::max(maxProcessMsValue, lastProcessMsValue);
    ++processCount;

    if (!ok)
        error = "ONNX inference failed";
    return ok;
#else
    juce::ignoreUnused(context, input, lowFreq, normalizationGain, outputStems);
    error = "ONNX StemgenRT backend not linked";
    return false;
#endif
}

juce::String OnnxStemgenBackend::backendName() const {
    return "ONNX StemgenRT";
}

juce::String OnnxStemgenBackend::statusText() const {
#if VXSTUDIO_REBALANCE_AI_HAS_STEMGENRT
    return runtime.getStatusString();
#else
    return "ONNX StemgenRT backend not linked";
#endif
}

} // namespace vxsuite::rebalance::ai
