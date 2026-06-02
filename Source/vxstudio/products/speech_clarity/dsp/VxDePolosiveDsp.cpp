#include "VxDePolosiveDsp.h"
#include <algorithm>
#include <cmath>

namespace vxsuite {
namespace speech_clarity {

void DePolosiveDsp::prepare(double sr, int /*maxBlockSize*/, int numChannels) {
    sampleRate = sr > 1000.0 ? sr : 48000.0;

    // 2nd-order Butterworth LP at 180 Hz (plosive burst range)
    const float K    = std::tan(3.14159265f * 180.0f / static_cast<float>(sampleRate));
    const float K2   = K * K;
    const float norm = 1.0f / (1.0f + 1.41421356f * K + K2);
    lpB0 =  K2 * norm;
    lpB1 =  2.0f * K2 * norm;
    lpB2 =  K2 * norm;
    lpA1 =  2.0f * (K2 - 1.0f) * norm;
    lpA2 = (1.0f - 1.41421356f * K + K2) * norm;

    attCoeff = std::exp(-1.0f / (static_cast<float>(sampleRate) * 0.002f));  // 2 ms
    relCoeff = std::exp(-1.0f / (static_cast<float>(sampleRate) * 0.040f));  // 40 ms

    channels.resize(static_cast<size_t>(numChannels));
    reset();
}

void DePolosiveDsp::reset() noexcept {
    for (auto& c : channels) {
        c.lpZ1 = c.lpZ2 = c.env = 0.0f;
        c.gateGain = 1.0f;
    }
}

void DePolosiveDsp::process(juce::AudioBuffer<float>& buffer, const Params& params) {
    if (params.strength < 0.001f || params.detectionIntensity < 0.001f)
        return;

    const int   numCh      = buffer.getNumChannels();
    const int   n          = buffer.getNumSamples();
    // Target gain for LP band: up to -12 dB at full settings
    const float targetGain = std::pow(10.0f, -12.0f * params.strength * params.detectionIntensity / 20.0f);

    for (int ch = 0; ch < std::min(numCh, static_cast<int>(channels.size())); ++ch) {
        auto&  c   = channels[static_cast<size_t>(ch)];
        float* buf = buffer.getWritePointer(ch);

        for (int i = 0; i < n; ++i) {
            // LP filter (DF2 transposed)
            const float lp = lpB0 * buf[i] + c.lpZ1;
            c.lpZ1 = lpB1 * buf[i] - lpA1 * lp + c.lpZ2;
            c.lpZ2 = lpB2 * buf[i] - lpA2 * lp;

            // Envelope of LP content
            const float rect = std::abs(lp);
            c.env = rect > c.env ? attCoeff * c.env + (1.0f - attCoeff) * rect
                                 : relCoeff * c.env + (1.0f - relCoeff) * rect;

            // Gate gain: close fast toward target, open slowly to avoid pumping
            c.gateGain = targetGain < c.gateGain
                ? attCoeff * c.gateGain + (1.0f - attCoeff) * targetGain
                : relCoeff * c.gateGain + (1.0f - relCoeff) * targetGain;

            // Subtract the gated LP component only — HP component is untouched
            buf[i] -= lp * (1.0f - c.gateGain);
        }
    }
}

} // namespace speech_clarity
} // namespace vxsuite
