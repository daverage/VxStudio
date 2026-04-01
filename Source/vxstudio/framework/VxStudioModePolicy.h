#pragma once

#include "VxStudioProduct.h"

#include <string_view>

namespace vxsuite {

struct ModePolicy {
    Mode mode = Mode::vocal;
    std::string_view label = "Vocal";
    std::string_view statusText = "Voice mode protects formants and center image";
    float sourceProtect = 0.85f;
    float lateTailAggression = 0.55f;
    float stereoWidthProtect = 0.85f;
    float bodyRecovery = 0.65f;
    float guardStrictness = 0.80f;
    float speechFocus = 0.85f;
};

inline constexpr ModePolicy kVocalModePolicy {
    Mode::vocal,
    "Vocal",
    "Voice mode protects formants and center image",
    0.96f,
    0.48f,
    0.96f,
    0.86f,
    0.92f,
    1.00f
};

inline constexpr ModePolicy kGeneralModePolicy {
    Mode::general,
    "General",
    "General mode allows deeper tail reduction",
    0.58f,
    0.90f,
    0.60f,
    0.58f,
    0.64f,
    0.58f
};

inline constexpr const ModePolicy& policyForMode(const Mode mode) noexcept {
    return mode == Mode::general ? kGeneralModePolicy : kVocalModePolicy;
}

} // namespace vxsuite
