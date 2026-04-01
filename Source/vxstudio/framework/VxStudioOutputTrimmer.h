#pragma once

#include "VxStudioBlockSmoothing.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace vxsuite {

// Safety output trimmer: instantaneous gain reduction when a block peaks above
// the ceiling, slow exponential release back to unity. Stateful — one instance
// per processor, reset on prepare/reset.
class OutputTrimmer {
public:
    void setCeiling(float linearCeiling) noexcept     { ceiling = linearCeiling; }
    void setReleaseSeconds(float seconds) noexcept    { releaseSeconds = seconds; }

    void reset() noexcept { currentGain = 1.0f; }

    void process(juce::AudioBuffer<float>& buffer, double sampleRate) noexcept {
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples <= 0 || numChannels <= 0)
            return;

        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = std::max(peak, buffer.getMagnitude(ch, 0, numSamples));

        const float targetGain = peak > ceiling
            ? (ceiling / std::max(peak, 1.0e-6f))
            : 1.0f;

        const float previousGain = currentGain;
        if (targetGain < currentGain) {
            currentGain = targetGain; // instantaneous gain reduction
        } else {
            const float alpha = blockBlendAlpha(sampleRate, numSamples, releaseSeconds);
            currentGain += alpha * (targetGain - currentGain);
        }

        if (targetGain < previousGain) {
            buffer.applyGain(currentGain);
        } else if (std::abs(previousGain - 1.0f) > 1.0e-5f || std::abs(currentGain - 1.0f) > 1.0e-5f) {
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.applyGainRamp(ch, 0, numSamples, previousGain, currentGain);
        }
    }

private:
    float currentGain    = 1.0f;
    float ceiling        = 0.97f;
    float releaseSeconds = 0.18f;
};

} // namespace vxsuite
