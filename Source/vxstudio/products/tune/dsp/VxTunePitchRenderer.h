#pragma once

#include "signalsmith-stretch/signalsmith-stretch.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace vxsuite::tune {

class VxTunePitchRenderer {
public:
    enum class Backend {
        Signalsmith,
    };

    enum class QualityProfile {
        Studio,
        Live,
    };

    struct PrepareSpec {
        double sampleRate = 44100.0;
        int maximumBlockSize = 512;
        int numChannels = 1;
        QualityProfile qualityProfile = QualityProfile::Studio;
    };

    struct FrameInfo {
        float correctionCents = 0.0f;
        float detectedF0Hz = 0.0f;
        float pitchConfidence = 0.0f;
        bool voiced = false;
    };

    struct DebugSnapshot {
        float requestedCents = 0.0f;
        float appliedCents = 0.0f;
        float mix = 0.0f;
        float hintPeriod = 0.0f;
        float hintConfidence = 0.0f;
        int voicedActive = 0;
        int parkedNow = 0;
        int parkedRun = 0;
        int epochCount = 0;
        const char* backendName = "";
        const char* qualityProfileName = "";
        int latencySamples = 0;
        int blockSamples = 0;
        int intervalSamples = 0;
        int splitComputation = 0;
    };

    virtual ~VxTunePitchRenderer() = default;

    virtual void prepare(const PrepareSpec& spec) = 0;
    virtual void reset() = 0;
    virtual void setFrameInfo(const FrameInfo& info) = 0;
    virtual void process(const float* const* input, float* const* output,
                         int numChannels, int numSamples,
                         float startCents, float endCents) = 0;
    virtual int latencySamples() const noexcept = 0;
    virtual const char* backendName() const noexcept = 0;
    virtual DebugSnapshot debugSnapshot() const noexcept = 0;
};

class SignalsmithPitchRenderer final : public VxTunePitchRenderer {
public:
    void prepare(const PrepareSpec& spec) override;
    void reset() override;
    void setFrameInfo(const FrameInfo& info) override;
    void process(const float* const* input, float* const* output,
                 int numChannels, int numSamples,
                 float startCents, float endCents) override;
    int latencySamples() const noexcept override;
    const char* backendName() const noexcept override { return "Signalsmith"; }
    DebugSnapshot debugSnapshot() const noexcept override;

private:
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    double sampleRate = 44100.0;
    int channelsPrepared = 0;
    QualityProfile qualityProfile = QualityProfile::Studio;
    float requestedCents = 0.0f;
    float appliedCents = 0.0f;
    float lastF0Hz = 0.0f;
    float lastConfidence = 0.0f;
    bool lastVoiced = false;
    std::vector<float> lastOutputSamples;
    std::vector<float> resetStartSamples;
    int resetEdgeFadeRemaining = 0;
    int resetOnsetFadeRemaining = 0;
    bool resetAwaitingSignal = false;
    std::vector<std::vector<float>> inputScratch;
    std::vector<std::vector<float>> outputScratch;
    std::vector<float*> inputPtrs;
    std::vector<float*> outputPtrs;
};

std::unique_ptr<VxTunePitchRenderer> createPitchRenderer(
    VxTunePitchRenderer::Backend backend);

} // namespace vxsuite::tune
