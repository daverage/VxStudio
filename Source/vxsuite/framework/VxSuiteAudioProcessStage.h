#pragma once

#include "VxSuiteProcessOptions.h"

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
};

} // namespace vxsuite
