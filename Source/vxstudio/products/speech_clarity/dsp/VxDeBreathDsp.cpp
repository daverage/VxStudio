#include "VxDeBreathDsp.h"
#include <algorithm>
#include <cmath>

namespace vxsuite {
namespace speech_clarity {

void DeBreathDsp::prepare(double sr, int /*maxBlockSize*/, int numChannels) {
    sampleRate = sr > 1000.0 ? sr : 48000.0;
    attCoeff   = std::exp(-1.0f / (static_cast<float>(sampleRate) * 0.010f));  // 10 ms
    relCoeff   = std::exp(-1.0f / (static_cast<float>(sampleRate) * 0.080f));  // 80 ms
    channels.resize(static_cast<size_t>(numChannels));
    reset();
}

void DeBreathDsp::reset() noexcept {
    for (auto& c : channels)
        c.currentGain = 1.0f;
}

void DeBreathDsp::process(juce::AudioBuffer<float>& buffer, const Params& params) {
    if (params.strength < 0.001f || params.detectionIntensity < 0.001f)
        return;

    const int   numCh      = buffer.getNumChannels();
    const int   n          = buffer.getNumSamples();
    // ~8 dB max reduction at full settings (enough to tame breaths without sounding gated)
    const float targetGain = 1.0f - params.strength * params.detectionIntensity * 0.60f;

    for (int ch = 0; ch < std::min(numCh, static_cast<int>(channels.size())); ++ch) {
        auto&  c   = channels[static_cast<size_t>(ch)];
        float* buf = buffer.getWritePointer(ch);

        for (int i = 0; i < n; ++i) {
            // Smooth toward target  -  close faster than it opens to be responsive to breaths
            c.currentGain = targetGain < c.currentGain
                ? attCoeff * c.currentGain + (1.0f - attCoeff) * targetGain
                : relCoeff * c.currentGain + (1.0f - relCoeff) * targetGain;
            buf[i] *= c.currentGain;
        }
    }
}

} // namespace speech_clarity
} // namespace vxsuite
