#pragma once

#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite {

/**
 * Computes block RMS across all channels (linear, not dB).
 * Stateless — call freely from processBlock without side effects.
 */
inline float blockRmsLinear(const juce::AudioBuffer<float>& buffer) noexcept {
    const int channels = buffer.getNumChannels();
    const int samples  = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return 0.0f;

    double sumSq = 0.0;
    for (int ch = 0; ch < channels; ++ch) {
        const float* data = buffer.getReadPointer(ch);
        for (int i = 0; i < samples; ++i)
            sumSq += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    }
    const int total = channels * samples;
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(total)));
}

/**
 * Stateful silence detector with hysteresis.
 *
 * Prevents heavy DSP (FFT pipelines, neural inference) from running on silent
 * input. A hold counter avoids false re-activations on short gaps.
 *
 * Usage in processProduct():
 *
 *   if (silenceGuard.update(buffer))
 *       return;   // skip DSP — input is silent
 *   dsp.processInPlace(buffer, ...);
 *
 * The DSP's internal state (FIFOs, OLA accumulators) is preserved across the
 * skip so processing resumes correctly when signal returns.
 */
class SilenceGuard {
public:
    /**
     * @param thresholdLinear  RMS below this is treated as silence.
     *                         Default 0.002 ≈ −54 dBFS.
     * @param holdBlocks       Consecutive silent blocks before declaring idle.
     *                         Default 8 prevents flapping on natural fades.
     */
    void prepare(float thresholdLinear = 0.002f, int holdBlocks = 8) noexcept {
        threshold        = thresholdLinear;
        holdTarget       = std::max(1, holdBlocks);
        silentBlockCount = 0;
    }

    void reset() noexcept {
        silentBlockCount = 0;
    }

    /**
     * Call once per processBlock before invoking DSP.
     * @return true if the block should be skipped (silence confirmed).
     */
    bool update(const juce::AudioBuffer<float>& buffer) noexcept {
        const float rms = blockRmsLinear(buffer);
        if (rms < threshold) {
            if (silentBlockCount < holdTarget)
                ++silentBlockCount;
        } else {
            silentBlockCount = 0;
        }
        return silentBlockCount >= holdTarget;
    }

    /** True once holdBlocks consecutive silent blocks have been observed. */
    bool isSilent() const noexcept {
        return silentBlockCount >= holdTarget;
    }

private:
    float threshold        = 0.002f;
    int   holdTarget       = 8;
    int   silentBlockCount = 0;
};

} // namespace vxsuite
