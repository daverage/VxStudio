#include "VxSuiteLookAndFeel.h"

#include <algorithm>

namespace vxsuite {
namespace {

juce::Colour rgb(const std::array<float, 3>& value) {
    return juce::Colour::fromFloatRGBA(value[0], value[1], value[2], 1.0f);
}

} // namespace

SuiteLookAndFeel::SuiteLookAndFeel(const ProductTheme& theme)
    : accent(rgb(theme.accentRgb)),
      accent2(rgb(theme.accent2Rgb)),
      panel(rgb(theme.panelRgb)),
      text(rgb(theme.textRgb)) {
    setColour(juce::ResizableWindow::backgroundColourId, rgb(theme.backgroundRgb));
    setColour(juce::Label::textColourId, text);
    setColour(juce::ComboBox::backgroundColourId, accent2);
    setColour(juce::ComboBox::outlineColourId, panel.brighter(0.18f));
    setColour(juce::ComboBox::textColourId, text);
    setColour(juce::ComboBox::arrowColourId, accent.withAlpha(0.9f));
    setColour(juce::TextButton::buttonColourId, accent2);
    setColour(juce::TextButton::textColourOffId, text);
    setColour(juce::Slider::textBoxBackgroundColourId, accent2);
    setColour(juce::Slider::textBoxTextColourId, text);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::rotarySliderOutlineColourId, panel.brighter(0.25f));
    setColour(juce::Slider::rotarySliderFillColourId, accent);
}

void SuiteLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                        int x,
                                        int y,
                                        int width,
                                        int height,
                                        float sliderPosProportional,
                                        float rotaryStartAngle,
                                        float rotaryEndAngle,
                                        juce::Slider& slider) {
    auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                         static_cast<float>(width), static_cast<float>(height));
    const float diameter = std::max(1.0f, std::min(bounds.getWidth(), bounds.getHeight()) - 28.0f);
    bounds = juce::Rectangle<float>(diameter, diameter).withCentre(bounds.getCentre());
    const auto radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    const auto arcBounds = bounds.reduced(radius * 0.18f);

    g.setColour(panel.brighter(0.05f));
    g.fillEllipse(bounds);

    g.setColour(juce::Colours::black.withAlpha(0.18f));
    g.fillEllipse(bounds.reduced(radius * 0.48f));

    juce::Path track;
    track.addCentredArc(centre.x,
                        centre.y,
                        arcBounds.getWidth() * 0.5f,
                        arcBounds.getHeight() * 0.5f,
                        0.0f,
                        rotaryStartAngle,
                        rotaryEndAngle,
                        true);
    g.setColour(text.withAlpha(0.12f));
    g.strokePath(track,
                 juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path arc;
    arc.addCentredArc(centre.x,
                      centre.y,
                      arcBounds.getWidth() * 0.5f,
                      arcBounds.getHeight() * 0.5f,
                      0.0f,
                      rotaryStartAngle, angle, true);
    g.setColour(accent);
    g.strokePath(arc,
                 juce::PathStrokeType(4.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path tick;
    tick.addRoundedRectangle(-2.0f, -radius + 18.0f, 4.0f, radius * 0.42f, 1.8f);
    g.setColour(text.withAlpha(0.92f));
    g.fillPath(tick, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));

    if (slider.hasKeyboardFocus(false)) {
        g.setColour(accent.withAlpha(0.85f));
        g.fillEllipse(centre.x - 3.0f, centre.y - 3.0f, 6.0f, 6.0f);
    }
}

void SuiteLookAndFeel::drawButtonBackground(juce::Graphics& g,
                                            juce::Button& button,
                                            const juce::Colour&,
                                            bool shouldDrawButtonAsHighlighted,
                                            bool shouldDrawButtonAsDown) {
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    auto fill = accent2;
    if (shouldDrawButtonAsDown)
        fill = fill.brighter(0.22f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter(0.10f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(accent.withAlpha(0.85f));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
}

void SuiteLookAndFeel::drawComboBox(juce::Graphics& g,
                                    int width,
                                    int height,
                                    bool isButtonDown,
                                    int buttonX,
                                    int buttonY,
                                    int buttonW,
                                    int buttonH,
                                    juce::ComboBox&) {
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(1.0f);
    auto fill = accent2.brighter(isButtonDown ? 0.08f : 0.0f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, 8.0f);

    g.setColour(panel.brighter(0.26f));
    g.drawRoundedRectangle(bounds, 8.0f, 1.0f);

    const auto arrowArea = juce::Rectangle<float>(static_cast<float>(buttonX),
                                                  static_cast<float>(buttonY),
                                                  static_cast<float>(buttonW),
                                                  static_cast<float>(buttonH)).reduced(8.0f, 10.0f);
    juce::Path arrow;
    arrow.startNewSubPath(arrowArea.getX(), arrowArea.getY());
    arrow.lineTo(arrowArea.getCentreX(), arrowArea.getBottom());
    arrow.lineTo(arrowArea.getRight(), arrowArea.getY());

    g.setColour(accent.withAlpha(0.92f));
    g.strokePath(arrow, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font SuiteLookAndFeel::getComboBoxFont(juce::ComboBox&) {
    return juce::FontOptions().withHeight(16.0f);
}

} // namespace vxsuite
