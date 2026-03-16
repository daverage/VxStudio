#pragma once

#include "ProcessOptions.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxcleaner::dsp {

/**
 * Generic realtime stage interface for suite processors.
 */
class AudioProcessStage {
public:
    virtual ~AudioProcessStage() = default;

    virtual void prepare(double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;
    virtual int getLatencySamples() const = 0;
    virtual bool processInPlace(juce::AudioBuffer<float>& buffer,
                                float amount,
                                const ProcessOptions& options) = 0;
};

} // namespace vxcleaner::dsp
