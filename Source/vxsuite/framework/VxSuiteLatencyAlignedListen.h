#pragma once

#include <algorithm>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite {

class LatencyAlignedListenBuffer {
public:
    void prepare(const int channels, const int samples, const int latencySamples) {
        const int safeChannels = std::max(1, channels);
        const int safeSamples = std::max(1, samples);
        dryScratch.setSize(safeChannels, safeSamples, false, false, true);
        alignedDryScratch.setSize(safeChannels, safeSamples, false, false, true);

        const int delayCapacity = std::max(1, std::max(0, latencySamples) + safeSamples + 1);
        dryDelayLines.assign(static_cast<size_t>(safeChannels),
                             std::vector<float>(static_cast<size_t>(delayCapacity), 0.0f));
        dryDelayWritePos.assign(static_cast<size_t>(safeChannels), 0);
    }

    void reset() {
        dryScratch.clear();
        alignedDryScratch.clear();
        for (auto& line : dryDelayLines)
            std::fill(line.begin(), line.end(), 0.0f);
        std::fill(dryDelayWritePos.begin(), dryDelayWritePos.end(), 0);
    }

    [[nodiscard]] bool canStore(const int channels, const int samples) const noexcept {
        return dryScratch.getNumChannels() >= channels
            && dryScratch.getNumSamples() >= samples
            && alignedDryScratch.getNumChannels() >= channels
            && alignedDryScratch.getNumSamples() >= samples;
    }

    void captureDry(const juce::AudioBuffer<float>& source, const int numSamples) {
        const int channels = std::min(source.getNumChannels(), dryScratch.getNumChannels());
        const int samples = std::min(numSamples, dryScratch.getNumSamples());
        for (int ch = 0; ch < channels; ++ch)
            dryScratch.copyFrom(ch, 0, source, ch, 0, samples);
    }

    void buildAlignedDry(const int numSamples, const int latencySamples) {
        const int channels = std::min(dryScratch.getNumChannels(), alignedDryScratch.getNumChannels());
        const int latency = std::max(0, latencySamples);
        for (int ch = 0; ch < channels; ++ch) {
            const auto* dry = dryScratch.getReadPointer(ch);
            auto* delayed = alignedDryScratch.getWritePointer(ch);
            auto& line = dryDelayLines[static_cast<size_t>(ch)];
            const int size = static_cast<int>(line.size());
            int writePos = dryDelayWritePos[static_cast<size_t>(ch)];
            for (int i = 0; i < numSamples; ++i) {
                line[static_cast<size_t>(writePos)] = dry[i];
                const int readPos = (writePos + size - latency) % size;
                delayed[i] = line[static_cast<size_t>(readPos)];
                writePos = (writePos + 1) % size;
            }
            dryDelayWritePos[static_cast<size_t>(ch)] = writePos;
        }
    }

    void renderRemovedDelta(juce::AudioBuffer<float>& outputBuffer) const {
        const int channels = std::min(outputBuffer.getNumChannels(), alignedDryScratch.getNumChannels());
        const int samples = std::min(outputBuffer.getNumSamples(), alignedDryScratch.getNumSamples());
        for (int ch = 0; ch < channels; ++ch) {
            auto* out = outputBuffer.getWritePointer(ch);
            const auto* dry = alignedDryScratch.getReadPointer(ch);
            for (int i = 0; i < samples; ++i)
                out[i] = dry[i] - out[i];
        }
    }

    juce::AudioBuffer<float>& dryBuffer() noexcept { return dryScratch; }
    const juce::AudioBuffer<float>& dryBuffer() const noexcept { return dryScratch; }
    const juce::AudioBuffer<float>& alignedDryBuffer() const noexcept { return alignedDryScratch; }

private:
    juce::AudioBuffer<float> dryScratch;
    juce::AudioBuffer<float> alignedDryScratch;
    std::vector<std::vector<float>> dryDelayLines;
    std::vector<int> dryDelayWritePos;
};

} // namespace vxsuite
