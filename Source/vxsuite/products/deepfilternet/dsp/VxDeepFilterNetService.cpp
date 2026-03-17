#include "VxDeepFilterNetService.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

#include <juce_core/juce_core.h>
#if JUCE_MAC || JUCE_LINUX
#include <dlfcn.h>
#endif
#if JUCE_WINDOWS
#include <windows.h>
#endif
#if defined(VXC_HAS_ORT_COREML_FACTORY) && VXC_HAS_ORT_COREML_FACTORY
#define VXC_COREML_EP_ENABLED 1
#else
#define VXC_COREML_EP_ENABLED 0
#endif
#if defined(VXC_HAS_ORT_DML_FACTORY) && VXC_HAS_ORT_DML_FACTORY
#define VXC_DML_EP_ENABLED 1
#else
#define VXC_DML_EP_ENABLED 0
#endif
#if defined(VXC_HAS_ONNXRUNTIME) && VXC_HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif
#if defined(VXC_HAS_EMBEDDED_DF_MODELS) && VXC_HAS_EMBEDDED_DF_MODELS
#include <BinaryData.h>
#endif

namespace vxsuite::deepfilternet {

namespace {
uint64_t fnv1a64Update(uint64_t h, const void* data, const size_t bytes) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < bytes; ++i) {
        h ^= static_cast<uint64_t>(p[i]);
        h *= 1099511628211ull;
    }
    return h;
}

std::vector<float> downmixToMono(const juce::AudioBuffer<float>& buffer) {
    const int channels = std::max(1, buffer.getNumChannels());
    const int samples = std::max(0, buffer.getNumSamples());
    std::vector<float> mono(static_cast<size_t>(samples), 0.0f);
    const float inv = 1.0f / static_cast<float>(channels);
    for (int ch = 0; ch < channels; ++ch) {
        const float* src = buffer.getReadPointer(ch);
        for (int i = 0; i < samples; ++i)
            mono[static_cast<size_t>(i)] += src[i] * inv;
    }
    return mono;
}

std::vector<float> resampleLinear(const std::vector<float>& in, double inSr, double outSr) {
    if (in.empty() || inSr <= 1000.0 || outSr <= 1000.0)
        return in;
    const double ratio = outSr / inSr;
    const size_t outLen = static_cast<size_t>(std::max(1.0, std::round(static_cast<double>(in.size()) * ratio)));
    std::vector<float> out(outLen, 0.0f);
    const double invRatio = inSr / outSr;
    for (size_t i = 0; i < outLen; ++i) {
        const double srcPos = static_cast<double>(i) * invRatio;
        const size_t i0 = static_cast<size_t>(std::floor(srcPos));
        const size_t i1 = std::min(i0 + 1, in.size() - 1);
        const float t = static_cast<float>(srcPos - static_cast<double>(i0));
        const float a = in[i0];
        const float b = in[i1];
        out[i] = a + (b - a) * t;
    }
    return out;
}

inline float safeValue(float x) {
    if (!std::isfinite(x)) return 0.0f;
    if (std::fpclassify(x) == FP_SUBNORMAL) return 0.0f;
    return x;
}
} // namespace

static vxsuite::deepfilternet::DeepFilterService::ModelVariant resolveModelVariantFromEnv(
    vxsuite::deepfilternet::DeepFilterService::ModelVariant fallback) {
    if (const char* env = std::getenv("VXC_DEEPFILTER_VARIANT"); env != nullptr && *env != '\0') {
        std::string s(env);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (s == "dfn2" || s == "deepfilternet2" || s == "2")
            return vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn2;
        if (s == "dfn3" || s == "deepfilternet3" || s == "3")
            return vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn3;
    }
    return fallback;
}

static juce::File findAggressiveModel(vxsuite::deepfilternet::DeepFilterService::ModelVariant variant);
static juce::File findOfficialModelBundle(vxsuite::deepfilternet::DeepFilterService::ModelVariant variant);

static juce::File extractEmbeddedModel(vxsuite::deepfilternet::DeepFilterService::ModelVariant variant) {
#if defined(VXC_HAS_EMBEDDED_DF_MODELS) && VXC_HAS_EMBEDDED_DF_MODELS
    const auto chosen = resolveModelVariantFromEnv(variant);
    const char* variantCandidates[] = {
        (chosen == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn2) ? "dfn2.onnx" : "dfn3.onnx",
        (chosen == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn2) ? "deepfilternet2.onnx" : "deepfilternet3.onnx",
        (chosen == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn2) ? "model_1.onnx" : "model_2.onnx"
    };
    const char* dfn3Aliases[] = { "aggressive.onnx", "model_2.onnx" };

    int dataSize = 0;
    const char* bytes = nullptr;
    juce::String chosenName;
    for (const auto* name : variantCandidates) {
        dataSize = 0;
        bytes = BinaryData::getNamedResource(name, dataSize);
        if (bytes != nullptr && dataSize > 0) {
            chosenName = name;
            break;
        }
    }
    if ((bytes == nullptr || dataSize <= 0)
        && chosen == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn3) {
        for (const auto* name : dfn3Aliases) {
            dataSize = 0;
            bytes = BinaryData::getNamedResource(name, dataSize);
            if (bytes != nullptr && dataSize > 0) {
                chosenName = name;
                break;
            }
        }
    }
    if (bytes == nullptr || dataSize <= 0)
        return {};

    static std::mutex writeMutex;
    const std::lock_guard<std::mutex> lock(writeMutex);

    const juce::File modelDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                    .getChildFile("vxsuite_deepfilternet_models");
    if (!modelDir.createDirectory())
        return {};
    const juce::File outFile = modelDir.getChildFile(chosenName);
    if (outFile.existsAsFile() && outFile.getSize() == static_cast<int64_t>(dataSize))
        return outFile;

    if (auto stream = std::unique_ptr<juce::FileOutputStream>(outFile.createOutputStream())) {
        stream->setPosition(0);
        stream->truncate();
        stream->write(bytes, static_cast<size_t>(dataSize));
        stream->flush();
        if (outFile.getSize() == static_cast<int64_t>(dataSize))
            return outFile;
    }
    return {};
#else
    juce::ignoreUnused(variant);
    return {};
#endif
}

