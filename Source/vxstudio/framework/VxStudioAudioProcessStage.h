#pragma once

#include "VxStudioProcessOptions.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite {

class AudioProcessStage {
public:
    virtual ~AudioProcessStage() = default;

    virtual void prepare(double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;
    virtual int getLatencySamples() const = 0;
    virtual bool processInPlace(juce::AudioBuffer<float>& buffer,
                                float amount,
                                const ProcessOptions& options) = 0;

    /**
     * Drain the pipeline tail into buffer after bypass is activated.
     *
     * STFT-based stages accumulate `latencySamples` of audio in their OLA
     * ring at the moment of bypass. Without draining, this tail never reaches
     * the host and causes a silence gap. Call this each block (replacing the
     * processInPlace call) until the stage returns false (pipeline empty).
     *
     * Default no-op: zero-latency stages need not override.
     *
     * @return true if there is still pipeline content remaining,
     *         false when the ring has been fully drained.
     */
    virtual bool drain(juce::AudioBuffer<float>& /*buffer*/) noexcept { return false; }
};

} // namespace vxsuite
