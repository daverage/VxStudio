#pragma once

#include "../third_party/df.h"
#include "../third_party/df2.h"
#include "../../../../../ThirdParty/resampler/Resampler.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
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
    void setModelVariant(ModelVariant v) noexcept { modelVariant = v; }
    ModelVariant getModelVariant() const noexcept { return modelVariant; }

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

    void releaseRuntime();
    void setStatus(StatusCode code) noexcept { statusCode.store(code, std::memory_order_relaxed); }
    bool prepareModelFile();
    bool prepareChannel(ChannelState& channel, int maxBlockSize);
    int latencySamplesForCurrentVariant() const noexcept;
    RuntimeApi selectedRuntimeApi() const noexcept;
    void* createRuntime(const juce::String& modelPath, float attenuationLimitDb) const;
    void destroyRuntime(void* runtime) const;
    int runtimeFrameLength(void* runtime) const;
    void setRuntimeAttenuation(void* runtime, float attenuationLimitDb) const;
    float processRuntimeFrame(void* runtime, float* input, float* output) const;
    juce::File modelAssetForVariant(ModelVariant variant) const;
    juce::String binaryDataNameForVariant(ModelVariant variant) const;
    bool extractEmbeddedModel(const juce::File& destination);
    float attenuationLimitForStrength(float strength) const noexcept;

    std::array<ChannelState, rtMaxChannels> channels {};
    double rtSampleRate = 48000.0;
    int rtBlockSize = 0;
    int rtFrameLength = 480;
    int latencySamples = 0;
    float tailPrior = 0.0f;
    bool rtReady = false;
    juce::File extractedModelFile;
    juce::String rtBackendTag { "none" };
    RealtimeCapability rtCapability = RealtimeCapability::unavailable;
    RealtimeBackend rtBackend = RealtimeBackend::none;
    RuntimeApi rtRuntimeApi = RuntimeApi::none;
    std::atomic<StatusCode> statusCode { StatusCode::idle };
    ModelVariant modelVariant = ModelVariant::dfn3;
    ModelVariant rtPreparedVariant = ModelVariant::dfn3;
};

} // namespace vxsuite::deepfilternet
