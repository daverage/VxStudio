#pragma once

#include "VxStudioHelpView.h"
#include "VxStudioLookAndFeel.h"
#include "VxStudioLevelTraceView.h"
#include "VxStudioGainMeterView.h"
#include "VxStudioPitchTraceView.h"
#include "VxStudioProcessorBase.h"
#include "VxStudioSpatialWidthView.h"

#include <array>
#include <memory>

namespace vxsuite {

class EditorBase : public juce::AudioProcessorEditor,
                   private juce::Timer {
public:
    explicit EditorBase(ProcessorBase&);
    ~EditorBase() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void setScaleFactor(float newScale) override;

    // Test-only visibility (Phase 2 spatial-width UI, VX_WIDTH_ENGINE_UPGRADE.md
    // §16): the real update runs off a private juce::Timer, which headless
    // tests can't pump without a running message loop - these let a test
    // drive/observe the exact same logic deterministically. Not used by
    // production code paths.
    void pumpSpatialWidthUiForTesting() { updateSpatialWidthUi(); }
    bool isWidthPresentationMonoLikeForTesting() const noexcept { return widthPresentationIsMonoLike; }
    juce::String getPrimaryDisplayTextForTesting() { return primarySlider.getTextFromValue(primarySlider.getValue()); }
    double getPrimarySliderValueForTesting() const { return primarySlider.getValue(); }
    void applyUiPresetForTesting(const int presetIndex) { applyUiPreset(presetIndex); }

protected:
    ProcessorBase& processor;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void configureKnob(juce::Slider& slider, juce::Label& label, std::string_view text, std::string_view hint);
    void configureBankControl(int index);
    void mouseDown(const juce::MouseEvent&) override;
    void timerCallback() override;
    int scaled(int value) const;
    juce::String footerText() const;
    void showTransientStatus(const juce::String& text);
    void showModelDownloadPrompt(bool automatic);
    void updateActivityIndicators();
    void updateLearnUi();
    void updateModelDownloadUi();
    void updateAuxSelectorUi();
    void applyUiPreset(int presetIndex);
    void updateExpertUi();
    bool lastExtrasAllowed = true;
    void applyTextFit();
    // Phase 2 spatial-width UI (VX_WIDTH_ENGINE_UPGRADE.md §5, §13):
    // hysteresis/dwell-gated mono<->stereo presentation state, purely
    // display-side - never writes to processor.getValueTreeState() (§1).
    void updateSpatialWidthUi();
    // Remaps primarySlider's OWN interactive range per mono/stereo mode -
    // never the stored parameter. See its definition for the full rationale.
    void applyWidthKnobRangeForCurrentMode();
    bool widthPresentationIsMonoLike = true;
    float widthMonoConfidenceSmoothed = 1.0f;
    int widthModeDwellTicksElapsed = 0;

    SuiteLookAndFeel lookAndFeel;
    float uiScale = 1.0f;
    double learnMeterUi = 0.0;
    int activityLightCount = 0;
    std::array<float, 8> activityLights {};
    juce::Label suiteLabel;
    juce::Label productLabel;
    juce::Label modeLabel;
    juce::Label auxSelectorLabel;
    juce::Label auxSelector2Label;
    juce::Label presetSelectorLabel;
    juce::Label statusLabel;
    juce::Label learnMeterLabel;
    juce::Label modelDownloadLabel;
    juce::ProgressBar learnMeterBar { learnMeterUi };
    double modelDownloadUi = 0.0;
    juce::ProgressBar modelDownloadBar { modelDownloadUi };
    juce::ComboBox traceZoomBox;
    juce::ComboBox pitchZoomYBox;
    juce::ComboBox modeBox;
    juce::ComboBox auxSelectorBox;
    juce::ComboBox auxSelector2Box;
    juce::ComboBox presetSelectorBox;
    juce::ToggleButton expertButton;
    HelpButton helpButton;
    juce::TextButton modelButton;
    juce::ToggleButton listenButton;
    juce::TextButton learnButton;
    juce::ToggleButton simpleToggleButton;
    juce::Slider primarySlider;
    juce::Slider secondarySlider;
    juce::Slider tertiarySlider;
    juce::Slider quaternarySlider;
    std::array<juce::Slider, ProductIdentity::maxControlBankControls> bankSliders;
    juce::Label primaryLabel;
    juce::Label secondaryLabel;
    juce::Label tertiaryLabel;
    juce::Label quaternaryLabel;
    std::array<juce::Label, ProductIdentity::maxControlBankControls> bankLabels;
    juce::Label primaryHint;
    juce::Label secondaryHint;
    juce::Label tertiaryHint;
    juce::Label quaternaryHint;
    std::array<juce::Label, ProductIdentity::maxControlBankControls> bankHints;
    std::unique_ptr<SliderAttachment> primaryAttachment;
    std::unique_ptr<SliderAttachment> secondaryAttachment;
    std::unique_ptr<SliderAttachment> tertiaryAttachment;
    std::unique_ptr<SliderAttachment> quaternaryAttachment;
    std::array<std::unique_ptr<SliderAttachment>, ProductIdentity::maxControlBankControls> bankAttachments;
    std::unique_ptr<ComboAttachment> modeAttachment;
    std::unique_ptr<ComboAttachment> auxSelectorAttachment;
    std::unique_ptr<ComboAttachment> auxSelector2Attachment;
    std::unique_ptr<ButtonAttachment> expertAttachment;
    std::unique_ptr<ButtonAttachment> listenAttachment;
    std::unique_ptr<ButtonAttachment> learnAttachment;
    std::unique_ptr<ButtonAttachment> simpleToggleAttachment;
    juce::TooltipWindow tooltipWindow;
    LevelTraceView levelTraceView;
    GainMeterView gainMeterView;
    PitchTraceView pitchTraceView;
    SpatialWidthView spatialWidthView;
    juce::Rectangle<int> activityStripBounds;
    juce::Rectangle<int> gainMeterBounds;
    juce::Rectangle<int> traceViewBounds;
    juce::Rectangle<int> spatialWidthViewBounds;
    juce::Rectangle<int> lowShelfIconBounds;
    juce::Rectangle<int> highShelfIconBounds;
    juce::Rectangle<int> sidechainBadgeBounds;
    bool lastLowShelfOn  = false;
    bool lastHighShelfOn = false;
    bool lastSidechainActive = false;
    bool lastExpertEnabled = false;
    int traceMissTicks = 0;
    juce::String transientStatusText;
    int transientStatusTicks = 0;
    bool modelPromptVisible = false;
};

} // namespace vxsuite
