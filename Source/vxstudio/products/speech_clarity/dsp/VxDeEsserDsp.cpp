#include "VxDeEsserDsp.h"
#include <algorithm>
#include <cmath>

namespace vxsuite {
namespace speech_clarity {

void DeEsserDsp::prepare(double sr, int /*maxBlockSize*/, int numChannels) {
    sampleRate   = sr > 1000.0 ? sr : 48000.0;
    attCoeff     = std::exp(-1.0f / (static_cast<float>(sampleRate) * 0.001f));  // 1 ms
    relCoeff     = std::exp(-1.0f / (static_cast<float>(sampleRate) * 0.100f));  // 100 ms
    thresholdLin = std::pow(10.0f, -40.0f / 20.0f);

    channels.resize(static_cast<size_t>(numChannels));
    for (auto& c : channels)
        designBandPass(sampleRate, 5500.0f, 1.5f, c.bp);
    reset();
}

void DeEsserDsp::reset() noexcept {
    for (auto& c : channels) {
        c.bp.x1 = c.bp.x2 = c.bp.y1 = c.bp.y2 = 0.0f;
        c.env = 0.0f;
    }
}

void DeEsserDsp::designBandPass(double sr, float hz, float q, BiquadState& s) noexcept {
    const float w     = 2.0f * juce::MathConstants<float>::pi * hz / static_cast<float>(sr);
    const float sw    = std::sin(w);
    const float cw    = std::cos(w);
    const float alpha = sw / (2.0f * q);
    const float a0    = 1.0f + alpha;
    s.b0 =  alpha / a0;
    s.b1 =  0.0f;
    s.b2 = -alpha / a0;
    s.a1 = (-2.0f * cw) / a0;
    s.a2 = (1.0f - alpha) / a0;
}

float DeEsserDsp::processBiquad(float x, BiquadState& s) noexcept {
    const float y = s.b0*x + s.b1*s.x1 + s.b2*s.x2 - s.a1*s.y1 - s.a2*s.y2;
    s.x2 = s.x1; s.x1 = x;
    s.y2 = s.y1; s.y1 = y;
    return y;
}

void DeEsserDsp::process(juce::AudioBuffer<float>& buffer, const Params& params) {
    if (params.strength < 0.001f || params.detectionIntensity < 0.001f)
        return;

    const int   numCh    = buffer.getNumChannels();
    const int   n        = buffer.getNumSamples();
    const float ratio    = 2.0f + params.strength * 6.0f;         // 2:1 – 8:1
    const float exponent = 1.0f - 1.0f / ratio;
    const float maxGain  = std::pow(10.0f, -12.0f * params.strength / 20.0f);
    const float detScale = params.detectionIntensity;

    for (int ch = 0; ch < std::min(numCh, static_cast<int>(channels.size())); ++ch) {
        auto&  c   = channels[static_cast<size_t>(ch)];
        float* buf = buffer.getWritePointer(ch);

        for (int i = 0; i < n; ++i) {
            const float sib  = processBiquad(buf[i], c.bp);
            const float rect = std::abs(sib);

            // Asymmetric envelope: fast attack catches sibilant onset, slow release avoids pumping
            c.env = rect > c.env ? attCoeff * c.env + (1.0f - attCoeff) * rect
                                 : relCoeff * c.env + (1.0f - relCoeff) * rect;

            float gain = 1.0f;
            if (c.env > thresholdLin)
                gain = std::max(maxGain, std::pow(thresholdLin / std::max(c.env, 1.0e-9f), exponent));

            // Subtract the gain-reduced portion of the sibilance band only
            buf[i] -= sib * (detScale * (1.0f - gain));
        }
    }
}

} // namespace speech_clarity
} // namespace vxsuite
