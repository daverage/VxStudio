#pragma once

#include "AudioProcessStage.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include "DenoiseOptions.h"
#include <type_traits>

namespace vxcleaner::dsp {

/**
 * Standard interface for all Denoise and Deverb engines.
 * This allows engines to be used as drop-in replacements.
 */
class DenoiseEngine {
public:
    virtual ~DenoiseEngine() = default;

    virtual void prepare(double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;
    
    /**
     * @return Internal latency of the engine in samples.
     */
    virtual int getLatencySamples() const = 0;

    /**
     * Process audio in-place.
     * @return true if processing was actually applied.
     */
    virtual bool processInPlace(juce::AudioBuffer<float>& buffer, 
                                float amount, 
                                const DenoiseOptions& options) = 0;
};

static_assert(std::is_same_v<DenoiseOptions, ProcessOptions>);

} // namespace vxcleaner::dsp
