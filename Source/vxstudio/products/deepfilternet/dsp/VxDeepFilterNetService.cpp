#include "VxDeepFilterNetService.h"

#include "../../../framework/VxStudioBlockSmoothing.h"
#include "../../../framework/VxStudioModelAssets.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>

#include <juce_core/juce_core.h>

#include <rnnoise.h>

namespace vxsuite::deepfilternet {

namespace {

constexpr double kEngineSampleRate = 48000.0;
constexpr int kDefaultFrameLength = 480;
constexpr int kFifoCapacity48k = 48000;
constexpr int kDfn3Latency48k = 1440; // 1 STFT frame (480) + conv_lookahead (2×480)
constexpr int kDfn2LlLatency48k = 480;
constexpr int kRnNoiseLatency48k = 480;
constexpr int kModelWarmupFrames = 8;
constexpr float kRnNoisePcmScale = 32768.0f;
constexpr float kGuardSpeechBackoff = 0.65f;
constexpr float kModelPeakLimit = 1.5f;

std::mutex& deepFilterRuntimeMutex() {
    static std::mutex mutex;
    return mutex;
}

bool isEffectivelyDualMono(const juce::AudioBuffer<float>& buffer, const int numSamples) noexcept {
    if (buffer.getNumChannels() < 2 || numSamples <= 0)
        return false;

    const auto* left = buffer.getReadPointer(0);
    const auto* right = buffer.getReadPointer(1);
    float peak = 0.0f;
    float maxDiff = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        peak = std::max(peak, std::max(std::abs(left[i]), std::abs(right[i])));
        maxDiff = std::max(maxDiff, std::abs(left[i] - right[i]));
    }

