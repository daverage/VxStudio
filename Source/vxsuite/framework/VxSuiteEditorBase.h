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
    void mouseDown(const juce::MouseEvent&) override;

protected:
    ProcessorBase& processor;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void configureKnob(juce::Slider& slider, juce::Label& label, std::string_view text, std::string_view hint);
    void timerCallback() override;
    int scaled(int value) const;
    juce::String footerText() const;

    SuiteLookAndFeel lookAndFeel;
    float uiScale = 1.0f;
    juce::Label suiteLabel;
    juce::Label productLabel;
    juce::Label modeLabel;
    juce::Label statusLabel;
    juce::ComboBox modeBox;
    juce::ToggleButton listenButton;
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

    juce::Rectangle<int> lowShelfIconBounds;
    juce::Rectangle<int> highShelfIconBounds;

    bool lastLowShelfOn  = true;
    bool lastHighShelfOn = true;
};

} // namespace vxsuite
