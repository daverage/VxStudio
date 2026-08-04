#include "VxTunePitchShifter.h"

#include <algorithm>
#include <cmath>

namespace vxsuite::tune {

namespace {
constexpr float kPi = 3.14159265358979323846f;

float frac01(float x) noexcept {
    x -= std::floor(x);
    return x < 0.0f ? x + 1.0f : x;
}
} // namespace

void PitchShifter::prepare(const double sampleRate, const int maxBlockSamples) {
    const double sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    // ~21 ms crossfade window (1024 @ 48 kHz), kept even so the parked
    // single-tap delay W/2 is an integer and passthrough is bit-exact.
    windowSamples = std::max(128, static_cast<int>(sr * 1024.0 / 48000.0)) & ~1;
    ringSize = windowSamples + std::max(64, maxBlockSamples) + 4;
    ring.assign(static_cast<size_t>(ringSize), 0.0f);
    smoothAlpha = 1.0f - static_cast<float>(std::exp(-1.0 / (0.015 * sr)));
    reset();
}

void PitchShifter::reset() {
    std::fill(ring.begin(), ring.end(), 0.0f);
    writeIndex = 0;
    phase = 0.5f;               // parked: single tap, pure delay of W/2
    targetCents = 0.0f;
    smoothedCents = 0.0f;
}

float PitchShifter::readTap(const float delaySamples) const noexcept {
    // Delay 0 = the sample just written.
    float pos = static_cast<float>(writeIndex - 1) - delaySamples;
    if (pos < 0.0f)
        pos += static_cast<float>(ringSize);
    const int i0 = static_cast<int>(pos);
    const float t = pos - static_cast<float>(i0);
    const int i1 = i0 + 1 == ringSize ? 0 : i0 + 1;
    return ring[static_cast<size_t>(i0)] * (1.0f - t)
         + ring[static_cast<size_t>(i1)] * t;
}

void PitchShifter::process(float* samples, const int numSamples) noexcept {
    const float w = static_cast<float>(windowSamples);
    float ratio = std::exp2(smoothedCents / 1200.0f);

    for (int i = 0; i < numSamples; ++i) {
        ring[static_cast<size_t>(writeIndex)] = samples[i];
        writeIndex = writeIndex + 1 == ringSize ? 0 : writeIndex + 1;

        smoothedCents += smoothAlpha * (targetCents - smoothedCents);
        if ((i & 15) == 0)
            ratio = std::exp2(smoothedCents / 1200.0f);

        if (std::abs(smoothedCents) < 0.1f) {
            // No shift requested: finish any crossfade by drifting gently to
            // the nearest single-tap park point (both park points read a pure
            // W/2 delay), then hold there for bit-exact passthrough.
            const float nearestPark = 0.5f * std::round(phase * 2.0f);
            const float toPark = nearestPark - phase;
            const float step = 0.008f / w;
            if (std::abs(toPark) <= step)
                phase = frac01(nearestPark);
            else
                phase += toPark > 0.0f ? step : -step;
        } else {
            phase = frac01(phase + (1.0f - ratio) / w);
        }

        const float f1 = frac01(phase);
        const float f2 = frac01(phase + 0.5f);
        const float g1 = std::sin(kPi * f1);
        const float g2 = std::sin(kPi * f2);
        samples[i] = g1 * readTap(w * f1) + g2 * readTap(w * f2);
    }
}

} // namespace vxsuite::tune
