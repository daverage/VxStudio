#pragma once

#include <array>
#include <string_view>

namespace vxsuite {

enum class Mode : int {
    vocal = 0,
    general = 1
};

struct ProductTheme {
    std::array<float, 3> accentRgb { 0.00f, 0.82f, 1.00f };
    std::array<float, 3> accent2Rgb { 0.10f, 0.12f, 0.16f };
    std::array<float, 3> backgroundRgb { 0.05f, 0.05f, 0.07f };
    std::array<float, 3> panelRgb { 0.09f, 0.09f, 0.12f };
    std::array<float, 3> textRgb { 0.86f, 0.91f, 1.00f };
};

struct ProductIdentity {
    std::string_view suiteName;
    std::string_view productName;
    std::string_view shortTag;
    std::string_view primaryParamId;
    std::string_view secondaryParamId;
    std::string_view tertiaryParamId;
    std::string_view modeParamId;
    std::string_view listenParamId;
    std::string_view primaryLabel;
    std::string_view secondaryLabel;
    std::string_view tertiaryLabel;
    std::string_view primaryHint;
    std::string_view secondaryHint;
    std::string_view tertiaryHint;
    Mode defaultMode = Mode::vocal;
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

    // Optional shelf-type filter icons drawn on the body separator line.
    // showLowShelfIcon  → HP/low-shelf curve on the left  (e.g. deMud)
    // showHighShelfIcon → LP/high-shelf curve on the right (e.g. deEss)
    bool showLowShelfIcon  = false;
    bool showHighShelfIcon = false;

    // Optional parameter IDs for shelf toggle bools (empty = icon is decorative only).
    // When set, the icon becomes a click-toggle: icon dims when the param is false.
    std::string_view lowShelfParamId;
    std::string_view highShelfParamId;

    bool supportsLowShelfToggle()  const noexcept { return !lowShelfParamId.empty(); }
    bool supportsHighShelfToggle() const noexcept { return !highShelfParamId.empty(); }
};

} // namespace vxsuite