DeepFilterService::~DeepFilterService() {
    stopRealtimeWorker();
}

juce::String DeepFilterService::lastStatus() const {
    switch (statusCode.load(std::memory_order_relaxed)) {
        case StatusCode::idle: return "idle";
        case StatusCode::rtBackendDisabled: return "rt_backend_disabled";
        case StatusCode::rtMissingModelDfn2: return "rt_missing_model:dfn2";
        case StatusCode::rtMissingModelDfn3: return "rt_missing_model:dfn3";
        case StatusCode::rtBundleOnlyDfn2: return "rt_bundle_only:dfn2";
        case StatusCode::rtBundleOnlyDfn3: return "rt_bundle_only:dfn3";
        case StatusCode::rtReady: return "rt_ready";
        case StatusCode::rtInitFailed: return "rt_init_failed";
        case StatusCode::rtInferFailed: return "rt_infer_failed";
        case StatusCode::rtResultOverflow: return "rt_result_overflow";
        case StatusCode::rtOutOverflow: return "rt_out_overflow";
        case StatusCode::rtInOverflow: return "rt_in_overflow";
        case StatusCode::rtDryOverflow: return "rt_dry_overflow";
        case StatusCode::rtJobOverflow: return "rt_job_overflow";
        case StatusCode::okRt: return "ok:rt";
        case StatusCode::rtReprepareNeeded: return "rt_reprepare_needed";
    }
    return "idle";
}

bool DeepFilterService::needsRealtimePrepare(const double sampleRate, const int maxBlockSize) const {
    if (sampleRate <= 1000.0 || maxBlockSize <= 0)
        return false;
    if (modelVariant != rtPreparedVariant)
        return true;
    if (std::abs(sampleRate - rtSampleRate) > 1.0)
        return true;
    const size_t needed = static_cast<size_t>(maxBlockSize);
    return rtInScratch[0].size() < needed;
}

bool DeepFilterService::hasQueuedRealtimeJobs() const {
    return rtJobRead.load(std::memory_order_acquire) != rtJobWrite.load(std::memory_order_acquire);
}

void DeepFilterService::stopRealtimeWorker() {
    rtWorkerExit.store(true, std::memory_order_release);
    rtWorkerCv.notify_all();
    if (rtWorker.joinable())
        rtWorker.join();
    rtWorkerReady.store(false, std::memory_order_release);
}

void DeepFilterService::startRealtimeWorker() {
    stopRealtimeWorker();
    rtJobRead.store(0, std::memory_order_release);
    rtJobWrite.store(0, std::memory_order_release);
    rtResultRead.store(0, std::memory_order_release);
    rtResultWrite.store(0, std::memory_order_release);
    rtWorkerExit.store(false, std::memory_order_release);
    if (!rtModelLoaded || rtBackend == RealtimeBackend::none)
        return;

    rtWorker = std::thread([this]() {
        while (!rtWorkerExit.load(std::memory_order_acquire)) {
            const size_t read = rtJobRead.load(std::memory_order_acquire);
            const size_t write = rtJobWrite.load(std::memory_order_acquire);
            if (read == write) {
                std::unique_lock<std::mutex> lock(rtWorkerMutex);
                rtWorkerCv.wait_for(lock,
                                    std::chrono::milliseconds(2),
                                    [this]() {
                                        return rtWorkerExit.load(std::memory_order_acquire)
                                            || hasQueuedRealtimeJobs();
                                    });
                continue;
            }

            const RtFrameSlot& slot = rtJobQueue[read % rtQueueCapacity];
            const int ch = juce::jlimit(0, rtMaxChannels - 1, slot.channel);
            auto& outFrame = rtFrameOut[static_cast<size_t>(ch)];
            if (outFrame.size() != rtFrameSamples)
                outFrame.assign(rtFrameSamples, 0.0f);

            bool ok = false;
            if (slot.frame.size() >= rtFrameSamples) {
                ok = runRealtimeModelFrame(slot.frame.data(),
                                           outFrame.data(),
                                           slot.attenLimDb,
                                           rtStates[static_cast<size_t>(ch)]);
            }
            if (!ok) {
                setStatus(StatusCode::rtInferFailed);
                std::copy_n(slot.frame.data(),
                            std::min(slot.frame.size(), outFrame.size()),
                            outFrame.data());
            }

            const size_t resultWrite = rtResultWrite.load(std::memory_order_relaxed);
            const size_t resultRead = rtResultRead.load(std::memory_order_acquire);
            if (resultWrite - resultRead < rtQueueCapacity) {
                RtFrameSlot& resultSlot = rtResultQueue[resultWrite % rtQueueCapacity];
                resultSlot.channel = ch;
                resultSlot.attenLimDb = slot.attenLimDb;
                if (resultSlot.frame.size() != rtFrameSamples)
                    resultSlot.frame.assign(rtFrameSamples, 0.0f);
                std::copy_n(outFrame.data(), rtFrameSamples, resultSlot.frame.data());
                rtResultWrite.store(resultWrite + 1, std::memory_order_release);
            } else {
                setStatus(StatusCode::rtResultOverflow);
            }

            rtJobRead.store(read + 1, std::memory_order_release);
        }
    });
    rtWorkerReady.store(true, std::memory_order_release);
}

bool DeepFilterService::enqueueRealtimeJob(const int channel, const float* inputFrame, const float attenLimDb) {
    const size_t write = rtJobWrite.load(std::memory_order_relaxed);
    const size_t read = rtJobRead.load(std::memory_order_acquire);
    if (write - read >= rtQueueCapacity)
        return false;

    RtFrameSlot& slot = rtJobQueue[write % rtQueueCapacity];
    slot.channel = channel;
    slot.attenLimDb = attenLimDb;
    if (slot.frame.size() != rtFrameSamples)
        slot.frame.assign(rtFrameSamples, 0.0f);
    std::copy_n(inputFrame, rtFrameSamples, slot.frame.data());
    rtJobWrite.store(write + 1, std::memory_order_release);
    rtWorkerCv.notify_one();
    return true;
}

