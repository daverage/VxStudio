#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace vxsuite {

enum class Mode : int {
    vocal = 0,
    general = 1,
    extended = 2
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
    static constexpr size_t maxControlBankControls = 6;
    static constexpr size_t maxUiPresets = 6;

    std::string_view suiteName = "VX Suite";
    std::string_view productName;
    std::string_view shortTag;
    std::string_view stageId;
    std::string_view dspVersion = "0.1.0";
    std::string_view primaryParamId;
    std::string_view secondaryParamId;
    std::string_view tertiaryParamId;
    std::string_view quaternaryParamId;
    std::string_view modeParamId;
    std::string_view auxSelectorParamId;
    std::string_view auxSelector2ParamId;
    std::string_view expertParamId;
    std::string_view listenParamId;
    std::string_view primaryLabel;
    std::string_view secondaryLabel;
    std::string_view tertiaryLabel;
    std::string_view quaternaryLabel;
    std::string_view primaryHint;
    std::string_view secondaryHint;
    std::string_view tertiaryHint;
    std::string_view quaternaryHint;
    // Keep these aligned with the public README whenever UI, behavior, or usage guidance changes.
    std::string_view helpTitle;
    std::string_view helpHtml;
    std::string_view readmeSection;
    float primaryDefaultValue = 0.5f;
    float secondaryDefaultValue = 0.5f;
    float tertiaryDefaultValue = 0.5f;
    float quaternaryDefaultValue = 0.5f;
    std::string_view selectorLabel = "Mode";
    std::array<std::string_view, 3> selectorChoiceLabels {};
    std::string_view auxSelectorLabel;
    std::string_view auxSelector2Label;
    std::string_view presetSelectorLabel;
    std::string_view expertButtonLabel = "Pro";
    // Sized for musical selectors (e.g. key/scale lists); products fill as
    // many leading entries as they need.
    std::array<std::string_view, 26> auxSelectorChoiceLabels {};
    std::array<std::string_view, 26> auxSelector2ChoiceLabels {};
    int auxSelectorDefaultIndex = 0;
    int auxSelector2DefaultIndex = 0;
    int presetChoiceCount = 0;
    std::array<std::string_view, maxUiPresets> presetChoiceLabels {};
    std::array<float, maxUiPresets> presetPrimaryValues {};
    std::array<float, maxUiPresets> presetSecondaryValues {};
    std::array<float, maxUiPresets> presetTertiaryValues {};
    std::array<float, maxUiPresets> presetQuaternaryValues {};
    std::array<int, maxUiPresets> presetAuxSelectorIndexes { -1, -1, -1, -1, -1, -1 };
    std::array<int, maxUiPresets> presetAuxSelector2Indexes { -1, -1, -1, -1, -1, -1 };
    bool auxSelectorFollowsGeneralMode = true;
    bool auxSelector2FollowsGeneralMode = true;
    bool auxSelectorRequiresExpert = false;
    bool auxSelector2RequiresExpert = false;
    bool tertiaryRequiresExpert = false;
    bool quaternaryRequiresExpert = false;
    // Show the tertiary/quaternary knobs only while the vocal mode is active.
    bool extraControlsFollowVocalMode = false;
    // > 0: display the knob as a centred +/- dB value with this max magnitude
    // (0 keeps the default percent display).
    float tertiaryCenteredDbRange = 0.0f;
    float quaternaryCenteredDbRange = 0.0f;
    bool expertDefaultValue = false;
    std::string_view learnParamId;
    std::string_view learnButtonLabel;
    // Generic plain on/off toggle - deliberately NOT "Listen" (audition-delta
    // semantics) or "Learn"/"Analyze" (drags in progress/confidence meter
    // UI, see updateLearnUi()) or "Expert" (gates other controls' visibility
    // elsewhere). For a product that just needs one simple persistent
    // switch with no side effects on the rest of the UI - e.g. VX Width's
    // experimental micro-pitch toggle.
    std::string_view simpleToggleParamId;
    std::string_view simpleToggleLabel = "Toggle";
    bool simpleToggleDefaultValue = false;
    bool showLevelTrace = false;
    bool showStereoGainMeter = false;
    // Scrolling pitch trace (detected vs corrected pitch on a note grid);
    // requires the ProcessorBase pitch-trace virtuals.
    bool showPitchTrace = false;
    // VX Width Phase 2 (docs/Task Based/VX_WIDTH_ENGINE_UPGRADE.md): a
    // dedicated real-time input/target/actual spatial-width visualiser plus
    // mono/stereo-adaptive Width knob presentation. Requires
    // ProcessorBase::getSpatialWidthTelemetry() to return a value.
    bool showSpatialWidthVisualizer = false;
    // Adds a stereo "Sidechain" input bus (disabled until the host routes to
    // it). Detection-only: the framework never writes SC channels to output.
    bool wantsSidechainInput = false;
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
    bool requiresVoiceAnalysis = true;  // Products like Rebalance don't need this
    bool requiresSignalQuality = false;  // Only a few products use this
    int controlBankCount = 0;
    bool controlBankVertical = true;
    bool compactControlBankLayout = false;
    std::array<std::string_view, maxControlBankControls> controlBankParamIds {};
    std::array<std::string_view, maxControlBankControls> controlBankLabels {};
    std::array<std::string_view, maxControlBankControls> controlBankHints {};
    std::array<float, maxControlBankControls> controlBankDefaultValues {};
    ProductTheme theme {};

    bool supportsControlBank() const noexcept {
        return controlBankCount > 0;
    }

    int clampedControlBankCount() const noexcept {
        return static_cast<int>(controlBankCount < 0
            ? 0
            : (controlBankCount > static_cast<int>(maxControlBankControls)
                ? static_cast<int>(maxControlBankControls)
                : controlBankCount));
    }

    bool supportsModeSwitch() const noexcept {
        return !modeParamId.empty();
    }

    bool supportsListenMode() const noexcept {
        return !listenParamId.empty();
    }

    bool supportsAuxSelector() const noexcept {
        return !auxSelectorParamId.empty();
    }

    bool supportsAuxSelector2() const noexcept {
        return !auxSelector2ParamId.empty();
    }

    bool supportsPresetSelector() const noexcept {
        return presetChoiceCount > 0 && !primaryParamId.empty() && !secondaryParamId.empty();
    }

    int clampedPresetChoiceCount() const noexcept {
        return presetChoiceCount < 0
            ? 0
            : (presetChoiceCount > static_cast<int>(maxUiPresets)
                ? static_cast<int>(maxUiPresets)
                : presetChoiceCount);
    }

    bool supportsExpertMode() const noexcept {
        return !expertParamId.empty();
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

    bool supportsSimpleToggle() const noexcept {
        return !simpleToggleParamId.empty();
    }

    bool hasHelpContent() const noexcept {
        return !helpHtml.empty();
    }

    std::string_view selectorChoiceLabel(const size_t index) const noexcept {
        return index < selectorChoiceLabels.size() ? selectorChoiceLabels[index] : std::string_view{};
    }

    std::string_view auxSelectorChoiceLabel(const size_t index) const noexcept {
        return index < auxSelectorChoiceLabels.size() ? auxSelectorChoiceLabels[index] : std::string_view{};
    }

    std::string_view auxSelector2ChoiceLabel(const size_t index) const noexcept {
        return index < auxSelector2ChoiceLabels.size() ? auxSelector2ChoiceLabels[index] : std::string_view{};
    }

    std::string_view presetChoiceLabel(const size_t index) const noexcept {
        return index < presetChoiceLabels.size() ? presetChoiceLabels[index] : std::string_view{};
    }

};

} // namespace vxsuite
