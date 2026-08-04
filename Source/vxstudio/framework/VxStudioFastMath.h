#pragma once

#include <algorithm>
#include <cstdint>

namespace vxsuite {

// Fast transcendental approximations for per-sample envelope/gain math.
// Accuracy: fastLog2 ≈ ±0.00015, fastExp2 ≈ ±2e-4 relative — well under
// 0.005 dB when used for dB↔gain conversion, inaudible on smooth gain
// envelopes. Do not use where bit-exact analysis values are required.
// (Derived from the well-known Mineiro fastapprox formulations.)

inline float fastLog2(const float x) noexcept {
    union { float f; std::uint32_t i; } vx { x };
    union { std::uint32_t i; float f; } mx { (vx.i & 0x007FFFFFu) | 0x3F000000u };
    const float y = static_cast<float>(vx.i) * 1.1920928955078125e-7f;
    return y - 124.22551499f - 1.498030302f * mx.f - 1.72587999f / (0.3520887068f + mx.f);
}

inline float fastExp2(const float p) noexcept {
    const float clipp = p < -126.0f ? -126.0f : p;
    const float z = clipp - static_cast<float>(static_cast<int>(clipp)) + (clipp < 0.0f ? 1.0f : 0.0f);
    union { std::uint32_t i; float f; } v {
        static_cast<std::uint32_t>(static_cast<float>(1 << 23)
            * (clipp + 121.2740575f + 27.7280233f / (4.84252568f - z) - 1.49012907f * z))
    };
    return v.f;
}

// gain (linear, > 0) → dB, floored like juce::Decibels::gainToDecibels.
inline float fastGainToDb(const float gain, const float minusInfinityDb = -120.0f) noexcept {
    if (gain <= 0.0f)
        return minusInfinityDb;
    return std::max(minusInfinityDb, 6.0205999132796239f * fastLog2(gain));
}

// dB → linear gain.
inline float fastDbToGain(const float db) noexcept {
    return fastExp2(db * 0.16609640474436813f);
}

} // namespace vxsuite
