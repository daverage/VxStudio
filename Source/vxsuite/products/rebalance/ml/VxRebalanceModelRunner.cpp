#include "VxRebalanceModelRunner.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace vxsuite::rebalance::ml {

namespace {

constexpr double kModelSampleRate = 44100.0;
constexpr int kModelFftOrder = 12;
constexpr int kModelFftSize = 1 << kModelFftOrder;
constexpr int kModelHopSize = kModelFftSize / 4;
constexpr int kModelBins = kModelFftSize / 2 + 1;
constexpr int kModelFrames = OnnxUmx4Model::kModelFrames;
constexpr float kEps = 1.0e-8f;

inline float clamp01(const float value) noexcept {
    return juce::jlimit(0.0f, 1.0f, value);
}

} // namespace

void ModelRunner::SampleFifo::reset(const int capacity) {
    buffer.assign(static_cast<size_t>(std::max(1, capacity)), 0.0f);
    clear();
}

void ModelRunner::SampleFifo::clear() {
    writePos = 0;
    available = 0;
}

void ModelRunner::SampleFifo::push(const float* data, const int count) {
    if (buffer.empty() || data == nullptr || count <= 0)
        return;

    const int capacity = static_cast<int>(buffer.size());
    for (int i = 0; i < count; ++i) {
        buffer[static_cast<size_t>(writePos)] = data[i];
        writePos = (writePos + 1) % capacity;
        available = std::min(available + 1, capacity);
    }
}

void ModelRunner::prepare(const double sampleRate, const int maxBlockSize) {
    sampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    maxBlockSizePrepared = std::max(1, maxBlockSize);
    featureBuffer.prepare(sampleRateHz, maxBlockSizePrepared);
    confidenceTracker.prepare(sampleRateHz);

    fft.prepare(kModelFftOrder);
    vxsuite::spectral::prepareSqrtHannWindow(window, kModelFftSize);
    const auto maxRatio = std::max(1.0, kModelSampleRate / sampleRateHz);
    const auto maxResampledSize = std::max(kModelHopSize,
        static_cast<int>(std::ceil(maxBlockSizePrepared * maxRatio)) + 128);
    for (auto& channel : channels) {
        channel.inputFifo.reset(kModelFftSize + maxResampledSize + kModelHopSize);
        channel.resampler = std::make_unique<Resampler<1, 1>>(sampleRateHz, kModelSampleRate);
        channel.sourceScratch.assign(static_cast<size_t>(maxBlockSizePrepared), 0.0f);
        channel.resampleIn.assign(static_cast<size_t>(maxResampledSize), 0.0f);
        channel.resampleOut.assign(static_cast<size_t>(maxResampledSize), 0.0f);
    }
    for (auto& buffer : fftBuffers)
        buffer.assign(static_cast<size_t>(kModelFftSize * 2), 0.0f);
    modelInputMagnitudes.assign(static_cast<size_t>(2 * kModelBins * kModelFrames), 0.0f);
    modelOutputMagnitudes.assign(static_cast<size_t>(OnnxUmx4Model::kHeadCount * 2 * kModelBins * kModelFrames), 0.0f);
    frameHistory.assign(static_cast<size_t>(2 * kModelBins * kModelFrames), 0.0f);
    frameWriteIndex = 0;
    frameCount = 0;

    const auto modelFile = findModelAsset();
    const bool foundModel = modelFile.existsAsFile();
    detectedLayout = foundModel ? detectModelLayout(modelFile) : ModelLayout::none;
    modelDiscovered.store(foundModel, std::memory_order_relaxed);

    bool modelReady = false;
    lastError.clear();
    if (foundModel && detectedLayout == ModelLayout::umx4DerivedGuitar) {
        const auto onnxPath = resolveOnnxPath(modelFile);
        if (onnxPath.existsAsFile())
            modelReady = onnxModel.prepare(onnxPath.getFullPathName().toStdString(), lastError);
    }

    status.store(static_cast<int>(modelReady ? Status::mlMasksActive
                                             : (foundModel ? Status::umx4ModelDetectedRuntimePending
                                                           : Status::heuristicFallback)),
                 std::memory_order_relaxed);
    reset();
}

