#include "VxSuiteUiHelpers.h"

#include <algorithm>

namespace vxsuite {

namespace {

float widestLineWidth(const juce::Font& font, const juce::String& text) {
    juce::StringArray lines;
    lines.addLines(text);
    float widest = 0.0f;
    for (const auto& line : lines)
        widest = std::max(widest, font.getStringWidthFloat(line));
    return widest;
}

float fittedHeightForText(const juce::Font& baseFont,
                          const juce::String& text,
                          const float width,
                          const float preferredHeight,
                          const float minimumHeight) {
    if (text.isEmpty() || width <= 1.0f)
        return preferredHeight;

    const auto trialFont = baseFont.withHeight(preferredHeight);
    const float measuredWidth = widestLineWidth(trialFont, text);
    if (measuredWidth <= width)
        return preferredHeight;

    const float scale = juce::jlimit(minimumHeight / std::max(preferredHeight, 1.0f),
                                     1.0f,
                                     width / std::max(measuredWidth, 1.0f));
    return juce::jlimit(minimumHeight, preferredHeight, preferredHeight * scale);
}

} // namespace

void fitLabelFontToBounds(juce::Label& label,
                          const float preferredHeight,
                          const float minimumHeight,
                          [[maybe_unused]] const int maxLines) {
    const auto baseFont = label.getFont();
    const float fittedHeight = fittedHeightForText(baseFont,
                                                   label.getText(),
                                                   static_cast<float>(label.getWidth()),
                                                   preferredHeight,
                                                   minimumHeight);
    label.setMinimumHorizontalScale(1.0f);
    label.setFont(baseFont.withHeight(fittedHeight));
}

} // namespace vxsuite
