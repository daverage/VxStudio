#include "VxRebalanceOnnxModel.h"

#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(VXS_HAS_ONNXRUNTIME)
#include "onnxruntime_c_api.h"
#endif

namespace vxsuite::rebalance::ml {

#if defined(VXS_HAS_ONNXRUNTIME)
namespace {

std::string statusMessageAndRelease(const OrtApi* api, OrtStatus* status) {
    if (status == nullptr)
        return {};
    const char* message = api->GetErrorMessage(status);
    std::string text = message != nullptr ? message : "unknown onnxruntime error";
    api->ReleaseStatus(status);
    return text;
}

void throwOnStatus(const OrtApi* api, OrtStatus* status) {
    if (status != nullptr)
        throw std::runtime_error(statusMessageAndRelease(api, status));
}

std::vector<int64_t> tensorShape(const OrtApi* api, OrtSession* session, const size_t index, const bool input) {
    OrtTypeInfo* typeInfo = nullptr;
    throwOnStatus(api, input
        ? api->SessionGetInputTypeInfo(session, index, &typeInfo)
        : api->SessionGetOutputTypeInfo(session, index, &typeInfo));

    const OrtTensorTypeAndShapeInfo* tensorInfo = nullptr;
    throwOnStatus(api, api->CastTypeInfoToTensorInfo(typeInfo, &tensorInfo));
    size_t dimCount = 0;
    throwOnStatus(api, api->GetDimensionsCount(tensorInfo, &dimCount));

    std::vector<int64_t> dims(dimCount, 0);
    throwOnStatus(api, api->GetDimensions(tensorInfo, dims.data(), dimCount));
    api->ReleaseTypeInfo(typeInfo);
    return dims;
}

std::string allocatedName(const OrtApi* api, OrtSession* session, OrtAllocator* allocator, const size_t index, const bool input) {
    char* raw = nullptr;
    throwOnStatus(api, input
        ? api->SessionGetInputName(session, index, allocator, &raw)
        : api->SessionGetOutputName(session, index, allocator, &raw));

    std::string name = raw != nullptr ? raw : "";
    if (raw != nullptr)
        allocator->Free(allocator, raw);
    return name;
}

} // namespace
#endif

struct OnnxUmx4Model::RuntimeHandles {
#if defined(VXS_HAS_ONNXRUNTIME)
    const OrtApi* api = nullptr;
    OrtThreadingOptions* threadingOptions = nullptr;
    OrtEnv* env = nullptr;
    OrtSessionOptions* sessionOptions = nullptr;
    OrtSession* session = nullptr;
    OrtMemoryInfo* memoryInfo = nullptr;
    OrtRunOptions* runOptions = nullptr;
    std::atomic<int> pendingAsync { 0 };
#endif
};

OnnxUmx4Model::OnnxUmx4Model() = default;
OnnxUmx4Model::~OnnxUmx4Model() {
    reset();
}

bool OnnxUmx4Model::prepare(const std::string& onnxPath, std::string& errorOut) {
#if defined(VXS_HAS_ONNXRUNTIME)
    try {
        reset();
        auto runtime = std::make_unique<RuntimeHandles>();
        runtime->api = OrtGetApiBase()->GetApi(ORT_API_VERSION);
        if (runtime->api == nullptr)
            throw std::runtime_error("failed to acquire onnxruntime api");

        auto* api = runtime->api;
        throwOnStatus(api, api->CreateThreadingOptions(&runtime->threadingOptions));
        throwOnStatus(api, api->SetGlobalIntraOpNumThreads(runtime->threadingOptions, 1));
        throwOnStatus(api, api->SetGlobalInterOpNumThreads(runtime->threadingOptions, 1));
        throwOnStatus(api, api->CreateEnvWithGlobalThreadPools(ORT_LOGGING_LEVEL_WARNING, "vx_rebalance_umx4", runtime->threadingOptions, &runtime->env));
        throwOnStatus(api, api->CreateSessionOptions(&runtime->sessionOptions));
        throwOnStatus(api, api->DisablePerSessionThreads(runtime->sessionOptions));
        throwOnStatus(api, api->SetSessionGraphOptimizationLevel(runtime->sessionOptions, ORT_ENABLE_ALL));
        throwOnStatus(api, api->CreateSession(runtime->env, onnxPath.c_str(), runtime->sessionOptions, &runtime->session));
        throwOnStatus(api, api->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &runtime->memoryInfo));
        throwOnStatus(api, api->CreateRunOptions(&runtime->runOptions));

        size_t inputCount = 0;
        size_t outputCount = 0;
        throwOnStatus(api, api->SessionGetInputCount(runtime->session, &inputCount));
        throwOnStatus(api, api->SessionGetOutputCount(runtime->session, &outputCount));
        if (inputCount != 1 || outputCount != 1)
            throw std::runtime_error("unexpected Open-Unmix ONNX IO count");

        OrtAllocator* allocator = nullptr;
        throwOnStatus(api, api->GetAllocatorWithDefaultOptions(&allocator));
        inputName = allocatedName(api, runtime->session, allocator, 0, true);
        outputName = allocatedName(api, runtime->session, allocator, 0, false);

        const auto inputShape = tensorShape(api, runtime->session, 0, true);
        const auto outputShape = tensorShape(api, runtime->session, 0, false);

        const bool inputMatches = inputShape.size() == 4
            && inputShape[1] == 2
            && inputShape[2] == kModelBins;
        const bool outputMatches = outputShape.size() == 5
            && outputShape[1] == kHeadCount
            && outputShape[2] == 2
            && outputShape[3] == kModelBins
            && outputShape[4] == kModelFrames;
        if (!inputMatches || !outputMatches)
            throw std::runtime_error("unexpected Open-Unmix ONNX tensor shape");

        handles = runtime.release();
        return true;
    } catch (const std::exception& e) {
        reset();
        errorOut = e.what();
        return false;
    }
#else
    juce::ignoreUnused(onnxPath);
    errorOut = "onnxruntime not available at build time";
    return false;
#endif
}

