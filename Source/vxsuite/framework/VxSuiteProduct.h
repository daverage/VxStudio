#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace vxsuite {

enum class Mode : int {
    vocal = 0,
    general = 1
};

enum class StageType : std::uint8_t {
    unknown = 0,
    spectral,
    dynamic,
    spatial,
    mixed
};

struct ProductTheme {
    std::array<float, 3> accentRgb { 0.00f, 0.82f, 1.00f };
    std::array<float, 3> accent2Rgb { 0.10f, 0.12f, 0.16f };
    std::array<float, 3> backgroundRgb { 0.05f, 0.05f, 0.07f };
    std::array<float, 3> panelRgb { 0.09f, 0.09f, 0.12f };
    std::array<float, 3> textRgb { 0.86f, 0.91f, 1.00f };
};

struct ProductIdentity {
    std::string_view suiteName = "VX Suite";
    std::string_view productName;
    std::string_view shortTag;
    std::string_view stageId;
    std::string_view primaryParamId;
    std::string_view secondaryParamId;
    std::string_view tertiaryParamId;
    std::string_view quaternaryParamId;
    std::string_view modeParamId;
    std::string_view listenParamId;
    std::string_view primaryLabel;
    std::string_view secondaryLabel;
    std::string_view tertiaryLabel;
    std::string_view quaternaryLabel;
    std::string_view primaryHint;
    std::string_view secondaryHint;
    std::string_view tertiaryHint;
    std::string_view quaternaryHint;
    float primaryDefaultValue = 0.5f;
    float secondaryDefaultValue = 0.5f;
    float tertiaryDefaultValue = 0.5f;
    float quaternaryDefaultValue = 0.5f;
    std::string_view selectorLabel = "Mode";
    std::array<std::string_view, 2> selectorChoiceLabels {};
    std::string_view learnParamId;
    std::string_view learnButtonLabel;
    bool showLevelTrace = false;
    // Decorative filter-curve icons drawn above the knobs (no interactivity).
    bool showLowShelfIcon  = false;   // HP/low-shelf shape
    bool showHighShelfIcon = false;   // LP/high-shelf shape

    // Optional params for shelf toggles (kept for processor use, not connected to icons).
    std::string_view lowShelfParamId;
    std::string_view highShelfParamId;
    bool defaultLowShelf  = true;
    bool defaultHighShelf = true;
    Mode defaultMode = Mode::vocal;
    StageType stageType = StageType::unknown;
    std::uint32_t semanticFlags = 0;
    std::uint32_t telemetryFlags = 0;
    ProductTheme theme {};

    bool supportsModeSwitch() const noexcept {
        return !modeParamId.empty();
    }

    bool supportsListenMode() const noexcept {
        return !listenParamId.empty();
    }

    bool supportsTertiaryControl() const noexcept {
        return !tertiaryParamId.empty();
    }

    bool supportsQuaternaryControl() const noexcept {
        return !quaternaryParamId.empty();
    }

    bool supportsLearnButton() const noexcept {
        return !learnParamId.empty();
    }

    std::string_view selectorChoiceLabel(const size_t index) const noexcept {
        return index < selectorChoiceLabels.size() ? selectorChoiceLabels[index] : std::string_view{};
    }

};

} // namespace vxsuite
