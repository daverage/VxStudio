#pragma once

#include "../third_party/df.h"
#include "../third_party/df2.h"
#include "../../../../../ThirdParty/resampler/Resampler.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

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
        cpu
    };

    enum class RealtimeCapability {
        unavailable = 0,
        embeddedRuntime
    };

    DeepFilterService() = default;
    ~DeepFilterService();

    void prepareRealtime(double sampleRate, int maxBlockSize);
    void resetRealtime();
    bool needsRealtimePrepare(double sampleRate, int maxBlockSize) const;
    bool processRealtime(juce::AudioBuffer<float>& buffer,
                         double sampleRate,
                         float strength,
                         uint64_t key);

    int getLatencySamples() const noexcept { return latencySamples; }
    float lastTailPrior() const noexcept { return tailPrior; }
    juce::String lastStatus() const;
    bool hasRealtimeBackend() const noexcept { return rtBackend != RealtimeBackend::none; }
    bool isRealtimeReady() const noexcept { return rtReady; }
    bool supportsRealtimeForSelectedVariant() const noexcept {
        return rtCapability == RealtimeCapability::embeddedRuntime && rtReady;
    }
    RealtimeCapability realtimeCapability() const noexcept { return rtCapability; }
    RealtimeBackend realtimeBackend() const noexcept { return rtBackend; }
    const juce::String& realtimeBackendTag() const noexcept { return rtBackendTag; }
    void setModelVariant(ModelVariant v) noexcept {
        requestedVariant.store(static_cast<int>(v), std::memory_order_relaxed);
    }
    ModelVariant getModelVariant() const noexcept {
        return static_cast<ModelVariant>(requestedVariant.load(std::memory_order_relaxed));
    }

private:
    enum class StatusCode {
        idle = 0,
        rtPreparing,
        rtReady,
        rtMissingModel,
        rtInitFailed,
        rtProcessFailed,
        rtReprepareNeeded
    };

    enum class RuntimeApi {
        none = 0,
        dfn3,
        dfn2
    };

    struct SampleFifo {
        std::vector<float> buffer;
        int writePos = 0;
        int readPos = 0;
        int available = 0;

        void reset(int capacity);
        void clear();
        void push(const float* data, int count);
        void pop(float* dest, int count);
    };

    struct ChannelState {
        void* runtime = nullptr;
        std::unique_ptr<Resampler<1, 1>> resampler;
        SampleFifo inputFifo;
        SampleFifo outputFifo;
        std::vector<float> frameIn;
        std::vector<float> frameOut;
        std::vector<float> resampleIn;
        std::vector<float> resampleOut;
    };

    struct RuntimeBundle {
        std::array<ChannelState, rtMaxChannels> channels {};
        double sampleRate = 48000.0;
        int blockSize = 0;
        int frameLength = 480;
        int latencySamples = 0;
        bool ready = false;
        juce::File modelFile;
        juce::String backendTag { "none" };
        RealtimeCapability capability = RealtimeCapability::unavailable;
        RealtimeBackend backend = RealtimeBackend::none;
        RuntimeApi runtimeApi = RuntimeApi::none;
        ModelVariant preparedVariant = ModelVariant::dfn3;
    };

    void releaseBundle(RuntimeBundle& bundle);
    bool prepareBundle(RuntimeBundle& bundle, double sampleRate, int maxBlockSize, ModelVariant variant);
    void waitForReaders(int bundleIndex) const noexcept;
    void setStatus(StatusCode code) noexcept { statusCode.store(code, std::memory_order_relaxed); }
    bool prepareModelFile(ModelVariant variant, juce::File& modelFileOut);
    bool prepareChannel(ChannelState& channel, const RuntimeBundle& bundle);
    int latencySamplesForVariant(ModelVariant variant, double sampleRate) const noexcept;
    RuntimeApi selectedRuntimeApi(ModelVariant variant) const noexcept;
    void* createRuntime(RuntimeApi api, const juce::String& modelPath, float attenuationLimitDb) const;
    void destroyRuntime(RuntimeApi api, void* runtime) const;
    int runtimeFrameLength(RuntimeApi api, void* runtime) const;
    void setRuntimeAttenuation(RuntimeApi api, void* runtime, float attenuationLimitDb) const;
    float processRuntimeFrame(RuntimeApi api, void* runtime, float* input, float* output) const;
    juce::File modelAssetForVariant(ModelVariant variant) const;
    juce::String binaryDataNameForVariant(ModelVariant variant) const;
    bool extractEmbeddedModel(ModelVariant variant, const juce::File& destination);
    float attenuationLimitForStrength(float strength) const noexcept;

    std::array<RuntimeBundle, 2> bundles {};
    std::array<std::atomic<int>, 2> bundleReaders { 0, 0 };
    std::atomic<int> activeBundleIndex { -1 };
    std::atomic<int> requestedVariant { static_cast<int>(ModelVariant::dfn3) };
    int latencySamples = 0;
    float tailPrior = 0.0f;
    bool rtReady = false;
    juce::String rtBackendTag { "none" };
    RealtimeCapability rtCapability = RealtimeCapability::unavailable;
    RealtimeBackend rtBackend = RealtimeBackend::none;
    std::atomic<bool> resetRequested { false };
    std::atomic<StatusCode> statusCode { StatusCode::idle };
    ModelVariant rtPreparedVariant = ModelVariant::dfn3;
};

} // namespace vxsuite::deepfilternet
