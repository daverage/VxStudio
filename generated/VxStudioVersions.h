#pragma once

#include <string_view>

namespace vxsuite::versions {

inline constexpr std::string_view framework = "0.2.2";

namespace plugins {
inline constexpr std::string_view deverb = "0.2.1";
inline constexpr std::string_view proximity = "0.3.0";
inline constexpr std::string_view ProximityClassic = "0.1.0";
inline constexpr std::string_view speech_clarity = "0.2.0";
inline constexpr std::string_view finish = "0.3.0";
inline constexpr std::string_view optocomp = "0.3.0";
inline constexpr std::string_view subtract = "0.2.1";
inline constexpr std::string_view denoiser = "0.2.1";
inline constexpr std::string_view deepfilternet = "0.2.2";
inline constexpr std::string_view tone = "0.2.0";
inline constexpr std::string_view tone_refine = "0.1.0";
inline constexpr std::string_view leveler = "0.2.0";
inline constexpr std::string_view rebalance = "0.2.2";
inline constexpr std::string_view analyser = "0.2.1";
inline constexpr std::string_view repair   = "0.1.1";
inline constexpr std::string_view tune     = "0.1.1";
inline constexpr std::string_view width    = "1.0.0";
} // namespace plugins

} // namespace vxsuite::versions
