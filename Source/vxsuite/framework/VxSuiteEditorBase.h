#pragma once

#include "VxSuiteHelpView.h"
#include "VxSuiteLookAndFeel.h"
#include "VxSuiteLevelTraceView.h"
#include "VxSuiteProcessorBase.h"

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
    void applyTextFit();

    SuiteLookAndFeel lookAndFeel;
    float uiScale = 1.0f;
    double learnMeterUi = 0.0;
    int activityLightCount = 0;
    std::array<float, 8> activityLights {};
    juce::Label suiteLabel;
    juce::Label productLabel;
    juce::Label modeLabel;
    juce::Label auxSelectorLabel;
    juce::Label statusLabel;
    juce::Label learnMeterLabel;
    juce::Label modelDownloadLabel;
    juce::ProgressBar learnMeterBar { learnMeterUi };
    double modelDownloadUi = 0.0;
    juce::ProgressBar modelDownloadBar { modelDownloadUi };
    juce::ComboBox traceZoomBox;
    juce::ComboBox modeBox;
    juce::ComboBox auxSelectorBox;
    HelpButton helpButton;
    juce::TextButton modelButton;
    juce::ToggleButton listenButton;
    juce::TextButton learnButton;
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
    std::unique_ptr<ButtonAttachment> listenAttachment;
    std::unique_ptr<ButtonAttachment> learnAttachment;
    juce::TooltipWindow tooltipWindow;
    LevelTraceView levelTraceView;
    juce::Rectangle<int> activityStripBounds;
    juce::Rectangle<int> traceViewBounds;
    juce::Rectangle<int> lowShelfIconBounds;
    juce::Rectangle<int> highShelfIconBounds;
    bool lastLowShelfOn  = false;
    bool lastHighShelfOn = false;
    int traceMissTicks = 0;
    juce::String transientStatusText;
    int transientStatusTicks = 0;
    bool modelPromptVisible = false;
};

} // namespace vxsuite