void DeepFilterService::drainRealtimeResults() {
    size_t read = rtResultRead.load(std::memory_order_acquire);
    const size_t write = rtResultWrite.load(std::memory_order_acquire);
    while (read != write) {
        const RtFrameSlot& slot = rtResultQueue[read % rtQueueCapacity];
        const int ch = juce::jlimit(0, rtMaxChannels - 1, slot.channel);
        auto& outQ = rtOutQueue48[static_cast<size_t>(ch)];
        for (size_t i = 0; i < std::min(rtFrameSamples, slot.frame.size()); ++i) {
            if (!outQ.push(slot.frame[i]))
                setStatus(StatusCode::rtOutOverflow);
        }
        ++read;
    }
    rtResultRead.store(read, std::memory_order_release);
}

void DeepFilterService::RtFifo::reset(const size_t cap) {
    data.assign(std::max<size_t>(1, cap), 0.0f);
    readPos = 0;
    writePos = 0;
    available = 0;
}

void DeepFilterService::RtFifo::clear() {
    readPos = 0;
    writePos = 0;
    available = 0;
}

bool DeepFilterService::RtFifo::push(const float v) {
    if (available >= data.size())
        return false;
    data[writePos] = v;
    writePos = (writePos + 1) % data.size();
    ++available;
    return true;
}

bool DeepFilterService::RtFifo::pop(float& out) {
    if (available == 0)
        return false;
    out = data[readPos];
    readPos = (readPos + 1) % data.size();
    --available;
    return true;
}

void DeepFilterService::selectRealtimeBackend() {
    if (const char* env = std::getenv("VXC_RT_BACKEND"); env != nullptr && *env != '\0') {
        std::string s(env);
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (s == "none" || s == "off") {
            rtBackend = RealtimeBackend::none;
            rtBackendTag = "none";
            return;
        }
    }
#if defined(VXC_HAS_ONNXRUNTIME) && VXC_HAS_ONNXRUNTIME
    rtBackend = RealtimeBackend::cpu;
    rtBackendTag = "onnx:auto";
#else
    rtBackend = RealtimeBackend::none;
    rtBackendTag = "disabled";
#endif
}

static juce::File findBundledDeepFilterBinary(vxsuite::deepfilternet::DeepFilterService::ModelVariant variant) {
    const auto chosen = resolveModelVariantFromEnv(variant);

    if (chosen == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn2) {
        if (const char* envBin = std::getenv("VXC_DEEPFILTER_BIN_DFN2"); envBin != nullptr && *envBin != '\0') {
            juce::File f{ juce::String(envBin) };
            if (f.existsAsFile())
                return f;
        }
    } else {
        if (const char* envBin = std::getenv("VXC_DEEPFILTER_BIN_DFN3"); envBin != nullptr && *envBin != '\0') {
            juce::File f{ juce::String(envBin) };
            if (f.existsAsFile())
                return f;
        }
    }

    if (const char* envBin = std::getenv("VXC_DEEPFILTER_BIN"); envBin != nullptr && *envBin != '\0') {
        juce::File f{ juce::String(envBin) };
        if (f.existsAsFile())
            return f;
    }

#if JUCE_MAC || JUCE_LINUX
    Dl_info info {};
    if (dladdr(reinterpret_cast<const void*>(&findBundledDeepFilterBinary), &info) != 0
        && info.dli_fname != nullptr) {
        juce::File module(juce::String(info.dli_fname));
        const juce::File candidates[] = {
            module.getSiblingFile("../Resources/deep-filter"),
            module.getSiblingFile("../Resources/deep-filter-mac"),
            module.getSiblingFile("../Resources/deep-filter.exe"),
            module.getSiblingFile("../Resources/deepfilter/mac/deep-filter"),
            module.getSiblingFile("../Resources/deepfilter/win/deep-filter.exe")
        };
        for (const auto& c : candidates) {
            if (c.existsAsFile())
                return c;
        }
    }
#endif

#if JUCE_WINDOWS
    HMODULE hm = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&findBundledDeepFilterBinary),
                           &hm)) {
        char path[MAX_PATH] = {0};
        if (GetModuleFileNameA(hm, path, MAX_PATH) > 0) {
            juce::File module(juce::String(path));
            const juce::File candidates[] = {
                module.getSiblingFile("../Resources/deep-filter.exe"),
                module.getSiblingFile("../Resources/deep-filter-windows.exe"),
                module.getSiblingFile("../Resources/deep-filter"),
                module.getSiblingFile("../Resources/deepfilter/win/deep-filter.exe"),
                module.getSiblingFile("../Resources/deepfilter/mac/deep-filter")
            };
            for (const auto& c : candidates) {
                if (c.existsAsFile())
                    return c;
            }
        }
    }
#endif

    // Development fallback: allow running harness/tools from repo root without a packaged VST3.
    {
        const juce::File cwd = juce::File::getCurrentWorkingDirectory();
        const juce::File candidates[] = {
            cwd.getChildFile("assets/deepfilter/deep-filter-mac"),
            cwd.getChildFile("assets/deepfilter/deep-filter"),
            cwd.getChildFile("assets/deepfilter/deep-filter-windows.exe"),
            cwd.getChildFile("../assets/deepfilter/deep-filter-mac"),
            cwd.getChildFile("../assets/deepfilter/deep-filter"),
            cwd.getChildFile("../assets/deepfilter/deep-filter-windows.exe")
        };
        for (const auto& c : candidates) {
            if (c.existsAsFile())
                return c;
        }
    }

    return {};
}

