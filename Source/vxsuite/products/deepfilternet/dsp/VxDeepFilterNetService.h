#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>
#include <array>
#include <string>
#include <thread>

#include <juce_audio_basics/juce_audio_basics.h>
#if defined(VXC_HAS_ONNXRUNTIME) && VXC_HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace vxsuite::deepfilternet {

class DeepFilterService {
public:
    static constexpr int rtMaxChannels = 2;
    enum class ModelVariant {
        dfn3 = 0,
        dfn2
    };
    enum class RealtimeBackend {
        none = 0,
        gpu,
        cpu
    };
    enum class RealtimeCapability {
        unavailable = 0,
        monolithicSession,
        officialBundleOnly
    };

    ~DeepFilterService();
    void prepareRealtime(double sampleRate, int maxBlockSize);
    void resetRealtime();
    bool needsRealtimePrepare(double sampleRate, int maxBlockSize) const;
    bool processRealtime(juce::AudioBuffer<float>& buffer,
                         double sampleRate,
                         float strength,
                         uint64_t key);
    int getLatencySamples() const { return juce::roundToInt(static_cast<double>(rtModelLatency48) * rtSampleRate / 48000.0); }
    float lastTailPrior() const { return tailPrior; }
    juce::String lastStatus() const;
    bool hasRealtimeBackend() const { return rtBackend != RealtimeBackend::none; }
    bool isRealtimeReady() const { return rtModelLoaded; }
    bool supportsRealtimeForSelectedVariant() const { return rtCapability == RealtimeCapability::monolithicSession; }
    RealtimeCapability realtimeCapability() const { return rtCapability; }
    RealtimeBackend realtimeBackend() const { return rtBackend; }
    const juce::String& realtimeBackendTag() const { return rtBackendTag; }
    void setModelVariant(ModelVariant v) { modelVariant = v; }
    ModelVariant getModelVariant() const { return modelVariant; }

private:
    enum class StatusCode {
        idle = 0,
        rtBackendDisabled,
        rtMissingModelDfn2,
        rtMissingModelDfn3,
        rtBundleOnlyDfn2,
        rtBundleOnlyDfn3,
        rtReady,
        rtInitFailed,
        rtInferFailed,
        rtResultOverflow,
        rtOutOverflow,
        rtInOverflow,
        rtDryOverflow,
        rtJobOverflow,
        okRt,
        rtReprepareNeeded
    };
    void setStatus(StatusCode code) { statusCode.store(code, std::memory_order_relaxed); }
    bool runRealtimeModelFrame(const float* input480, float* output480, float attenLimDb, std::vector<float>& state);
    void selectRealtimeBackend();
    void stopRealtimeWorker();
    void startRealtimeWorker();
    bool enqueueRealtimeJob(int channel, const float* inputFrame, float attenLimDb);
    void drainRealtimeResults();
    bool hasQueuedRealtimeJobs() const;
    struct RtFifo {
        std::vector<float> data;
        size_t readPos = 0;
        size_t writePos = 0;
        size_t available = 0;
        void reset(size_t cap);
        void clear();
        bool push(float v);
        bool pop(float& out);
    };
    bool rtModelLoaded = false;
    RealtimeCapability rtCapability = RealtimeCapability::unavailable;
    RealtimeBackend rtBackend = RealtimeBackend::none;
    juce::String rtBackendTag { "none" };
    double rtSampleRate = 48000.0;
    std::array<std::vector<float>, rtMaxChannels> rtInScratch;
    std::array<std::vector<float>, rtMaxChannels> rtOutScratch;
    std::array<std::vector<float>, rtMaxChannels> rt48In;
    std::array<std::vector<float>, rtMaxChannels> rt48Out;
    std::array<std::vector<float>, rtMaxChannels> rt48Dry;
    std::array<std::vector<float>, rtMaxChannels> rtDryLp;
    std::array<RtFifo, rtMaxChannels> rtInQueue48;
    std::array<RtFifo, rtMaxChannels> rtOutQueue48;
    std::array<RtFifo, rtMaxChannels> rtDryQueue48;
    static constexpr int rtMaxExtraChannels = 6;
    struct ExtraChannelDelay {
        std::vector<float> buffer;
        size_t readPos = 0;
        size_t writePos = 0;
        size_t available = 0;
    };
    std::array<ExtraChannelDelay, rtMaxExtraChannels> rtExtraChannelDelays;
    std::array<std::vector<float>, rtMaxChannels> rtFrameIn;
    std::array<std::vector<float>, rtMaxChannels> rtFrameOut;
    std::array<juce::WindowedSincInterpolator, rtMaxChannels> rtDownsampler;
    std::array<juce::WindowedSincInterpolator, rtMaxChannels> rtUpsampler;
    std::array<juce::WindowedSincInterpolator, rtMaxChannels> rtUpsamplerDry;
    struct RtFrameSlot {
        int channel = -1;
        float attenLimDb = 0.0f;
        std::vector<float> frame;
    };
    size_t rtFrameSamples = 480;
    size_t rtModelLatency48 = 960;
    size_t rtQueueCapacity = 32;
    std::vector<RtFrameSlot> rtJobQueue;
    std::vector<RtFrameSlot> rtResultQueue;
    std::atomic<size_t> rtJobRead { 0 };
    std::atomic<size_t> rtJobWrite { 0 };
    std::atomic<size_t> rtResultRead { 0 };
    std::atomic<size_t> rtResultWrite { 0 };
    std::thread rtWorker;
    mutable std::mutex rtWorkerMutex;
    std::condition_variable rtWorkerCv;
    std::atomic<bool> rtWorkerExit { false };
    std::atomic<bool> rtWorkerReady { false };
#if defined(VXC_HAS_ONNXRUNTIME) && VXC_HAS_ONNXRUNTIME
    std::unique_ptr<Ort::Env> rtEnv;
    Ort::SessionOptions rtOpts;
    std::unique_ptr<Ort::Session> rtSession;
    std::string rtInputFrameName;
    std::string rtInputStateName;
    std::string rtInputAttenName;
    std::string rtOutputFrameName;
    std::string rtOutputStateName;
    std::vector<int64_t> rtFrameShape;
    std::vector<int64_t> rtStateShape;
    std::vector<int64_t> rtAttenShape;
#endif
    std::array<std::vector<float>, rtMaxChannels> rtStates;
    float tailPrior = 0.0f;
    std::atomic<StatusCode> statusCode { StatusCode::idle };
    ModelVariant modelVariant = ModelVariant::dfn3;
    ModelVariant rtPreparedVariant = ModelVariant::dfn3;
};

} // namespace vxsuite::deepfilternet
