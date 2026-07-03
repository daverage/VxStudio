#pragma once

#include "IStemSeparationBackend.h"

#if VXSTUDIO_REBALANCE_AI_HAS_STEMGENRT
#include "StemgenRT/OnnxRuntime.h"
#endif

#include <array>

namespace vxsuite::rebalance::ai {

class OnnxStemgenBackend final : public IStemSeparationBackend {
public:
    OnnxStemgenBackend();
    ~OnnxStemgenBackend() override;

    bool prepare(double sampleRate,
                 int outputChunkSize,
                 int contextSize,
                 int numChannels,
                 juce::String& error) override;
    void reset() override;

    bool processFrame(const ChannelBuffers& context,
                      const ChannelBuffers& input,
                      const ChannelBuffers& lowFreq,
                      float normalizationGain,
                      StemBuffers& outputStems,
                      juce::String& error) override;

    [[nodiscard]] juce::String backendName() const override;
    [[nodiscard]] juce::String statusText() const override;
    [[nodiscard]] double lastProcessMs() const noexcept override { return lastProcessMsValue; }
    [[nodiscard]] double averageProcessMs() const noexcept override { return processCount > 0 ? totalProcessMs / static_cast<double>(processCount) : 0.0; }
    [[nodiscard]] double maxProcessMs() const noexcept override { return maxProcessMsValue; }

private:
#if VXSTUDIO_REBALANCE_AI_HAS_STEMGENRT
    audio_plugin::OnnxRuntime runtime;
    StemBuffers overlapTail;
#endif
    double lastProcessMsValue = 0.0;
    double totalProcessMs = 0.0;
    double maxProcessMsValue = 0.0;
    std::uint64_t processCount = 0;
};

} // namespace vxsuite::rebalance::ai
