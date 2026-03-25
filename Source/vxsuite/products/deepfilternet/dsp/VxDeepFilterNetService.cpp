#include "VxDeepFilterNetService.h"

#include "../../../framework/VxSuiteBlockSmoothing.h"
#include "../../../framework/VxSuiteModelAssets.h"

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

vxsuite::ModelPackage packageForVariant(const vxsuite::deepfilternet::DeepFilterService::ModelVariant variant) {
    if (variant == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn2) {
        return {
            "deepfilternet2",
            "DeepFilterNet 2 Model",
            {},
            { { "DeepFilterNet2_onnx_ll.tar.gz", "https://raw.githubusercontent.com/daverage/VxStudio/main/assets/deepfilternet/models/DeepFilterNet2_onnx_ll.tar.gz" } }
        };
    }

    return {
        "deepfilternet3",
        "DeepFilterNet 3 Model",
        {},
        { { "DeepFilterNet3_onnx.tar.gz", "https://raw.githubusercontent.com/daverage/VxStudio/main/assets/deepfilternet/models/DeepFilterNet3_onnx.tar.gz" } }
    };
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
    for (auto& bundle : bundles)
        releaseBundle(bundle);
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

DeepFilterService::RuntimeApi DeepFilterService::selectedRuntimeApi(const ModelVariant variant) const noexcept {
    return variant == ModelVariant::dfn2 ? RuntimeApi::dfn2 : RuntimeApi::dfn3;
}

int DeepFilterService::latencySamplesForVariant(const ModelVariant variant, const double sampleRate) const noexcept {
    const int latency48k = variant == ModelVariant::dfn2 ? kDfn2LowLatency48k : kDfn3Latency48k;
    return juce::roundToInt(static_cast<double>(latency48k) * sampleRate / kEngineSampleRate);
}

bool DeepFilterService::needsRealtimePrepare(const double sampleRate, const int maxBlockSize) const {
    if (sampleRate <= 1000.0 || maxBlockSize <= 0)
        return false;
    const int activeIndex = activeBundleIndex.load(std::memory_order_acquire);
    if (activeIndex < 0 || !rtReady)
        return true;
    const auto& active = bundles[static_cast<size_t>(activeIndex)];
    if (!active.ready)
        return true;
    const auto variant = getModelVariant();
    if (active.preparedVariant != variant)
        return true;
    if (std::abs(active.sampleRate - sampleRate) > 1.0)
        return true;
    if (active.blockSize < maxBlockSize)
        return true;
    return resetRequested.load(std::memory_order_relaxed);
}

void DeepFilterService::releaseBundle(RuntimeBundle& bundle) {
    for (auto& channel : bundle.channels) {
        if (channel.runtime != nullptr) {
            destroyRuntime(bundle.runtimeApi, channel.runtime);
            channel.runtime = nullptr;
        }
        channel.resampler.reset();
        channel.inputFifo.clear();
        channel.outputFifo.clear();
        std::fill(channel.frameIn.begin(), channel.frameIn.end(), 0.0f);
        std::fill(channel.frameOut.begin(), channel.frameOut.end(), 0.0f);
    }
    bundle.modelFile = juce::File();
    bundle.ready = false;
    bundle.capability = RealtimeCapability::unavailable;
    bundle.backend = RealtimeBackend::none;
    bundle.backendTag = "none";
    bundle.runtimeApi = RuntimeApi::none;
    bundle.blockSize = 0;
    bundle.frameLength = kDefaultFrameLength;
    bundle.latencySamples = 0;
}

void* DeepFilterService::createRuntime(const RuntimeApi api,
                                       const juce::String& modelPath,
                                       const float attenuationLimitDb) const {
    switch (api) {
        case RuntimeApi::dfn2:
            return df2_create(modelPath.toRawUTF8(), attenuationLimitDb, nullptr);
        case RuntimeApi::dfn3:
            return df_create(modelPath.toRawUTF8(), attenuationLimitDb, nullptr);
        case RuntimeApi::none:
            break;
    }
    return nullptr;
}

void DeepFilterService::destroyRuntime(const RuntimeApi api, void* runtime) const {
    if (runtime == nullptr)
        return;

    switch (api) {
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

int DeepFilterService::runtimeFrameLength(const RuntimeApi api, void* runtime) const {
    if (runtime == nullptr)
        return 0;

    switch (api) {
        case RuntimeApi::dfn2:
            return static_cast<int>(df2_get_frame_length(static_cast<DF2State*>(runtime)));
        case RuntimeApi::dfn3:
            return static_cast<int>(df_get_frame_length(static_cast<DFState*>(runtime)));
        case RuntimeApi::none:
            break;
    }
    return 0;
}

void DeepFilterService::setRuntimeAttenuation(const RuntimeApi api, void* runtime, const float attenuationLimitDb) const {
    if (runtime == nullptr)
        return;

    switch (api) {
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

float DeepFilterService::processRuntimeFrame(const RuntimeApi api, void* runtime, float* input, float* output) const {
    if (runtime == nullptr || input == nullptr || output == nullptr)
        return 0.0f;

    switch (api) {
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
    const auto fileName = variant == ModelVariant::dfn2
        ? juce::String("DeepFilterNet2_onnx_ll.tar.gz")
        : juce::String("DeepFilterNet3_onnx.tar.gz");
    const auto installedFile = vxsuite::ModelAssetService::instance().packageFile(packageForVariant(variant), fileName);
    return installedFile.existsAsFile() ? installedFile : juce::File{};
}

juce::String DeepFilterService::binaryDataNameForVariant(const ModelVariant variant) const {
    juce::ignoreUnused(variant);
    return {};
}

bool DeepFilterService::extractEmbeddedModel(const ModelVariant variant, const juce::File& destination) {
    juce::ignoreUnused(variant, destination);
    return false;
}

bool DeepFilterService::prepareModelFile(const ModelVariant variant, juce::File& modelFileOut) {
    auto modelFile = modelAssetForVariant(variant);
    if (modelFile.existsAsFile()) {
        modelFileOut = modelFile;
        return true;
    }

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("vxsuite_deepfilternet_models");
    if (!tempDirectory.createDirectory())
        return false;

    const auto tempModel = tempDirectory.getChildFile(variant == ModelVariant::dfn2
        ? "DeepFilterNet2_onnx_ll.tar.gz"
        : "DeepFilterNet3_onnx.tar.gz");
    if (!tempModel.existsAsFile() && !extractEmbeddedModel(variant, tempModel))
        return false;

    modelFileOut = tempModel;
    return modelFileOut.existsAsFile();
}

bool DeepFilterService::prepareChannel(ChannelState& channel, const RuntimeBundle& bundle) {
    channel.runtime = createRuntime(bundle.runtimeApi, bundle.modelFile.getFullPathName(), 24.0f);
    if (channel.runtime == nullptr)
        return false;

    channel.resampler = std::make_unique<Resampler<1, 1>>(bundle.sampleRate, kEngineSampleRate);
    channel.inputFifo.reset(kFifoCapacity48k);
    channel.outputFifo.reset(kFifoCapacity48k);

    const int frameLength = runtimeFrameLength(bundle.runtimeApi, channel.runtime);
    const int safeFrameLength = std::max(1, frameLength);
    channel.frameIn.assign(static_cast<size_t>(safeFrameLength), 0.0f);
    channel.frameOut.assign(static_cast<size_t>(safeFrameLength), 0.0f);

    const auto maxRatio = std::max(1.0, kEngineSampleRate / bundle.sampleRate);
    const auto maxResampledSize = std::max(safeFrameLength, static_cast<int>(std::ceil(bundle.blockSize * maxRatio)) + 128);
    channel.resampleIn.assign(static_cast<size_t>(maxResampledSize), 0.0f);
    channel.resampleOut.assign(static_cast<size_t>(maxResampledSize), 0.0f);
    return true;
}

void DeepFilterService::waitForReaders(const int bundleIndex) const noexcept {
    if (bundleIndex < 0 || bundleIndex >= static_cast<int>(bundleReaders.size()))
        return;
    while (bundleReaders[static_cast<size_t>(bundleIndex)].load(std::memory_order_acquire) > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

bool DeepFilterService::prepareBundle(RuntimeBundle& bundle,
                                      const double sampleRate,
                                      const int maxBlockSize,
                                      const ModelVariant variant) {
    releaseBundle(bundle);

    bundle.sampleRate = sampleRate > 1000.0 ? sampleRate : kEngineSampleRate;
    bundle.blockSize = std::max(1, maxBlockSize);
    bundle.runtimeApi = selectedRuntimeApi(variant);
    bundle.preparedVariant = variant;
    bundle.frameLength = kDefaultFrameLength;
    bundle.latencySamples = latencySamplesForVariant(variant, bundle.sampleRate);

    if (!prepareModelFile(variant, bundle.modelFile))
        return false;

    for (auto& channel : bundle.channels) {
        if (!prepareChannel(channel, bundle)) {
            releaseBundle(bundle);
            return false;
        }
        bundle.frameLength = std::max(bundle.frameLength, static_cast<int>(channel.frameIn.size()));
    }

    bundle.ready = true;
    bundle.capability = RealtimeCapability::embeddedRuntime;
    bundle.backend = RealtimeBackend::cpu;
    bundle.backendTag = bundle.runtimeApi == RuntimeApi::dfn2 ? "libdf031:cpu" : "libdf:cpu";
    return true;
}

void DeepFilterService::prepareRealtime(const double sampleRate, const int maxBlockSize) {
    setStatus(StatusCode::rtPreparing);
    const auto variant = getModelVariant();
    const int currentActive = activeBundleIndex.load(std::memory_order_acquire);
    const int prepareIndex = currentActive == 0 ? 1 : 0;

    waitForReaders(prepareIndex);
    auto& bundle = bundles[static_cast<size_t>(prepareIndex)];
    if (!prepareBundle(bundle, sampleRate, maxBlockSize, variant)) {
        setStatus(bundle.modelFile.existsAsFile() ? StatusCode::rtInitFailed : StatusCode::rtMissingModel);
        return;
    }

    const int previousActive = activeBundleIndex.exchange(prepareIndex, std::memory_order_acq_rel);
    if (previousActive >= 0 && previousActive != prepareIndex) {
        waitForReaders(previousActive);
        releaseBundle(bundles[static_cast<size_t>(previousActive)]);
    }

    latencySamples = bundle.latencySamples;
    rtPreparedVariant = bundle.preparedVariant;
    rtReady = true;
    rtCapability = bundle.capability;
    rtBackend = bundle.backend;
    rtBackendTag = bundle.backendTag;
    resetRequested.store(false, std::memory_order_relaxed);
    setStatus(StatusCode::rtReady);
}

void DeepFilterService::resetRealtime() {
    resetRequested.store(true, std::memory_order_relaxed);
}

float DeepFilterService::attenuationLimitForStrength(const float strength) const noexcept {
    return 6.0f + 54.0f * vxsuite::clamp01(strength);
}

bool DeepFilterService::processRealtime(juce::AudioBuffer<float>& buffer,
                                        const double sampleRate,
                                        const float strength,
                                        const uint64_t key) {
    juce::ignoreUnused(key);

    const int activeIndex = activeBundleIndex.load(std::memory_order_acquire);
    if (activeIndex < 0 || !rtReady || !hasRealtimeBackend())
        return false;
    auto& bundle = bundles[static_cast<size_t>(activeIndex)];
    if (!bundle.ready)
        return false;

    const int numChannels = std::min(buffer.getNumChannels(), rtMaxChannels);
    const int numSamples = buffer.getNumSamples();
    if (numChannels <= 0 || numSamples <= 0)
        return false;

    bundleReaders[static_cast<size_t>(activeIndex)].fetch_add(1, std::memory_order_acq_rel);
    const auto attenuationLimitDb = attenuationLimitForStrength(strength);

    for (int channelIndex = 0; channelIndex < numChannels; ++channelIndex) {
        auto& channel = bundle.channels[static_cast<size_t>(channelIndex)];
        if (channel.runtime == nullptr || channel.resampler == nullptr)
            continue;

        setRuntimeAttenuation(bundle.runtimeApi, channel.runtime, attenuationLimitDb);

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

                    while (channel.inputFifo.available >= bundle.frameLength) {
                        channel.inputFifo.pop(channel.frameIn.data(), bundle.frameLength);
                        processRuntimeFrame(bundle.runtimeApi, channel.runtime, channel.frameIn.data(), channel.frameOut.data());
                        channel.outputFifo.push(channel.frameOut.data(), bundle.frameLength);
                    }

                const int outputCount = std::min(channel.outputFifo.available, sampleCount48k);
                channel.outputFifo.pop(output48k, outputCount);
                if (outputCount < sampleCount48k)
                    juce::FloatVectorOperations::clear(output48k + outputCount, sampleCount48k - outputCount);
                });
    }

    tailPrior = 0.92f * tailPrior + 0.08f * vxsuite::clamp01(strength);
    bundleReaders[static_cast<size_t>(activeIndex)].fetch_sub(1, std::memory_order_acq_rel);
    setStatus(StatusCode::rtReady);
    return true;
}

} // namespace vxsuite::deepfilternet
