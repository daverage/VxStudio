#pragma once

#include "VxSuiteLookAndFeel.h"
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
    class ShelfIconButton;

    void configureKnob(juce::Slider& slider, juce::Label& label, std::string_view text, std::string_view hint);
    void configureShelfButton(ShelfIconButton& button, std::string_view tooltip);
    void timerCallback() override;
    int scaled(int value) const;
    juce::String footerText() const;
    void updateActivityIndicators();
    void updateLearnUi();

    SuiteLookAndFeel lookAndFeel;
    float uiScale = 1.0f;
    float lowShelfActivity = 0.0f;
    float highShelfActivity = 0.0f;
    double learnMeterUi = 0.0;
    juce::Label suiteLabel;
    juce::Label productLabel;
    juce::Label modeLabel;
    juce::Label statusLabel;
    juce::Label learnMeterLabel;
    juce::ProgressBar learnMeterBar { learnMeterUi };
    juce::ComboBox modeBox;
    juce::ToggleButton listenButton;
    juce::TextButton learnButton;
    std::unique_ptr<ShelfIconButton> lowShelfButton;
    std::unique_ptr<ShelfIconButton> highShelfButton;
    juce::Slider primarySlider;
    juce::Slider secondarySlider;
    juce::Slider tertiarySlider;
    juce::Label primaryLabel;
    juce::Label secondaryLabel;
    juce::Label tertiaryLabel;
    juce::Label primaryHint;
    juce::Label secondaryHint;
    juce::Label tertiaryHint;
    std::unique_ptr<SliderAttachment> primaryAttachment;
    std::unique_ptr<SliderAttachment> secondaryAttachment;
    std::unique_ptr<SliderAttachment> tertiaryAttachment;
    std::unique_ptr<ComboAttachment> modeAttachment;
    std::unique_ptr<ButtonAttachment> listenAttachment;
    std::unique_ptr<ButtonAttachment> learnAttachment;
    std::unique_ptr<ButtonAttachment> lowShelfAttachment;
    std::unique_ptr<ButtonAttachment> highShelfAttachment;
    juce::TooltipWindow tooltipWindow;
};

} // namespace vxsuite