static juce::File findAggressiveModel(vxsuite::deepfilternet::DeepFilterService::ModelVariant variant) {
    const auto chosen = resolveModelVariantFromEnv(variant);
    if (chosen == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn2) {
        if (const char* envModel = std::getenv("VXC_DEEPFILTER_MODEL_DFN2"); envModel != nullptr && *envModel != '\0') {
            juce::File f{ juce::String(envModel) };
            if (f.existsAsFile())
                return f;
        }
    } else {
        if (const char* envModel = std::getenv("VXC_DEEPFILTER_MODEL_DFN3"); envModel != nullptr && *envModel != '\0') {
            juce::File f{ juce::String(envModel) };
            if (f.existsAsFile())
                return f;
        }
    }

    if (const char* envModel = std::getenv("VXC_AGGRESSIVE_MODEL"); envModel != nullptr && *envModel != '\0') {
        juce::File f{ juce::String(envModel) };
        if (f.existsAsFile())
            return f;
    }

    if (const auto embedded = extractEmbeddedModel(chosen); embedded.existsAsFile())
        return embedded;

#if JUCE_MAC || JUCE_LINUX
    Dl_info info {};
    if (dladdr(reinterpret_cast<const void*>(&findBundledDeepFilterBinary), &info) != 0
        && info.dli_fname != nullptr) {
        juce::File module(juce::String(info.dli_fname));
        const juce::File candidates[] = {
            chosen == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn2
                ? module.getSiblingFile("../Resources/models/dfn2.onnx")
                : module.getSiblingFile("../Resources/models/dfn3.onnx"),
            chosen == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn2
                ? module.getSiblingFile("../Resources/models/deepfilternet2.onnx")
                : module.getSiblingFile("../Resources/models/dfn3.onnx"),
            chosen == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn3
                ? module.getSiblingFile("../Resources/models/aggressive.onnx")
                : juce::File{}
        };
        for (const auto& c : candidates) {
            if (c != juce::File{} && c.existsAsFile())
                return c;
        }
    }
#endif

    // Development fallback for local harness/tools execution.
    {
        const juce::File cwd = juce::File::getCurrentWorkingDirectory();
        const juce::File candidates[] = {
            chosen == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn2
                ? cwd.getChildFile("assets/deepfilternet/models/dfn2.onnx")
                : cwd.getChildFile("assets/deepfilternet/models/dfn3.onnx"),
            chosen == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn2
                ? cwd.getChildFile("assets/deepfilternet/models/deepfilternet2.onnx")
                : cwd.getChildFile("assets/deepfilternet/models/dfn3.onnx"),
            chosen == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn3
                ? cwd.getChildFile("assets/deepfilternet/models/aggressive.onnx")
                : juce::File{},
            chosen == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn2
                ? cwd.getChildFile("../assets/deepfilternet/models/dfn2.onnx")
                : cwd.getChildFile("../assets/deepfilternet/models/dfn3.onnx"),
            chosen == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn2
                ? cwd.getChildFile("../assets/deepfilternet/models/deepfilternet2.onnx")
                : cwd.getChildFile("../assets/deepfilternet/models/dfn3.onnx"),
            chosen == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn3
                ? cwd.getChildFile("../assets/deepfilternet/models/aggressive.onnx")
                : juce::File{}
        };
        for (const auto& c : candidates) {
            if (c != juce::File{} && c.existsAsFile())
                return c;
        }
    }
    return {};
}

static juce::File findOfficialModelBundle(vxsuite::deepfilternet::DeepFilterService::ModelVariant variant) {
    const auto chosen = resolveModelVariantFromEnv(variant);
    const juce::String bundleName = chosen == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn2 ? "dfn2" : "dfn3";

#if JUCE_MAC || JUCE_LINUX
    Dl_info info {};
    if (dladdr(reinterpret_cast<const void*>(&findBundledDeepFilterBinary), &info) != 0
        && info.dli_fname != nullptr) {
        juce::File module(juce::String(info.dli_fname));
        const juce::File bundle = module.getSiblingFile("../Resources/models/" + bundleName);
        if (bundle.isDirectory())
            return bundle;
    }
#endif

    const juce::File cwd = juce::File::getCurrentWorkingDirectory();
    const juce::File candidates[] = {
        cwd.getChildFile("assets/deepfilternet/models/" + bundleName),
        cwd.getChildFile("../assets/deepfilternet/models/" + bundleName)
    };
    for (const auto& candidate : candidates) {
        if (candidate.isDirectory())
            return candidate;
    }
    return {};
}