void OnnxUmx4Model::reset() noexcept {
#if defined(VXS_HAS_ONNXRUNTIME)
    if (handles != nullptr) {
        if (handles->api != nullptr) {
            if (handles->runOptions != nullptr)
                handles->api->ReleaseRunOptions(handles->runOptions);
            if (handles->memoryInfo != nullptr)
                handles->api->ReleaseMemoryInfo(handles->memoryInfo);
            if (handles->session != nullptr)
                handles->api->ReleaseSession(handles->session);
            if (handles->sessionOptions != nullptr)
                handles->api->ReleaseSessionOptions(handles->sessionOptions);
            if (handles->env != nullptr)
                handles->api->ReleaseEnv(handles->env);
            if (handles->threadingOptions != nullptr)
                handles->api->ReleaseThreadingOptions(handles->threadingOptions);
        }
        delete handles;
        handles = nullptr;
    }
#endif
    inputName.clear();
    outputName.clear();
}

bool OnnxUmx4Model::isReady() const noexcept {
    return handles != nullptr;
}


bool OnnxUmx4Model::runAsync(const float* inputMagnitudes,
                             const int frames,
                             std::function<void(const float*, int)> onComplete,
                             std::string& errorOut) {
#if defined(VXS_HAS_ONNXRUNTIME)
    if (!isReady() || inputMagnitudes == nullptr || frames != kModelFrames || !onComplete) {
        errorOut = "Open-Unmix ONNX runAsync: precondition failed";
        return false;
    }

    try {
        auto* api = handles->api;
        const std::array<int64_t, 4> inputShape {
            1, 2, static_cast<int64_t>(kModelBins), static_cast<int64_t>(frames)
        };
        const size_t inputBytes = static_cast<size_t>(2 * kModelBins * frames) * sizeof(float);

        OrtValue* inputTensor = nullptr;
        throwOnStatus(api, api->CreateTensorWithDataAsOrtValue(handles->memoryInfo,
                                                               const_cast<float*>(inputMagnitudes),
                                                               inputBytes,
                                                               inputShape.data(),
                                                               inputShape.size(),
                                                               ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                                                               &inputTensor));

        struct Context {
            const OrtApi* api;
            OrtValue* inputTensor;
            OrtValue* outputTensor; // ORT fills this in; must outlive the dispatch
            int totalOutputFloats;
            std::atomic<int>* pendingAsync;
            std::function<void(const float*, int)> onComplete;
        };

        const int totalOut = kHeadCount * 2 * kModelBins * frames;
        auto* ctx = new Context { api, inputTensor, nullptr, totalOut, &handles->pendingAsync, std::move(onComplete) };

        const char* inputNames[]  = { inputName.c_str() };
        const char* outputNames[] = { outputName.c_str() };

        // pendingAsync incremented before dispatch so waitForPendingAsync is safe
        handles->pendingAsync.fetch_add(1, std::memory_order_relaxed);

        OrtStatus* dispatchStatus = api->RunAsync(
            handles->session, handles->runOptions,
            inputNames, &inputTensor, 1,
            outputNames, 1, &ctx->outputTensor,
            [](void* userData, OrtValue** /*outputs*/, size_t /*numOutputs*/, OrtStatusPtr cbStatus) {
                auto* ctx = static_cast<Context*>(userData);
                if (cbStatus == nullptr && ctx->outputTensor != nullptr) {
                    float* data = nullptr;
                    OrtStatus* dataStatus = ctx->api->GetTensorMutableData(ctx->outputTensor, reinterpret_cast<void**>(&data));
                    if (dataStatus != nullptr)
                        ctx->api->ReleaseStatus(dataStatus);
                    else if (data != nullptr)
                        ctx->onComplete(data, ctx->totalOutputFloats);
                    ctx->api->ReleaseValue(ctx->outputTensor);
                } else if (cbStatus != nullptr) {
                    ctx->api->ReleaseStatus(const_cast<OrtStatus*>(cbStatus));
                }
                ctx->api->ReleaseValue(ctx->inputTensor);
                ctx->pendingAsync->fetch_sub(1, std::memory_order_release);
                delete ctx;
            },
            ctx);

        if (dispatchStatus != nullptr) {
            // Dispatch failed synchronously — callback will NOT be called.
            handles->pendingAsync.fetch_sub(1, std::memory_order_relaxed);
            errorOut = statusMessageAndRelease(api, dispatchStatus);
            api->ReleaseValue(inputTensor);
            delete ctx;
            return false;
        }
        return true;
    } catch (const std::exception& e) {
        errorOut = e.what();
        return false;
    }
#else
    juce::ignoreUnused(inputMagnitudes, frames, onComplete);
    errorOut = "onnxruntime not available at build time";
    return false;
#endif
}

void OnnxUmx4Model::waitForPendingAsync() noexcept {
#if defined(VXS_HAS_ONNXRUNTIME)
    if (handles == nullptr)
        return;
    while (handles->pendingAsync.load(std::memory_order_acquire) > 0)
        std::this_thread::yield();
#endif
}

} // namespace vxsuite::rebalance::ml
