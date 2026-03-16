#pragma once

#include "VxSuiteProduct.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace vxsuite {

class SuiteLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    explicit SuiteLookAndFeel(const ProductTheme& theme);

    void drawRotarySlider(juce::Graphics& g,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider&) override;

    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    void drawComboBox(juce::Graphics& g,
                      int width,
                      int height,
                      bool isButtonDown,
                      int buttonX,
                      int buttonY,
                      int buttonW,
                      int buttonH,
                      juce::ComboBox& box) override;

    juce::Font getComboBoxFont(juce::ComboBox&) override;

private:
    juce::Colour accent;
    juce::Colour accent2;
    juce::Colour panel;
    juce::Colour text;
};

} // namespace vxsuite