void DeepFilterService::prepareRealtime(const double sampleRate, const int maxBlockSize) {
    stopRealtimeWorker();
    selectRealtimeBackend();
    rtPreparedVariant = modelVariant;
    rtCapability = RealtimeCapability::unavailable;
    rtSampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    const size_t cap = static_cast<size_t>(std::max(1, maxBlockSize));
    const size_t cap48 = std::max<size_t>(16, static_cast<size_t>(std::round(cap * (48000.0 / rtSampleRate))) + 32);
    const size_t queueCap48 = std::max<size_t>(8192, cap48 * 16);
    for (int ch = 0; ch < rtMaxChannels; ++ch) {
        rtInScratch[static_cast<size_t>(ch)].assign(cap, 0.0f);
        rtOutScratch[static_cast<size_t>(ch)].assign(cap, 0.0f);
        rtDryLp[static_cast<size_t>(ch)].assign(cap, 0.0f);
        rt48In[static_cast<size_t>(ch)].assign(cap48, 0.0f);
        rt48Out[static_cast<size_t>(ch)].assign(cap48, 0.0f);
        rt48Dry[static_cast<size_t>(ch)].assign(cap48, 0.0f);
        rtInQueue48[static_cast<size_t>(ch)].reset(queueCap48);
        rtOutQueue48[static_cast<size_t>(ch)].reset(queueCap48);
        rtDryQueue48[static_cast<size_t>(ch)].reset(queueCap48);
        rtDownsampler[static_cast<size_t>(ch)].reset();
        rtUpsampler[static_cast<size_t>(ch)].reset();
        rtUpsamplerDry[static_cast<size_t>(ch)].reset();
        rtFrameIn[static_cast<size_t>(ch)].assign(rtFrameSamples, 0.0f);
        rtFrameOut[static_cast<size_t>(ch)].assign(rtFrameSamples, 0.0f);
    }
    const size_t nativeLatency = static_cast<size_t>(std::round(480.0 * rtSampleRate / 48000.0));
    for (auto& ed : rtExtraChannelDelays) {
        ed.buffer.assign(nativeLatency + cap + 32, 0.0f);
        ed.readPos = ed.writePos = ed.available = 0;
    }
    rtModelLoaded = false;
#if defined(VXC_HAS_ONNXRUNTIME) && VXC_HAS_ONNXRUNTIME
    if (rtBackend == RealtimeBackend::none) {
        setStatus(StatusCode::rtBackendDisabled);
        return;
    }
    const auto modelFile = findAggressiveModel(modelVariant);
    if (!modelFile.existsAsFile()) {
        const auto officialBundle = findOfficialModelBundle(modelVariant);
        if (officialBundle.isDirectory()) {
            rtCapability = RealtimeCapability::officialBundleOnly;
            setStatus(modelVariant == ModelVariant::dfn2
                ? StatusCode::rtBundleOnlyDfn2
                : StatusCode::rtBundleOnlyDfn3);
        } else {
            setStatus(modelVariant == ModelVariant::dfn2 ? StatusCode::rtMissingModelDfn2 : StatusCode::rtMissingModelDfn3);
        }
        return;
    }

    auto normalizedShape = [](std::vector<int64_t> shape, const size_t required) {
        if (shape.empty())
            shape = { static_cast<int64_t>(required) };
        for (auto& d : shape) {
            if (d <= 0)
                d = 1;
        }
        size_t product = 1;
        for (const auto d : shape)
            product *= static_cast<size_t>(d);
        if (product == required)
            return shape;
        shape.clear();
        shape.push_back(static_cast<int64_t>(required));
        return shape;
    };

    try {
        if (!rtEnv)
            rtEnv = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "VxDeepFilterNetRt");

        rtOpts = Ort::SessionOptions{};
        rtOpts.SetIntraOpNumThreads(1);
        rtOpts.SetInterOpNumThreads(1);
        rtOpts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL);
        rtOpts.DisableCpuMemArena();
        rtOpts.DisableMemPattern();

        std::string pref = "auto";
        if (const char* env = std::getenv("VXC_RT_BACKEND"); env != nullptr && *env != '\0') {
            pref = env;
            std::transform(pref.begin(), pref.end(), pref.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        }
        const bool forceCpu = (pref == "cpu");
        const bool requestGpu = !forceCpu;
        bool gpuEnabled = false;
        juce::String selectedProvider = "cpu";

        auto tryAppendProvider = [&](const std::string& providerName,
                                     const std::unordered_map<std::string, std::string>& options = {}) -> bool {
            try {
                rtOpts.AppendExecutionProvider(providerName, options);
                selectedProvider = juce::String(providerName);
                return true;
            } catch (...) {
                return false;
            }
        };

        if (requestGpu) {
#if JUCE_MAC
            if (VXC_COREML_EP_ENABLED && (pref == "auto" || pref == "gpu" || pref == "coreml"))
                gpuEnabled = tryAppendProvider("CoreML");
#elif JUCE_WINDOWS
            if (VXC_DML_EP_ENABLED && (pref == "auto" || pref == "gpu" || pref == "dml"))
                gpuEnabled = tryAppendProvider("DML") || tryAppendProvider("Dml");
            if (!gpuEnabled && (pref == "auto" || pref == "gpu" || pref == "cuda"))
                gpuEnabled = tryAppendProvider("CUDA") || tryAppendProvider("CUDAExecutionProvider");
#else
            if (pref == "auto" || pref == "gpu" || pref == "cuda")
                gpuEnabled = tryAppendProvider("CUDA") || tryAppendProvider("CUDAExecutionProvider");
#endif
        }
        if (!gpuEnabled) {
            // CPU EP is available as the default fallback even when provider factory helpers
            // are not exposed in this ORT build.
            selectedProvider = "cpu";
            rtBackend = RealtimeBackend::cpu;
        } else {
            rtBackend = RealtimeBackend::gpu;
        }
        rtBackendTag = "onnx:" + selectedProvider;
        rtCapability = RealtimeCapability::monolithicSession;

        rtSession = std::make_unique<Ort::Session>(*rtEnv, modelFile.getFullPathName().toRawUTF8(), rtOpts);

        Ort::AllocatorWithDefaultOptions allocator;
        const int inCount = static_cast<int>(rtSession->GetInputCount());
        const int outCount = static_cast<int>(rtSession->GetOutputCount());
        int frameInIdx = -1;
        int stateInIdx = -1;
        int attenInIdx = -1;
        auto lowerName = [](std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        };
        for (int i = 0; i < inCount; ++i) {
            const auto allocatedName = rtSession->GetInputNameAllocated(i, allocator);
            const std::string inputName = lowerName(allocatedName.get() != nullptr ? allocatedName.get() : "");
            const auto typeInfo = rtSession->GetInputTypeInfo(i);
            const auto info = typeInfo.GetTensorTypeAndShapeInfo();
            const auto shape = info.GetShape();
            size_t count = 1;
            for (auto d : shape)
                count *= static_cast<size_t>(d > 0 ? d : 1);
            if (inputName.find("state") != std::string::npos)
                stateInIdx = i;
            else if (inputName.find("atten") != std::string::npos || inputName.find("db") != std::string::npos)
                attenInIdx = i;
            else if (inputName.find("frame") != std::string::npos
                     || inputName.find("audio") != std::string::npos
                     || inputName.find("input") != std::string::npos)
                frameInIdx = i;
            else if (count == 480)
                frameInIdx = i;
            else if (count >= 4096)
                stateInIdx = i;
            else
                attenInIdx = i;
        }
        if (frameInIdx < 0) frameInIdx = 0;
        if (stateInIdx < 0) stateInIdx = 1;
        if (attenInIdx < 0) attenInIdx = std::min(2, inCount - 1);

        int frameOutIdx = -1;
        int stateOutIdx = -1;
        for (int i = 0; i < outCount; ++i) {
            const auto allocatedName = rtSession->GetOutputNameAllocated(i, allocator);
            const std::string outputName = lowerName(allocatedName.get() != nullptr ? allocatedName.get() : "");
            const auto typeInfo = rtSession->GetOutputTypeInfo(i);
            const auto info = typeInfo.GetTensorTypeAndShapeInfo();
            const auto shape = info.GetShape();
            size_t count = 1;
            for (auto d : shape)
                count *= static_cast<size_t>(d > 0 ? d : 1);
            if (outputName.find("state") != std::string::npos)
                stateOutIdx = i;
            else if (outputName.find("frame") != std::string::npos
                     || outputName.find("audio") != std::string::npos
                     || outputName.find("output") != std::string::npos
                     || outputName.find("enh") != std::string::npos)
                frameOutIdx = i;
            else if (count == 480)
                frameOutIdx = i;
            else if (count >= 4096)
                stateOutIdx = i;
        }
        if (frameOutIdx < 0) frameOutIdx = 0;
        if (stateOutIdx < 0) stateOutIdx = std::min(1, outCount - 1);

        {
            auto n = rtSession->GetInputNameAllocated(frameInIdx, allocator);
            rtInputFrameName = n.get();
            auto s = rtSession->GetInputNameAllocated(stateInIdx, allocator);
            rtInputStateName = s.get();
            auto a = rtSession->GetInputNameAllocated(attenInIdx, allocator);
            rtInputAttenName = a.get();
            auto o0 = rtSession->GetOutputNameAllocated(frameOutIdx, allocator);
            rtOutputFrameName = o0.get();
            auto o1 = rtSession->GetOutputNameAllocated(stateOutIdx, allocator);
            rtOutputStateName = o1.get();
        }

        const auto frameTypeInfo = rtSession->GetInputTypeInfo(frameInIdx);
        const auto stateTypeInfo = rtSession->GetInputTypeInfo(stateInIdx);
        const auto attenTypeInfo = rtSession->GetInputTypeInfo(attenInIdx);
        const auto rawFrameShape = frameTypeInfo.GetTensorTypeAndShapeInfo().GetShape();
        size_t frameElements = 1;
        for (const auto d : rawFrameShape)
            frameElements *= static_cast<size_t>(d > 0 ? d : 1);
        frameElements = std::max<size_t>(1, frameElements);
        rtFrameShape = normalizedShape(rawFrameShape, frameElements);
        rtFrameSamples = 1;
        for (const auto d : rtFrameShape)
            rtFrameSamples *= static_cast<size_t>(d > 0 ? d : 1);
        rtFrameSamples = std::max<size_t>(1, rtFrameSamples);
        rtModelLatency48 = std::max<size_t>(rtFrameSamples * 2, 960);
        rtStateShape = stateTypeInfo.GetTensorTypeAndShapeInfo().GetShape();
        rtAttenShape = attenTypeInfo.GetTensorTypeAndShapeInfo().GetShape();
        if (rtStateShape.empty())
            rtStateShape = { 45304 };
        for (auto& d : rtStateShape) {
            if (d <= 0)
                d = 1;
        }
        size_t stateCount = 1;
        for (const auto d : rtStateShape)
            stateCount *= static_cast<size_t>(d);
        for (int ch = 0; ch < rtMaxChannels; ++ch) {
            rtFrameIn[static_cast<size_t>(ch)].assign(rtFrameSamples, 0.0f);
            rtFrameOut[static_cast<size_t>(ch)].assign(rtFrameSamples, 0.0f);
            rtStates[static_cast<size_t>(ch)].assign(std::max<size_t>(1, stateCount), 0.0f);
        }
        rtQueueCapacity = std::max<size_t>(32, std::min<size_t>(128, cap48 / std::max<size_t>(1, rtFrameSamples / 4) + 24));
        rtJobQueue.assign(rtQueueCapacity, RtFrameSlot{});
        rtResultQueue.assign(rtQueueCapacity, RtFrameSlot{});
        for (auto& slot : rtJobQueue)
            slot.frame.assign(rtFrameSamples, 0.0f);
        for (auto& slot : rtResultQueue)
            slot.frame.assign(rtFrameSamples, 0.0f);
        rtModelLoaded = true;
        setStatus(StatusCode::rtReady);
    } catch (const Ort::Exception& e) {
        rtModelLoaded = false;
        juce::ignoreUnused(e);
        setStatus(StatusCode::rtInitFailed);
    } catch (const std::exception& e) {
        rtModelLoaded = false;
        juce::ignoreUnused(e);
        setStatus(StatusCode::rtInitFailed);
    } catch (...) {
        rtModelLoaded = false;
        setStatus(StatusCode::rtInitFailed);
    }