void ModelRunner::reset() {
    featureBuffer.reset();
    confidenceTracker.reset();
    for (auto& channel : channels) {
        channel.inputFifo.clear();
        channel.resampler = std::make_unique<Resampler<1, 1>>(sampleRateHz, kModelSampleRate);
        std::fill(channel.sourceScratch.begin(), channel.sourceScratch.end(), 0.0f);
        std::fill(channel.resampleIn.begin(), channel.resampleIn.end(), 0.0f);
        std::fill(channel.resampleOut.begin(), channel.resampleOut.end(), 0.0f);
    }
    for (auto& buffer : fftBuffers)
        std::fill(buffer.begin(), buffer.end(), 0.0f);
    std::fill(modelInputMagnitudes.begin(), modelInputMagnitudes.end(), 0.0f);
    std::fill(modelOutputMagnitudes.begin(), modelOutputMagnitudes.end(), 0.0f);
    std::fill(frameHistory.begin(), frameHistory.end(), 0.0f);
    frameWriteIndex = 0;
    frameCount = 0;
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

    const int numChannels = std::min(2, buffer.getNumChannels());
    if (numChannels <= 0)
        return;

    for (int ch = 0; ch < 2; ++ch) {
        const int sourceChannel = numChannels == 1 ? 0 : ch;
        auto& channel = channels[static_cast<size_t>(ch)];
        if (channel.resampler == nullptr)
            continue;

        float* sourceInput[] = { const_cast<float*>(buffer.getReadPointer(sourceChannel)) };
        float* sourceOutput[] = { channel.sourceScratch.data() };
        float* targetInput[] = { channel.resampleIn.data() };
        float* targetOutput[] = { channel.resampleOut.data() };

        channel.resampler->process(
            sourceInput,
            sourceOutput,
            targetInput,
            targetOutput,
            buffer.getNumSamples(),
            [&](float* const* inputBuffers, float* const* outputBuffers, int sampleCount44k) {
                juce::ignoreUnused(outputBuffers);
                channel.inputFifo.push(inputBuffers[0], sampleCount44k);
            });
    }

    if (status.load(std::memory_order_relaxed) != static_cast<int>(Status::mlMasksActive))
        return;

    while (channels[0].inputFifo.available >= kModelFftSize && channels[1].inputFifo.available >= kModelFftSize)
        processModelFrame(signalQuality);

    if (status.load(std::memory_order_relaxed) == static_cast<int>(Status::mlMasksActive))
        latestSnapshot.confidence = blendConfidence;
}

void ModelRunner::processModelFrame(const vxsuite::SignalQualitySnapshot& signalQuality) {
    for (int ch = 0; ch < 2; ++ch) {
        auto& fifo = channels[static_cast<size_t>(ch)].inputFifo;
        auto& fftBuffer = fftBuffers[static_cast<size_t>(ch)];
        std::fill(fftBuffer.begin(), fftBuffer.end(), 0.0f);
        const int fifoSize = static_cast<int>(fifo.buffer.size());
        const int frameStart = (fifo.writePos - fifo.available + fifoSize) % fifoSize;
        for (int i = 0; i < kModelFftSize; ++i) {
            const int srcIndex = (frameStart + i) % fifoSize;
            fftBuffer[static_cast<size_t>(i)] = fifo.buffer[static_cast<size_t>(srcIndex)] * window[static_cast<size_t>(i)];
        }
        fft.performForward(fftBuffer.data());

        for (int k = 0; k < kModelBins; ++k) {
            const float re = fftBuffer[static_cast<size_t>(2 * k)];
            const float im = (k == 0 || k == kModelBins - 1) ? 0.0f : fftBuffer[static_cast<size_t>(2 * k + 1)];
            const float mag = std::sqrt(std::max(kEps, re * re + im * im));
            const size_t index = static_cast<size_t>(((ch * kModelBins) + k) * kModelFrames + frameWriteIndex);
            frameHistory[index] = mag;
        }

        fifo.available -= kModelHopSize;
    }

    frameWriteIndex = (frameWriteIndex + 1) % kModelFrames;
    frameCount = std::min(frameCount + 1, kModelFrames);

    if (frameCount < kModelFrames)
        return;

    for (int ch = 0; ch < 2; ++ch) {
        for (int k = 0; k < kModelBins; ++k) {
            for (int frame = 0; frame < kModelFrames; ++frame) {
                const int sourceFrame = (frameWriteIndex + frame) % kModelFrames;
                const size_t historyIndex = static_cast<size_t>(((ch * kModelBins) + k) * kModelFrames + sourceFrame);
                const size_t inputIndex = static_cast<size_t>(((ch * kModelBins) + k) * kModelFrames + frame);
                modelInputMagnitudes[inputIndex] = frameHistory[historyIndex];
            }
        }
    }

    std::string error;
    if (!onnxModel.run(modelInputMagnitudes.data(), kModelFrames, modelOutputMagnitudes.data(), error)) {
        status.store(static_cast<int>(Status::umx4ModelDetectedRuntimePending), std::memory_order_relaxed);
        lastError = error;
        latestSnapshot = {};
        return;
    }

    const int latestFrame = kModelFrames - 1;
    for (int k = 0; k < Dsp::kBins; ++k) {
        const float hz = static_cast<float>(k) * static_cast<float>(sampleRateHz) / static_cast<float>(Dsp::kFftSize);
        const int modelBin = juce::jlimit(0, kModelBins - 1,
            juce::roundToInt(hz * static_cast<float>(kModelFftSize) / static_cast<float>(kModelSampleRate)));

        float shares[OnnxUmx4Model::kHeadCount] {};
        float total = 0.0f;
        for (int head = 0; head < OnnxUmx4Model::kHeadCount; ++head) {
            const size_t leftIndex = static_cast<size_t>((((head * 2) + 0) * kModelBins + modelBin) * kModelFrames + latestFrame);
            const size_t rightIndex = static_cast<size_t>((((head * 2) + 1) * kModelBins + modelBin) * kModelFrames + latestFrame);
            shares[head] = 0.5f * (modelOutputMagnitudes[leftIndex] + modelOutputMagnitudes[rightIndex]);
            total += shares[head];
        }
        total = std::max(kEps, total);

        latestSnapshot.masks[Dsp::vocalsSource][static_cast<size_t>(k)] = shares[0] / total;
        latestSnapshot.masks[Dsp::drumsSource][static_cast<size_t>(k)] = shares[1] / total;
        latestSnapshot.masks[Dsp::bassSource][static_cast<size_t>(k)] = shares[2] / total;
        latestSnapshot.masks[Dsp::guitarSource][static_cast<size_t>(k)] = 0.0f;
        latestSnapshot.masks[Dsp::otherSource][static_cast<size_t>(k)] = shares[3] / total;
    }

    latestSnapshot.available = true;
    latestSnapshot.derivedGuitarFromOther = true;
    latestSnapshot.confidence = clamp01(latestBlendConfidence.load(std::memory_order_relaxed)
        * signalQuality.separationConfidence);
}

