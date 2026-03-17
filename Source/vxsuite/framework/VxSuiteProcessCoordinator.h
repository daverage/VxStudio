#pragma once

#include "VxSuiteAudioProcessStage.h"
#include "VxSuiteLatencyAlignedListen.h"

namespace vxsuite {

class ProcessCoordinator {
public:
    void prepare(const int channels, const int maxBlockSize, const int latencySamples) {
        preparedChannels = std::max(1, channels);
        preparedMaxBlockSize = std::max(1, maxBlockSize);
        currentLatencySamples = std::max(0, latencySamples);
        listenBuffer.prepare(preparedChannels, preparedMaxBlockSize, currentLatencySamples);
        alignedDryReady = false;
    }

    void reset() {
        listenBuffer.reset();
        alignedDryReady = false;
    }

    void release() {
        listenBuffer.prepare(1, 1, 0);
        reset();
    }

    void setLatencySamples(const int latencySamples) {
        const int clampedLatency = std::max(0, latencySamples);
        if (clampedLatency == currentLatencySamples)
            return;
        currentLatencySamples = clampedLatency;
        listenBuffer.prepare(preparedChannels, preparedMaxBlockSize, currentLatencySamples);
        alignedDryReady = false;
    }

    template <typename... Stages>
    void setLatencyFromStages(const Stages&... stages) {
        setLatencySamples((0 + ... + stages.getLatencySamples()));
    }

    [[nodiscard]] int latencySamples() const noexcept { return currentLatencySamples; }

    [[nodiscard]] bool canRenderListen(const juce::AudioBuffer<float>& buffer,
                                       const bool listenEnabled) const noexcept {
        return listenEnabled && listenBuffer.canStore(buffer.getNumChannels(), buffer.getNumSamples());
    }

    void beginBlock(const juce::AudioBuffer<float>& dryInput,
                    const bool listenEnabled) {
        alignedDryReady = false;
        if (!canRenderListen(dryInput, listenEnabled))
            return;
        listenBuffer.captureDry(dryInput, dryInput.getNumSamples());
    }

    void ensureAlignedDry(const int numSamples) {
        if (!listenBuffer.canStore(listenBuffer.dryBuffer().getNumChannels(), numSamples) || alignedDryReady)
            return;
        listenBuffer.buildAlignedDry(numSamples, currentLatencySamples);
        alignedDryReady = true;
    }

    void renderRemovedDelta(juce::AudioBuffer<float>& wetBuffer) {
        ensureAlignedDry(wetBuffer.getNumSamples());
        listenBuffer.renderRemovedDelta(wetBuffer);
    }

    [[nodiscard]] const juce::AudioBuffer<float>& alignedDryBuffer() const noexcept {
        return listenBuffer.alignedDryBuffer();
    }

private:
    LatencyAlignedListenBuffer listenBuffer;
    int preparedChannels = 1;
    int preparedMaxBlockSize = 1;
    int currentLatencySamples = 0;
    bool alignedDryReady = false;
};

} // namespace vxsuite