#else
    juce::ignoreUnused(maxBlockSize);
    setStatus(StatusCode::rtBackendDisabled);
#endif
    if (rtModelLoaded)
        startRealtimeWorker();
}

void DeepFilterService::resetRealtime() {
    for (int ch = 0; ch < rtMaxChannels; ++ch) {
        rtInQueue48[static_cast<size_t>(ch)].clear();
        rtOutQueue48[static_cast<size_t>(ch)].clear();
        rtDryQueue48[static_cast<size_t>(ch)].clear();
        rtDownsampler[static_cast<size_t>(ch)].reset();
        rtUpsampler[static_cast<size_t>(ch)].reset();
        rtUpsamplerDry[static_cast<size_t>(ch)].reset();
        std::fill(rtFrameIn[static_cast<size_t>(ch)].begin(), rtFrameIn[static_cast<size_t>(ch)].end(), 0.0f);
        std::fill(rtFrameOut[static_cast<size_t>(ch)].begin(), rtFrameOut[static_cast<size_t>(ch)].end(), 0.0f);
    }
    rtJobRead.store(0, std::memory_order_release);
    rtJobWrite.store(0, std::memory_order_release);
    rtResultRead.store(0, std::memory_order_release);
    rtResultWrite.store(0, std::memory_order_release);
    for (auto& ed : rtExtraChannelDelays) {
        std::fill(ed.buffer.begin(), ed.buffer.end(), 0.0f);
        ed.readPos = ed.writePos = ed.available = 0;
    }
#if defined(VXC_HAS_ONNXRUNTIME) && VXC_HAS_ONNXRUNTIME
    for (int ch = 0; ch < rtMaxChannels; ++ch)
        std::fill(rtStates[static_cast<size_t>(ch)].begin(), rtStates[static_cast<size_t>(ch)].end(), 0.0f);
#endif
}