juce::String ModelRunner::statusText() const {
    const auto current = static_cast<Status>(status.load(std::memory_order_relaxed));
    switch (current) {
        case Status::mlMasksActive:
            return "V2.0 UMX4 masks active";
        case Status::umx4ModelDetectedRuntimePending:
            if (modelDiscovered.load(std::memory_order_relaxed)) {
                auto text = juce::String("V2.0 UMX4 model found - runtime pending");
                if (!lastError.empty())
                    text << " (" << juce::String(lastError) << ")";
                return text;
            }
            return "V2.0 heuristic fallback";
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
    if (const auto bundleResources = bundleResourcesDirectory(); bundleResources.isDirectory()) {
        const auto bundledManifest = bundleResources.getChildFile("rebalance_umx4.json");
        if (bundledManifest.existsAsFile())
            return bundledManifest;
        const auto bundledOnnx = bundleResources.getChildFile("vx_rebalance_umx4.onnx");
        if (bundledOnnx.existsAsFile())
            return bundledOnnx;
    }

    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const juce::File candidates[] = {
        cwd.getChildFile("assets/rebalance/models/openunmix_umxhq_spec_onnx/rebalance_umx4.json"),
        cwd.getChildFile("../assets/rebalance/models/openunmix_umxhq_spec_onnx/rebalance_umx4.json"),
        cwd.getChildFile("assets/rebalance/models/openunmix_umxhq_spec_onnx/vx_rebalance_umx4.onnx"),
        cwd.getChildFile("../assets/rebalance/models/openunmix_umxhq_spec_onnx/vx_rebalance_umx4.onnx")
    };
    for (const auto& candidate : candidates) {
        if (candidate.existsAsFile())
            return candidate;
    }
    return {};
}

ModelRunner::ModelLayout ModelRunner::detectModelLayout(const juce::File& modelFile) const noexcept {
    const auto name = modelFile.getFileNameWithoutExtension().toLowerCase();
    if (name.contains("umx") || name.contains("rebalance_umx4") || name.contains("separator"))
        return ModelLayout::umx4DerivedGuitar;
    if (name.contains("5head") || name.contains("five"))
        return ModelLayout::fiveStem;
    return ModelLayout::none;
}

juce::File ModelRunner::resolveOnnxPath(const juce::File& modelAsset) const {
    if (modelAsset.hasFileExtension("onnx"))
        return modelAsset;

    const auto sibling = modelAsset.getSiblingFile("vx_rebalance_umx4.onnx");
    if (sibling.existsAsFile())
        return sibling;
    return {};
}

} // namespace vxsuite::rebalance::ml
