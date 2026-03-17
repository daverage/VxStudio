#include "VxDeepFilterNetService.h"

#include <BinaryData.h>

#include <algorithm>
#include <cmath>

#include <juce_core/juce_core.h>

namespace vxsuite::deepfilternet {

namespace {

constexpr double kEngineSampleRate = 48000.0;
constexpr int kDefaultFrameLength = 480;
constexpr int kFifoCapacity48k = 48000;
constexpr int kDfn3Latency48k = 1920;
constexpr int kDfn2LowLatency48k = 480;

float clamp01(const float value) {
    return juce::jlimit(0.0f, 1.0f, value);
}

} // namespace

void DeepFilterService::SampleFifo::reset(const int capacity) {
    buffer.assign(static_cast<size_t>(std::max(1, capacity)), 0.0f);
    clear();
}

void DeepFilterService::SampleFifo::clear() {
    writePos = 0;
    readPos = 0;
    available = 0;
}

void DeepFilterService::SampleFifo::push(const float* data, const int count) {
    if (buffer.empty() || data == nullptr || count <= 0)
        return;

    const int capacity = static_cast<int>(buffer.size());
    for (int i = 0; i < count; ++i) {
        buffer[static_cast<size_t>(writePos)] = data[i];
        writePos = (writePos + 1) % capacity;
        if (available < capacity) {
            ++available;
        } else {
            readPos = (readPos + 1) % capacity;
        }
    }
}

void DeepFilterService::SampleFifo::pop(float* dest, const int count) {
    if (buffer.empty() || dest == nullptr || count <= 0)
        return;

    const int capacity = static_cast<int>(buffer.size());
    for (int i = 0; i < count; ++i) {
        if (available > 0) {
            dest[i] = buffer[static_cast<size_t>(readPos)];
            readPos = (readPos + 1) % capacity;
            --available;
        } else {
            dest[i] = 0.0f;
        }
    }
}

DeepFilterService::~DeepFilterService() {
    releaseRuntime();
}

juce::String DeepFilterService::lastStatus() const {
    switch (statusCode.load(std::memory_order_relaxed)) {
        case StatusCode::idle: return "idle";
        case StatusCode::rtPreparing: return "rt_preparing";
        case StatusCode::rtReady: return "rt_ready";
        case StatusCode::rtMissingModel: return "rt_missing_model";
        case StatusCode::rtInitFailed: return "rt_init_failed";
        case StatusCode::rtProcessFailed: return "rt_process_failed";
        case StatusCode::rtReprepareNeeded: return "rt_reprepare_needed";
    }
    return "idle";
}

DeepFilterService::RuntimeApi DeepFilterService::selectedRuntimeApi() const noexcept {
    return modelVariant == ModelVariant::dfn2 ? RuntimeApi::dfn2 : RuntimeApi::dfn3;
}

int DeepFilterService::latencySamplesForCurrentVariant() const noexcept {
    const int latency48k = modelVariant == ModelVariant::dfn2 ? kDfn2LowLatency48k : kDfn3Latency48k;
    return juce::roundToInt(static_cast<double>(latency48k) * rtSampleRate / kEngineSampleRate);
}

bool DeepFilterService::needsRealtimePrepare(const double sampleRate, const int maxBlockSize) const {
    if (sampleRate <= 1000.0 || maxBlockSize <= 0)
        return false;
    if (!rtReady)
        return true;
    if (rtPreparedVariant != modelVariant)
        return true;
    if (std::abs(rtSampleRate - sampleRate) > 1.0)
        return true;
    return rtBlockSize < maxBlockSize;
}

void DeepFilterService::releaseRuntime() {
    for (auto& channel : channels) {
        if (channel.runtime != nullptr) {
            destroyRuntime(channel.runtime);
            channel.runtime = nullptr;
        }
        channel.resampler.reset();
        channel.inputFifo.clear();
        channel.outputFifo.clear();
        std::fill(channel.frameIn.begin(), channel.frameIn.end(), 0.0f);
        std::fill(channel.frameOut.begin(), channel.frameOut.end(), 0.0f);
    }
    extractedModelFile = juce::File();
    rtReady = false;
    rtCapability = RealtimeCapability::unavailable;
    rtBackend = RealtimeBackend::none;
    rtBackendTag = "none";
    rtRuntimeApi = RuntimeApi::none;
}

void* DeepFilterService::createRuntime(const juce::String& modelPath, const float attenuationLimitDb) const {
    switch (selectedRuntimeApi()) {
        case RuntimeApi::dfn2:
            return df2_create(modelPath.toRawUTF8(), attenuationLimitDb, nullptr);
        case RuntimeApi::dfn3:
            return df_create(modelPath.toRawUTF8(), attenuationLimitDb, nullptr);
        case RuntimeApi::none:
            break;
    }
    return nullptr;
}

void DeepFilterService::destroyRuntime(void* runtime) const {
    if (runtime == nullptr)
        return;

    switch (rtRuntimeApi) {
        case RuntimeApi::dfn2:
            df2_free(static_cast<DF2State*>(runtime));
            return;
        case RuntimeApi::dfn3:
            df_free(static_cast<DFState*>(runtime));
            return;
        case RuntimeApi::none:
            break;
    }
}

int DeepFilterService::runtimeFrameLength(void* runtime) const {
    if (runtime == nullptr)
        return 0;

    switch (rtRuntimeApi) {
        case RuntimeApi::dfn2:
            return static_cast<int>(df2_get_frame_length(static_cast<DF2State*>(runtime)));
        case RuntimeApi::dfn3:
            return static_cast<int>(df_get_frame_length(static_cast<DFState*>(runtime)));
        case RuntimeApi::none:
            break;
    }
    return 0;
}

void DeepFilterService::setRuntimeAttenuation(void* runtime, const float attenuationLimitDb) const {
    if (runtime == nullptr)
        return;

    switch (rtRuntimeApi) {
        case RuntimeApi::dfn2:
            df2_set_atten_lim(static_cast<DF2State*>(runtime), attenuationLimitDb);
            return;
        case RuntimeApi::dfn3:
            df_set_atten_lim(static_cast<DFState*>(runtime), attenuationLimitDb);
            return;
        case RuntimeApi::none:
            break;
    }
}

float DeepFilterService::processRuntimeFrame(void* runtime, float* input, float* output) const {
    if (runtime == nullptr || input == nullptr || output == nullptr)
        return 0.0f;

    switch (rtRuntimeApi) {
        case RuntimeApi::dfn2:
            return df2_process_frame(static_cast<DF2State*>(runtime), input, output);
        case RuntimeApi::dfn3:
            return df_process_frame(static_cast<DFState*>(runtime), input, output);
        case RuntimeApi::none:
            break;
    }
    return 0.0f;
}

juce::File DeepFilterService::modelAssetForVariant(const ModelVariant variant) const {
    const auto currentWorkingDirectory = juce::File::getCurrentWorkingDirectory();
    if (variant == ModelVariant::dfn2) {
        const juce::String dfn2Names[] = { "DeepFilterNet2_onnx_ll.tar.gz", "DeepFilterNet2_onnx.tar.gz" };
        for (const auto& fileName : dfn2Names) {
            const juce::File candidates[] = {
                currentWorkingDirectory.getChildFile("assets/deepfilternet/models/" + fileName),
                currentWorkingDirectory.getChildFile("../assets/deepfilternet/models/" + fileName)
            };
            for (const auto& candidate : candidates) {
                if (candidate.existsAsFile())
                    return candidate;
            }
        }
        return {};
    }

    const juce::String fileName = "DeepFilterNet3_onnx.tar.gz";
    const juce::File candidates[] = {
        currentWorkingDirectory.getChildFile("assets/deepfilternet/models/" + fileName),
        currentWorkingDirectory.getChildFile("../assets/deepfilternet/models/" + fileName)
    };
    for (const auto& candidate : candidates) {
        if (candidate.existsAsFile())
            return candidate;
    }
    return {};
}

juce::String DeepFilterService::binaryDataNameForVariant(const ModelVariant variant) const {
    return variant == ModelVariant::dfn2
        ? "DeepFilterNet2_onnx_ll_tar_gz"
        : "DeepFilterNet3_onnx_tar_gz";
}

bool DeepFilterService::extractEmbeddedModel(const juce::File& destination) {
    int dataSize = 0;
    const auto resourceName = binaryDataNameForVariant(modelVariant);
    const char* bytes = BinaryData::getNamedResource(resourceName.toRawUTF8(), dataSize);
    if (bytes == nullptr || dataSize <= 0)
        return false;

    if (auto stream = std::unique_ptr<juce::FileOutputStream>(destination.createOutputStream())) {
        stream->setPosition(0);
        stream->truncate();
        stream->write(bytes, static_cast<size_t>(dataSize));
        stream->flush();
        return destination.getSize() == static_cast<int64_t>(dataSize);
    }
    return false;
}

bool DeepFilterService::prepareModelFile() {
    auto modelFile = modelAssetForVariant(modelVariant);
    if (modelFile.existsAsFile()) {
        extractedModelFile = modelFile;
        return true;
    }

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("vxsuite_deepfilternet_models");
    if (!tempDirectory.createDirectory())
        return false;

    const auto tempModel = tempDirectory.getChildFile(modelVariant == ModelVariant::dfn2
        ? "DeepFilterNet2_onnx_ll.tar.gz"
        : "DeepFilterNet3_onnx.tar.gz");
    if (!tempModel.existsAsFile() && !extractEmbeddedModel(tempModel))
        return false;

    extractedModelFile = tempModel;
    return extractedModelFile.existsAsFile();
}

bool DeepFilterService::prepareChannel(ChannelState& channel, const int maxBlockSize) {
    if (!prepareModelFile())
        return false;

    channel.runtime = createRuntime(extractedModelFile.getFullPathName(), 24.0f);
    if (channel.runtime == nullptr)
        return false;

    channel.resampler = std::make_unique<Resampler<1, 1>>(rtSampleRate, kEngineSampleRate);
    channel.inputFifo.reset(kFifoCapacity48k);
    channel.outputFifo.reset(kFifoCapacity48k);

    const int frameLength = runtimeFrameLength(channel.runtime);
    rtFrameLength = std::max(1, frameLength);
    channel.frameIn.assign(static_cast<size_t>(rtFrameLength), 0.0f);
    channel.frameOut.assign(static_cast<size_t>(rtFrameLength), 0.0f);

    const auto maxRatio = std::max(1.0, kEngineSampleRate / rtSampleRate);
    const auto maxResampledSize = std::max(rtFrameLength, static_cast<int>(std::ceil(maxBlockSize * maxRatio)) + 128);
    channel.resampleIn.assign(static_cast<size_t>(maxResampledSize), 0.0f);
    channel.resampleOut.assign(static_cast<size_t>(maxResampledSize), 0.0f);
    return true;
}

void DeepFilterService::prepareRealtime(const double sampleRate, const int maxBlockSize) {
    setStatus(StatusCode::rtPreparing);
    releaseRuntime();

    rtSampleRate = sampleRate > 1000.0 ? sampleRate : kEngineSampleRate;
    rtBlockSize = std::max(1, maxBlockSize);
    rtRuntimeApi = selectedRuntimeApi();
    rtPreparedVariant = modelVariant;
    rtFrameLength = kDefaultFrameLength;
    latencySamples = latencySamplesForCurrentVariant();

    for (auto& channel : channels) {
        if (!prepareChannel(channel, rtBlockSize)) {
            releaseRuntime();
            setStatus(extractedModelFile.existsAsFile() ? StatusCode::rtInitFailed : StatusCode::rtMissingModel);
            return;
        }
    }

    rtReady = true;
    rtCapability = RealtimeCapability::embeddedRuntime;
    rtBackend = RealtimeBackend::cpu;
    rtBackendTag = rtRuntimeApi == RuntimeApi::dfn2 ? "libdf031:cpu" : "libdf:cpu";
    setStatus(StatusCode::rtReady);
}

void DeepFilterService::resetRealtime() {
    for (auto& channel : channels) {
        if (channel.runtime != nullptr) {
            destroyRuntime(channel.runtime);
            channel.runtime = createRuntime(extractedModelFile.getFullPathName(), 24.0f);
        }
        if (channel.runtime != nullptr)
            setRuntimeAttenuation(channel.runtime, 24.0f);
        if (channel.resampler != nullptr)
            channel.resampler = std::make_unique<Resampler<1, 1>>(rtSampleRate, kEngineSampleRate);
        channel.inputFifo.clear();
        channel.outputFifo.clear();
        std::fill(channel.frameIn.begin(), channel.frameIn.end(), 0.0f);
        std::fill(channel.frameOut.begin(), channel.frameOut.end(), 0.0f);
    }
    tailPrior = 0.0f;
}

float DeepFilterService::attenuationLimitForStrength(const float strength) const noexcept {
    return 6.0f + 54.0f * clamp01(strength);
}

bool DeepFilterService::processRealtime(juce::AudioBuffer<float>& buffer,
                                        const double sampleRate,
                                        const float strength,
                                        const uint64_t key) {
    juce::ignoreUnused(key);

    if (!rtReady || !hasRealtimeBackend())
        return false;
    if (needsRealtimePrepare(sampleRate, buffer.getNumSamples())) {
        setStatus(StatusCode::rtReprepareNeeded);
        return false;
    }

    const int numChannels = std::min(buffer.getNumChannels(), rtMaxChannels);
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return false;

    const auto attenuationLimitDb = attenuationLimitForStrength(strength);
    const float wet = clamp01(strength);

    for (int channelIndex = 0; channelIndex < numChannels; ++channelIndex) {
        auto& channel = channels[static_cast<size_t>(channelIndex)];
        if (channel.runtime == nullptr || channel.resampler == nullptr)
            return false;

        setRuntimeAttenuation(channel.runtime, attenuationLimitDb);

        float* sourceInput[] = { buffer.getWritePointer(channelIndex) };
        float* sourceOutput[] = { buffer.getWritePointer(channelIndex) };
        float* targetInput[] = { channel.resampleIn.data() };
        float* targetOutput[] = { channel.resampleOut.data() };

        channel.resampler->process(
            sourceInput,
            sourceOutput,
            targetInput,
            targetOutput,
            numSamples,
            [&](float* const* inputBuffers, float* const* outputBuffers, int sampleCount48k) {
                auto* input48k = inputBuffers[0];
                auto* output48k = outputBuffers[0];

                channel.inputFifo.push(input48k, sampleCount48k);

                while (channel.inputFifo.available >= rtFrameLength) {
                    channel.inputFifo.pop(channel.frameIn.data(), rtFrameLength);
                    processRuntimeFrame(channel.runtime, channel.frameIn.data(), channel.frameOut.data());
                    channel.outputFifo.push(channel.frameOut.data(), rtFrameLength);
                }

                const int outputCount = std::min(channel.outputFifo.available, sampleCount48k);
                channel.outputFifo.pop(output48k, outputCount);
                if (outputCount < sampleCount48k)
                    juce::FloatVectorOperations::clear(output48k + outputCount, sampleCount48k - outputCount);
            });
    }

    for (int channelIndex = 0; channelIndex < numChannels; ++channelIndex) {
        auto* samples = buffer.getWritePointer(channelIndex);
        for (int i = 0; i < numSamples; ++i)
            samples[i] *= wet;
    }

    if (buffer.getNumChannels() > 1 && numChannels == 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);

    tailPrior = 0.92f * tailPrior + 0.08f * wet;
    setStatus(StatusCode::rtReady);
    return true;
}

} // namespace vxsuite::deepfilternet