bool DeepFilterService::runRealtimeModelFrame(const float* input480,
                                              float* output480,
                                              const float attenLimDb,
                                              std::vector<float>& state) {
#if defined(VXC_HAS_ONNXRUNTIME) && VXC_HAS_ONNXRUNTIME
    if (!rtModelLoaded || !rtSession)
        return false;

    try {
        auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        float atten = attenLimDb;
        auto frameTensor = Ort::Value::CreateTensor<float>(mem,
                                                            const_cast<float*>(input480),
                                                            rtFrameSamples,
                                                            rtFrameShape.data(),
                                                            rtFrameShape.size());
        auto stateTensor = Ort::Value::CreateTensor<float>(mem,
                                                            state.data(),
                                                            state.size(),
                                                            rtStateShape.data(),
                                                            rtStateShape.size());
        const int64_t scalarDims[1] = { 1 };
        const int64_t* attenDims = rtAttenShape.empty() ? scalarDims : rtAttenShape.data();
        const size_t attenRank = rtAttenShape.empty() ? 0 : rtAttenShape.size();
        auto attenTensor = Ort::Value::CreateTensor<float>(mem,
                                                            &atten,
                                                            1,
                                                            attenDims,
                                                            attenRank);

        const char* inNames[] = {
            rtInputFrameName.c_str(),
            rtInputStateName.c_str(),
            rtInputAttenName.c_str()
        };
        const char* outNames[] = {
            rtOutputFrameName.c_str(),
            rtOutputStateName.c_str()
        };
        std::array<Ort::Value, 3> inputs{
            std::move(frameTensor),
            std::move(stateTensor),
            std::move(attenTensor)
        };
        auto outputs = rtSession->Run(Ort::RunOptions{nullptr},
                                            inNames,
                                            inputs.data(),
                                            inputs.size(),
                                            outNames,
                                            2);
        if (outputs.size() != 2 || !outputs[0].IsTensor() || !outputs[1].IsTensor())
            return false;

        const float* frameOut = outputs[0].GetTensorData<float>();
        const size_t frameCount = static_cast<size_t>(outputs[0].GetTensorTypeAndShapeInfo().GetElementCount());
        if (frameOut == nullptr || frameCount == 0)
            return false;
        const size_t frameCopy = std::min(rtFrameSamples, frameCount);
        std::fill(output480, output480 + rtFrameSamples, 0.0f);
        std::copy_n(frameOut, frameCopy, output480);

        const float* stateOut = outputs[1].GetTensorData<float>();
        const size_t stateCount = static_cast<size_t>(outputs[1].GetTensorTypeAndShapeInfo().GetElementCount());
        if (stateOut != nullptr && stateCount > 0) {
            const size_t copyCount = std::min(stateCount, state.size());
            std::copy_n(stateOut, copyCount, state.begin());
        }
        return true;
    } catch (...) {
        return false;
    }
#else
    juce::ignoreUnused(input480, output480, attenLimDb);
    return false;
#endif
}