    return maxDiff <= (1.0e-6f + peak * 1.0e-4f);
}

void constrainModelFrame(const float* input, float* output, const int frameLength, float& previousOutput) noexcept {
    if (input == nullptr || output == nullptr || frameLength <= 0)
        return;

    for (int i = 0; i < frameLength; ++i) {
        const float dry = std::isfinite(input[i]) ? input[i] : 0.0f;
        float wet = output[i];
        if (!std::isfinite(wet)) {
            output[i] = dry;
            previousOutput = dry;
            continue;
        }

        const float inputStep = i == 0 ? std::abs(dry - previousOutput)
                                       : std::abs(dry - input[i - 1]);
        const float allowedStep = std::max(0.35f, inputStep * 6.0f + 0.05f);
        const float step = wet - previousOutput;
        if (std::abs(step) > allowedStep)
            wet = previousOutput + std::copysign(allowedStep, step);

        const float dynamicLimit = std::max(kModelPeakLimit, std::abs(dry) * 8.0f + 0.25f);
        output[i] = juce::jlimit(-dynamicLimit, dynamicLimit, wet);
        previousOutput = output[i];
    }
}

vxsuite::ModelPackage packageForVariant(const vxsuite::deepfilternet::DeepFilterService::ModelVariant variant) {
    if (variant == vxsuite::deepfilternet::DeepFilterService::ModelVariant::rnnoise) {
        return {
            "rnnoise",
            "RNNoise Model",
            {},
            {}
        };
    }

    if (variant == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn2) {
        return {
            "deepfilternet2",
            "DeepFilterNet 2 Model",
            {},
            { { "DeepFilterNet2_onnx_ll.tar.gz", "https://github.com/daverage/VxStudio/releases/download/models-v1/DeepFilterNet2_onnx_ll.tar.gz" } }
        };
    }

    return {
        "deepfilternet3",
        "DeepFilterNet 3 Model",
        {},
        {}
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
    switch (variant) {
        case ModelVariant::dfn2: return RuntimeApi::dfn2;
        case ModelVariant::rnnoise: return RuntimeApi::rnnoise;
        case ModelVariant::dfn3: break;
    }
    return RuntimeApi::dfn3;
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
    if (active.startupPrerollSamples48k != requestedStartupPreroll48k.load(std::memory_order_relaxed))
        return true;
    return resetRequested.load(std::memory_order_relaxed);
}

void DeepFilterService::setStartupPrerollSeconds(const double seconds) noexcept {
    const int samples48k = juce::roundToInt(std::max(0.0, seconds) * kEngineSampleRate);
    requestedStartupPreroll48k.store(samples48k, std::memory_order_relaxed);
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
        channel.startupSamplesRemaining = 0;
        channel.lastModelWetMix = 1.0f;
        channel.lastOutputSample = 0.0f;
        channel.modelWetPrimed = false;
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
    bundle.startupPrerollSamples48k = 0;
    bundle.startupHoldbackSamples48k = 0;
}

void* DeepFilterService::createRuntime(const RuntimeApi api,
                                       const juce::String& modelPath,
                                       const float attenuationLimitDb) const {
    const std::lock_guard lock(deepFilterRuntimeMutex());
    switch (api) {
        case RuntimeApi::dfn2:
            return df2_create(modelPath.toRawUTF8(), attenuationLimitDb, nullptr);
        case RuntimeApi::dfn3:
            return df_create(modelPath.toRawUTF8(), attenuationLimitDb, nullptr);
        case RuntimeApi::rnnoise:
            juce::ignoreUnused(modelPath, attenuationLimitDb);
            return rnnoise_create(nullptr);
        case RuntimeApi::none:
            break;
    }
    return nullptr;
}

void DeepFilterService::destroyRuntime(const RuntimeApi api, void* runtime) const {
    if (runtime == nullptr)
        return;

    const std::lock_guard lock(deepFilterRuntimeMutex());
    switch (api) {
        case RuntimeApi::dfn2:
            df2_free(static_cast<DF2State*>(runtime));
            return;
        case RuntimeApi::dfn3:
            df_free(static_cast<DFState*>(runtime));
            return;
        case RuntimeApi::rnnoise:
            rnnoise_destroy(static_cast<DenoiseState*>(runtime));
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
        case RuntimeApi::rnnoise:
            juce::ignoreUnused(runtime);
            return rnnoise_get_frame_size();
        case RuntimeApi::none:
            break;
    }
    return 0;
}

void DeepFilterService::setRuntimeAttenuation(const RuntimeApi api, void* runtime, const float attenuationLimitDb) const {
    if (runtime == nullptr)
        return;

    const std::lock_guard lock(deepFilterRuntimeMutex());
    switch (api) {
        case RuntimeApi::dfn2:
            df2_set_atten_lim(static_cast<DF2State*>(runtime), attenuationLimitDb);
            return;
        case RuntimeApi::dfn3:
            df_set_atten_lim(static_cast<DFState*>(runtime), attenuationLimitDb);
            return;
        case RuntimeApi::rnnoise:
            juce::ignoreUnused(runtime, attenuationLimitDb);
            return;
        case RuntimeApi::none:
            break;
    }
}

float DeepFilterService::processRuntimeFrame(const RuntimeApi api, void* runtime, float* input, float* output) const {
    if (runtime == nullptr || input == nullptr || output == nullptr)
        return 0.0f;

    const std::lock_guard lock(deepFilterRuntimeMutex());
    switch (api) {
        case RuntimeApi::dfn2:
            return df2_process_frame(static_cast<DF2State*>(runtime), input, output);
        case RuntimeApi::dfn3:
            return df_process_frame(static_cast<DFState*>(runtime), input, output);
        case RuntimeApi::rnnoise: {
            const int frameLength = rnnoise_get_frame_size();
            for (int i = 0; i < frameLength; ++i)
                output[i] = input[i] * kRnNoisePcmScale;
            const float vad = rnnoise_process_frame(static_cast<DenoiseState*>(runtime), output, output);
            for (int i = 0; i < frameLength; ++i)
                output[i] /= kRnNoisePcmScale;
            return vad;
        }
        case RuntimeApi::none:
            break;
    }
    return 0.0f;
}

juce::File DeepFilterService::modelAssetForVariant(const ModelVariant variant) const {
    if (variant == ModelVariant::dfn3)
        return juce::File{};
    const auto fileName = juce::String("DeepFilterNet2_onnx_ll.tar.gz");
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
    if (variant == ModelVariant::rnnoise || variant == ModelVariant::dfn3) {
        // rnnoise has no model file; dfn3 model is embedded in the Rust library via include_bytes!
        modelFileOut = juce::File();
        return true;
    }

    auto modelFile = modelAssetForVariant(variant);
    if (modelFile.existsAsFile()) {
        modelFileOut = modelFile;
        return true;
    }

    const auto tempDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                   .getChildFile("vxsuite_deepfilternet_models");
    if (!tempDirectory.createDirectory()) {
        // Fallback: try user application data directory
        juce::File fallbackDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                      .getChildFile("VxStudio")
                                      .getChildFile("deepfilternet_models");
        if (fallbackDir.createDirectory()) {
            const auto fallbackModel = fallbackDir.getChildFile(variant == ModelVariant::dfn2
                ? "DeepFilterNet2_onnx_ll.tar.gz"
                : "DeepFilterNet3_onnx.tar.gz");
            if (fallbackModel.existsAsFile()) {
                modelFileOut = fallbackModel;
                return true;
            }
        }
        return false;
    }

    const auto tempModel = tempDirectory.getChildFile(variant == ModelVariant::dfn2
        ? "DeepFilterNet2_onnx_ll.tar.gz"
        : "DeepFilterNet3_onnx.tar.gz");
    if (!tempModel.existsAsFile()) {
        // Try to extract embedded model with timeout protection
        if (!extractEmbeddedModel(variant, tempModel)) {
            // If extraction fails, check fallback location
            juce::File fallbackDir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                          .getChildFile("VxStudio")
                                          .getChildFile("deepfilternet_models");
            if (fallbackDir.createDirectory()) {
                const auto fallbackModel = fallbackDir.getChildFile(variant == ModelVariant::dfn2
                    ? "DeepFilterNet2_onnx_ll.tar.gz"
                    : "DeepFilterNet3_onnx.tar.gz");
                if (fallbackModel.existsAsFile()) {
                    modelFileOut = fallbackModel;
                    return true;
                }
            }
            return false;
        }
    }

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
    const int expectedFrameLength = bundle.runtimeApi == RuntimeApi::rnnoise
        ? rnnoise_get_frame_size()
        : ((bundle.runtimeApi == RuntimeApi::dfn2) ? DF2_EXPECTED_FRAME_LENGTH : DF_EXPECTED_FRAME_LENGTH);
    if (frameLength != expectedFrameLength)
        return false;
    const int safeFrameLength = std::max(1, frameLength);
    channel.frameIn.assign(static_cast<size_t>(safeFrameLength), 0.0f);
    channel.frameOut.assign(static_cast<size_t>(safeFrameLength), 0.0f);
    const auto maxRatio = std::max(1.0, kEngineSampleRate / bundle.sampleRate);
    const auto maxResampledSize = std::max(safeFrameLength, static_cast<int>(std::ceil(bundle.blockSize * maxRatio)) + 128);
    channel.resampleIn.assign(static_cast<size_t>(maxResampledSize), 0.0f);
    channel.resampleOut.assign(static_cast<size_t>(maxResampledSize), 0.0f);

    // Warm up the resampler and GRU state with silence before real audio arrives.
    for (int i = 0; i < kModelWarmupFrames; ++i)
        processRuntimeFrame(bundle.runtimeApi, channel.runtime, channel.frameIn.data(), channel.frameOut.data());

    channel.outputFifo.clear();
    channel.inputFifo.clear();
    channel.startupSamplesRemaining = bundle.startupHoldbackSamples48k;
    channel.lastModelWetMix = 1.0f;
    channel.lastOutputSample = 0.0f;
    channel.modelWetPrimed = false;
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
    const int algorithmLatency48k = variant == ModelVariant::rnnoise
        ? kRnNoiseLatency48k
        : ((variant == ModelVariant::dfn2) ? kDfn2LlLatency48k : kDfn3Latency48k);
    bundle.startupPrerollSamples48k = requestedStartupPreroll48k.load(std::memory_order_relaxed);
    bundle.startupHoldbackSamples48k = algorithmLatency48k + bundle.startupPrerollSamples48k;
    bundle.latencySamples = juce::roundToInt(static_cast<double>(algorithmLatency48k + bundle.startupPrerollSamples48k)
                                            * bundle.sampleRate / kEngineSampleRate);

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
    bundle.backendTag = bundle.runtimeApi == RuntimeApi::rnnoise
        ? "rnnoise:cpu"
        : (bundle.runtimeApi == RuntimeApi::dfn2 ? "libdf031:cpu" : "libdf:cpu");
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
        const bool modelAvailable = bundle.modelFile.existsAsFile() || variant == ModelVariant::dfn3;
    setStatus(modelAvailable ? StatusCode::rtInitFailed : StatusCode::rtMissingModel);
        return;
    }

    const int previousActive = activeBundleIndex.exchange(prepareIndex, std::memory_order_acq_rel);
    if (previousActive >= 0 && previousActive != prepareIndex) {
        waitForReaders(previousActive);
        releaseBundle(bundles[static_cast<size_t>(previousActive)]);
    }

    latencySamples = bundle.latencySamples;
    rtPreparedVariant = bundle.preparedVariant;
    rtReady.store(true, std::memory_order_release);
    rtCapability = bundle.capability;
    rtBackend = bundle.backend;
    rtBackendTag = bundle.backendTag;
    resetRequested.store(false, std::memory_order_relaxed);
    startupBypassActive.store(true, std::memory_order_release);
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
                                        const float guard,
                                        const uint64_t key) {
    juce::ignoreUnused(key, sampleRate);

    const int activeIndex = activeBundleIndex.load(std::memory_order_acquire);
    if (activeIndex < 0 || !rtReady.load(std::memory_order_acquire) || !hasRealtimeBackend())
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
    const float cleanAmount = vxsuite::clamp01(strength);
    const float guardAmount = vxsuite::clamp01(guard);
    const bool dualMonoFastPath = numChannels == 2 && isEffectivelyDualMono(buffer, numSamples);
    const int channelsToProcess = dualMonoFastPath ? 1 : numChannels;
    bool anyStartupBypass = false;

    for (int channelIndex = 0; channelIndex < channelsToProcess; ++channelIndex) {
        auto& channel = bundle.channels[static_cast<size_t>(channelIndex)];
        if (channel.runtime == nullptr || channel.resampler == nullptr)
            continue;

        float* sourceInput[]  = { buffer.getWritePointer(channelIndex) };
        float* sourceOutput[] = { buffer.getWritePointer(channelIndex) };
        float* targetInput[]  = { channel.resampleIn.data() };
        float* targetOutput[] = { channel.resampleOut.data() };

        channel.resampler->process(
            sourceInput,
            sourceOutput,
            targetInput,
            targetOutput,
            numSamples,
            [&](float* const* inputBuffers, float* const* outputBuffers, int sampleCount48k) {
                auto* input48k  = inputBuffers[0];
                auto* output48k = outputBuffers[0];

                channel.inputFifo.push(input48k, sampleCount48k);

                setRuntimeAttenuation(bundle.runtimeApi, channel.runtime, attenuationLimitDb);
                while (channel.inputFifo.available >= bundle.frameLength) {
                    channel.inputFifo.pop(channel.frameIn.data(), bundle.frameLength);
                    const float vad = vxsuite::clamp01(processRuntimeFrame(bundle.runtimeApi, channel.runtime,
                                                                            channel.frameIn.data(), channel.frameOut.data()));
                    constrainModelFrame(channel.frameIn.data(), channel.frameOut.data(),
                                        bundle.frameLength, channel.lastOutputSample);

                    if (bundle.runtimeApi == RuntimeApi::rnnoise) {
                        const float speechBackoff = guardAmount * vad * kGuardSpeechBackoff;
                        const float targetWet = vxsuite::clamp01(cleanAmount * (1.0f - speechBackoff));
                        if (!channel.modelWetPrimed) {
                            channel.lastModelWetMix = targetWet;
                            channel.modelWetPrimed = true;
                        }
                        if (targetWet < 0.9995f || channel.lastModelWetMix < 0.9995f) {
                            const float startWet = channel.lastModelWetMix;
                            for (int i = 0; i < bundle.frameLength; ++i) {
                                const float t = bundle.frameLength > 1
                                    ? static_cast<float>(i) / static_cast<float>(bundle.frameLength - 1)
                                    : 1.0f;
                                const float weight = startWet + (targetWet - startWet) * t;
                                channel.frameOut[static_cast<size_t>(i)] =
                                    channel.frameIn[static_cast<size_t>(i)]
                                    + (channel.frameOut[static_cast<size_t>(i)] - channel.frameIn[static_cast<size_t>(i)]) * weight;
                            }
                            channel.lastModelWetMix = targetWet;
                        }
                    }
                    channel.outputFifo.push(channel.frameOut.data(), bundle.frameLength);
                }

                const int toPop = std::min(channel.outputFifo.available, sampleCount48k);
                if (toPop > 0)
                    channel.outputFifo.pop(output48k, toPop);
                if (toPop < sampleCount48k)
                    juce::FloatVectorOperations::clear(output48k + toPop, sampleCount48k - toPop);

                if (channel.startupSamplesRemaining > 0) {
                    const int bypassCount = std::min(channel.startupSamplesRemaining, sampleCount48k);
                    juce::FloatVectorOperations::clear(output48k, bypassCount);
                    channel.startupSamplesRemaining -= bypassCount;
                    anyStartupBypass = true;
                }
            });
    }

    if (dualMonoFastPath)
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);

    startupBypassActive.store(anyStartupBypass, std::memory_order_release);
    bundleReaders[static_cast<size_t>(activeIndex)].fetch_sub(1, std::memory_order_acq_rel);
    setStatus(StatusCode::rtReady);
    return true;
}

} // namespace vxsuite::deepfilternet
