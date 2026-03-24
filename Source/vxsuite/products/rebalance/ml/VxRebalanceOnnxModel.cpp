#include "VxRebalanceOnnxModel.h"

#include <array>
#include <memory>
#include <stdexcept>
#include <string>
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
    OrtEnv* env = nullptr;
    OrtSessionOptions* sessionOptions = nullptr;
    OrtSession* session = nullptr;
    OrtMemoryInfo* memoryInfo = nullptr;
    OrtRunOptions* runOptions = nullptr;
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
        throwOnStatus(api, api->CreateEnv(ORT_LOGGING_LEVEL_WARNING, "vx_rebalance_umx4", &runtime->env));
        throwOnStatus(api, api->CreateSessionOptions(&runtime->sessionOptions));
        throwOnStatus(api, api->SetIntraOpNumThreads(runtime->sessionOptions, 1));
        throwOnStatus(api, api->SetInterOpNumThreads(runtime->sessionOptions, 1));
        throwOnStatus(api, api->SetSessionGraphOptimizationLevel(runtime->sessionOptions, ORT_ENABLE_BASIC));
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

bool OnnxUmx4Model::run(const float* inputMagnitudes,
                        const int frames,
                        float* outputMagnitudes,
                        std::string& errorOut) {
#if defined(VXS_HAS_ONNXRUNTIME)
    if (!isReady() || inputMagnitudes == nullptr || outputMagnitudes == nullptr || frames != kModelFrames) {
        errorOut = "Open-Unmix ONNX run received unexpected frame count";
        return false;
    }

    try {
        auto* api = handles->api;
        const std::array<int64_t, 4> inputShape {
            1,
            2,
            static_cast<int64_t>(kModelBins),
            static_cast<int64_t>(frames)
        };
        const std::array<int64_t, 5> outputShape {
            1,
            static_cast<int64_t>(kHeadCount),
            2,
            static_cast<int64_t>(kModelBins),
            static_cast<int64_t>(frames)
        };
        const size_t inputBytes = static_cast<size_t>(2 * kModelBins * frames) * sizeof(float);
        const size_t outputBytes = static_cast<size_t>(kHeadCount * 2 * kModelBins * frames) * sizeof(float);

        OrtValue* inputTensor = nullptr;
        OrtValue* outputTensor = nullptr;
        throwOnStatus(api, api->CreateTensorWithDataAsOrtValue(handles->memoryInfo,
                                                               const_cast<float*>(inputMagnitudes),
                                                               inputBytes,
                                                               inputShape.data(),
                                                               inputShape.size(),
                                                               ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                                                               &inputTensor));
        throwOnStatus(api, api->CreateTensorWithDataAsOrtValue(handles->memoryInfo,
                                                               outputMagnitudes,
                                                               outputBytes,
                                                               outputShape.data(),
                                                               outputShape.size(),
                                                               ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                                                               &outputTensor));

        const char* inputNames[] = { inputName.c_str() };
        const char* outputNames[] = { outputName.c_str() };
        OrtValue* outputValues[] = { outputTensor };
        OrtStatus* status = api->Run(handles->session,
                                     handles->runOptions,
                                     inputNames,
                                     &inputTensor,
                                     1,
                                     outputNames,
                                     1,
                                     outputValues);
        api->ReleaseValue(inputTensor);
        api->ReleaseValue(outputTensor);
        throwOnStatus(api, status);
        return true;
    } catch (const std::exception& e) {
        errorOut = e.what();
        return false;
    }
#else
    juce::ignoreUnused(inputMagnitudes, frames, outputMagnitudes);
    errorOut = "onnxruntime not available at build time";
    return false;
#endif
}

} // namespace vxsuite::rebalance::ml