bool DeepFilterService::processRealtime(juce::AudioBuffer<float>& buffer,
                                        const double sampleRate,
                                        const float strength,
                                        const uint64_t key) {
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused(key);
    if (buffer.getNumChannels() <= 0 || buffer.getNumSamples() <= 0)
        return false;
    const float wet = juce::jlimit(0.0f, 1.0f, strength);
    if (wet <= 1.0e-4f)
        return false;
    if (!rtModelLoaded || rtCapability != RealtimeCapability::monolithicSession) {
        if (needsRealtimePrepare(sampleRate, buffer.getNumSamples()))
            setStatus(StatusCode::rtReprepareNeeded);
        return false;
    }
    if (needsRealtimePrepare(sampleRate, buffer.getNumSamples())) {
        setStatus(StatusCode::rtReprepareNeeded);
        return false;
    }

    const int channels = buffer.getNumChannels();
    const int samples = buffer.getNumSamples();
    const int processChannels = std::min(channels, rtMaxChannels);
    for (int ch = 0; ch < processChannels; ++ch) {
        auto& scratch = rtInScratch[static_cast<size_t>(ch)];
        const float* src = buffer.getReadPointer(ch);
        for (int i = 0; i < samples; ++i)
            scratch[static_cast<size_t>(i)] = src[i];
    }

    const int out48Count = std::max(1, juce::roundToInt(static_cast<double>(samples) * 48000.0 / rtSampleRate));
    for (int ch = 0; ch < processChannels; ++ch) {
        auto& ch48In = rt48In[static_cast<size_t>(ch)];
        auto& ch48Out = rt48Out[static_cast<size_t>(ch)];
        auto& ch48Dry = rt48Dry[static_cast<size_t>(ch)];
        if (ch48In.size() < static_cast<size_t>(out48Count + 8))
            return false;
    }
    const double downRatio = rtSampleRate / 48000.0;
    for (int ch = 0; ch < processChannels; ++ch) {
        auto& chIn = rtInScratch[static_cast<size_t>(ch)];
        auto& ch48In = rt48In[static_cast<size_t>(ch)];
        rtDownsampler[static_cast<size_t>(ch)].process(downRatio, chIn.data(), ch48In.data(), out48Count);
    }

    for (int ch = 0; ch < processChannels; ++ch) {
        auto& q = rtInQueue48[static_cast<size_t>(ch)];
        auto& dq = rtDryQueue48[static_cast<size_t>(ch)];
        auto& ch48In = rt48In[static_cast<size_t>(ch)];
        for (int i = 0; i < out48Count; ++i) {
            const float s = ch48In[static_cast<size_t>(i)];
            if (!q.push(s))
                setStatus(StatusCode::rtInOverflow);
            if (!dq.push(s))
                setStatus(StatusCode::rtDryOverflow);
        }
    }

    // Match offline attenuation mapping to avoid over-gating in realtime.
    const float attenDb = juce::jlimit(0.0f, 20.0f, 3.0f + 17.0f * wet);
    auto hasFrame = [&](const int ch) -> bool {
        return rtInQueue48[static_cast<size_t>(ch)].available >= rtFrameSamples;
    };
    while (hasFrame(0) && (processChannels < 2 || hasFrame(1))) {
        for (int ch = 0; ch < processChannels; ++ch) {
            auto& q = rtInQueue48[static_cast<size_t>(ch)];
            auto& frameIn = rtFrameIn[static_cast<size_t>(ch)];
            for (size_t i = 0; i < rtFrameSamples; ++i) {
                q.pop(frameIn[static_cast<size_t>(i)]);
            }
        }
        for (int ch = 0; ch < processChannels; ++ch) {
            auto& frameIn = rtFrameIn[static_cast<size_t>(ch)];
            if (!enqueueRealtimeJob(ch, frameIn.data(), attenDb))
                setStatus(StatusCode::rtJobOverflow);
        }
    }
    drainRealtimeResults();

    for (int ch = 0; ch < processChannels; ++ch) {
        auto& outQ = rtOutQueue48[static_cast<size_t>(ch)];
        auto& dryQ = rtDryQueue48[static_cast<size_t>(ch)];
        auto& ch48Out = rt48Out[static_cast<size_t>(ch)];
        auto& ch48Dry = rt48Dry[static_cast<size_t>(ch)];
        for (int i = 0; i < out48Count; ++i) {
            if (outQ.available > 0) {
                outQ.pop(ch48Out[static_cast<size_t>(i)]);
                if (dryQ.available > 0)
                    dryQ.pop(ch48Dry[static_cast<size_t>(i)]);
            } else {
                // Not enough processed samples; keep dry sync for this sample.
                if (dryQ.available > 0) {
                    float dry = 0.0f;
                    dryQ.pop(dry);
                    ch48Out[static_cast<size_t>(i)] = dry;
                    ch48Dry[static_cast<size_t>(i)] = dry;
                } else {
                    ch48Out[static_cast<size_t>(i)] = 0.0f;
                    ch48Dry[static_cast<size_t>(i)] = 0.0f;
                }
            }
        }
    }

    const double upRatio = 48000.0 / rtSampleRate;
    for (int ch = 0; ch < processChannels; ++ch) {
        auto& ch48Out = rt48Out[static_cast<size_t>(ch)];
        auto& ch48Dry = rt48Dry[static_cast<size_t>(ch)];
        auto& outScratch = rtOutScratch[static_cast<size_t>(ch)];
        auto& dryLp = rtDryLp[static_cast<size_t>(ch)];
        rtUpsampler[static_cast<size_t>(ch)].process(upRatio, ch48Out.data(), outScratch.data(), samples);
        rtUpsamplerDry[static_cast<size_t>(ch)].process(upRatio, ch48Dry.data(), dryLp.data(), samples);

        float* d = buffer.getWritePointer(ch);
        for (int i = 0; i < samples; ++i) {
            const float diff = (outScratch[static_cast<size_t>(i)] - dryLp[static_cast<size_t>(i)]) * wet;
            d[i] = d[i] + diff;
        }
    }

    const size_t nativeLatency = static_cast<size_t>(std::round(static_cast<double>(rtModelLatency48) * rtSampleRate / 48000.0));
    for (int ch = processChannels; ch < channels; ++ch) {
        const int ecIdx = ch - processChannels;
        if (ecIdx < rtMaxExtraChannels) {
            auto& ed = rtExtraChannelDelays[static_cast<size_t>(ecIdx)];
            float* d = buffer.getWritePointer(ch);
            for (int i = 0; i < samples; ++i) {
                const float dry = d[i];
                ed.buffer[ed.writePos] = safeValue(dry);
                ed.writePos = (ed.writePos + 1) % ed.buffer.size();
                if (ed.available < ed.buffer.size()) ++ed.available;
                else ed.readPos = (ed.readPos + 1) % ed.buffer.size();

                if (ed.available > nativeLatency) {
                    d[i] = ed.buffer[ed.readPos];
                    ed.readPos = (ed.readPos + 1) % ed.buffer.size();
                    --ed.available;
                } else {
                    d[i] = 0.0f;
                }
            }
        }
    }

    float dryAccum = 1.0e-12f;
    float outAccum = 1.0e-12f;
    for (int ch = 0; ch < processChannels; ++ch) {
        const auto& inScratch = rtInScratch[static_cast<size_t>(ch)];
        const auto& outScratch = rtOutScratch[static_cast<size_t>(ch)];
        dryAccum += std::inner_product(inScratch.begin(), inScratch.begin() + samples, inScratch.begin(), 0.0f);
        outAccum += std::inner_product(outScratch.begin(), outScratch.begin() + samples, outScratch.begin(), 0.0f);
    }
    const float norm = static_cast<float>(std::max(1, processChannels * samples));
    const float dryRms = std::sqrt(dryAccum / norm);
    const float outRms = std::sqrt(outAccum / norm);
    const float prior = juce::jlimit(0.0f, 1.0f, std::abs(dryRms - outRms) / std::max(1.0e-6f, dryRms));
    tailPrior = 0.85f * tailPrior + 0.15f * prior;
    setStatus(StatusCode::okRt);
    return true;
}

} // namespace vxsuite::deepfilternet
