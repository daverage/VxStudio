#include "VxRebalanceEditor.h"

#include "../../framework/VxStudioEditorBase.h"
#include "VxRebalanceProcessor.h"

#include <algorithm>

namespace {

juce::Colour colourFromRgb(const std::array<float, 3>& rgb) {
    return juce::Colour::fromFloatRGBA(rgb[0], rgb[1], rgb[2], 1.0f);
}

juce::Colour sourceColour(const int source) {
    switch (source) {
        case vxsuite::rebalance::Dsp::vocalsSource:
            return juce::Colour(0xfff69d72);
        case vxsuite::rebalance::Dsp::drumsSource:
            return juce::Colour(0xff64d0ff);
        case vxsuite::rebalance::Dsp::bassSource:
            return juce::Colour(0xff8edc8c);
        case vxsuite::rebalance::Dsp::guitarSource:
            return juce::Colour(0xffffcb74);
        default:
            return juce::Colour(0xffb694ff);
    }
}

constexpr int kUiRefreshHz = 15;
constexpr float kDefaultShellScale = 0.82f;

} // namespace

class VXRebalanceEditor::DebugPanel final : public juce::Component {
public:
    void setSnapshot(const vxsuite::rebalance::Dsp::DebugSnapshot& next) {
        snapshot = next;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().reduced(12);
        g.setColour(juce::Colour(0xff141722));
        g.fillRoundedRectangle(bounds.toFloat(), 14.0f);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(bounds.toFloat(), 14.0f, 1.0f);

        auto header = bounds.removeFromTop(26);
        g.setColour(juce::Colours::white.withAlpha(0.88f));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText("Diagnostics | Dominant Bin Ownership", header, juce::Justification::centredLeft, false);

        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText("Conf " + juce::String(snapshot.overallConfidence * 100.0f, 0) + "%  |  Frame "
                       + juce::String(snapshot.frameCounter),
                   header, juce::Justification::centredRight, false);

        auto legend = bounds.removeFromTop(20);
        constexpr std::array<const char*, vxsuite::rebalance::Dsp::kSourceCount> labels {
            "Vox", "Drums", "Bass", "Gtr", "Other"
        };
        const int slotWidth = legend.getWidth() / vxsuite::rebalance::Dsp::kSourceCount;

        for (int s = 0; s < vxsuite::rebalance::Dsp::kSourceCount; ++s) {
            auto item = legend.removeFromLeft(slotWidth);
            const auto chip = item.removeFromLeft(10).withSizeKeepingCentre(8, 8);
            g.setColour(sourceColour(s));
            g.fillEllipse(chip.toFloat());
            g.setColour(juce::Colours::white.withAlpha(0.65f));
            g.drawText(juce::String(labels[static_cast<size_t>(s)]) + " "
                           + juce::String(snapshot.dominantCoverage[static_cast<size_t>(s)] * 100.0f, 0) + "%",
                       item.reduced(6, 0), juce::Justification::centredLeft, false);
        }

        auto plot = bounds.reduced(4, 6);
        const auto plotFrame = plot.toFloat();
        g.setColour(juce::Colour(0xff0d1118));
        g.fillRoundedRectangle(plotFrame, 10.0f);

        for (int line = 1; line < 4; ++line) {
            const float y = plotFrame.getY() + plotFrame.getHeight() * (static_cast<float>(line) / 4.0f);
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            g.drawHorizontalLine(static_cast<int>(y), plotFrame.getX(), plotFrame.getRight());
        }

        const float binWidth = plotFrame.getWidth() / static_cast<float>(vxsuite::rebalance::Dsp::kDebugBins);
        for (int i = 0; i < vxsuite::rebalance::Dsp::kDebugBins; ++i) {
            const float x = plotFrame.getX() + binWidth * static_cast<float>(i);
            const float confidence = juce::jlimit(0.0f, 1.0f, snapshot.confidence[static_cast<size_t>(i)]);
            const float dominantMask = juce::jlimit(0.0f, 1.0f, snapshot.dominantMasks[static_cast<size_t>(i)]);
            const float otherMask = juce::jlimit(0.0f, 1.0f, snapshot.otherMasks[static_cast<size_t>(i)]);
            const float height = plotFrame.getHeight() * std::max(0.06f, dominantMask);
            const auto colour = sourceColour(snapshot.dominantSources[static_cast<size_t>(i)])
                .withAlpha(0.20f + 0.75f * confidence);

            g.setColour(colour);
            g.fillRect(juce::Rectangle<float>(x, plotFrame.getBottom() - height, std::max(1.0f, binWidth - 1.0f), height));

            const float otherY = plotFrame.getBottom() - plotFrame.getHeight() * otherMask;
            g.setColour(juce::Colour(0xffff86c8).withAlpha(0.55f));
            g.drawLine(x, otherY, x + std::max(1.0f, binWidth - 1.0f), otherY, 1.0f);
        }
    }

private:
    vxsuite::rebalance::Dsp::DebugSnapshot snapshot;
};

VXRebalanceEditor::VXRebalanceEditor(VXRebalanceAudioProcessor& processorToUse)
    : juce::AudioProcessorEditor(processorToUse),
      processor(processorToUse),
      mainEditor(std::make_unique<vxsuite::EditorBase>(processorToUse)),
      debugPanel(std::make_unique<DebugPanel>()) {
    addAndMakeVisible(*mainEditor);
    diagnosticsToggleButton.setButtonText("Show Diagnostics");
    diagnosticsToggleButton.onClick = [this] {
        diagnosticsExpanded = !diagnosticsExpanded;
        diagnosticsToggleButton.setButtonText(diagnosticsExpanded ? "Hide Diagnostics" : "Show Diagnostics");
        updateLayout();
    };
    addAndMakeVisible(diagnosticsToggleButton);
    addAndMakeVisible(*debugPanel);
    updateLayout();
    startTimerHz(kUiRefreshHz);
}

VXRebalanceEditor::~VXRebalanceEditor() {
    stopTimer();
}

void VXRebalanceEditor::paint(juce::Graphics& g) {
    const auto& theme = processor.getProductIdentity().theme;
    g.fillAll(colourFromRgb(theme.backgroundRgb));
    if (diagnosticsExpanded) {
        const auto panelBounds = debugPanel->getBounds().expanded(0, 6).toFloat();
        g.setColour(colourFromRgb(theme.panelRgb).darker(0.15f));
        g.fillRoundedRectangle(panelBounds, 16.0f);
    }
}

void VXRebalanceEditor::resized() {
    const float shellScale = kDefaultShellScale * uiScale;
    const int toggleHeight = juce::roundToInt(32.0f * uiScale);
    const int panelHeight = diagnosticsExpanded ? juce::roundToInt(188.0f * uiScale) : 0;
    const int width = getWidth();
    const int baseWidth = mainEditor->getWidth();
    const int baseHeight = mainEditor->getHeight();
    const int mainHeight = juce::roundToInt(baseHeight * shellScale);

    mainEditor->setTransform(juce::AffineTransform::scale(shellScale));
    mainEditor->setBounds(0, 0, baseWidth, baseHeight);

    auto footer = juce::Rectangle<int>(0, mainHeight, width, getHeight() - mainHeight).reduced(12, 8);
    diagnosticsToggleButton.setBounds(footer.removeFromTop(toggleHeight).removeFromRight(170));
    debugPanel->setVisible(diagnosticsExpanded);
    if (diagnosticsExpanded)
        debugPanel->setBounds(footer.withTrimmedTop(6));
}

void VXRebalanceEditor::setScaleFactor(const float newScale) {
    uiScale = newScale;
    juce::AudioProcessorEditor::setScaleFactor(newScale);
    mainEditor->setScaleFactor(newScale);
    updateLayout();
}

void VXRebalanceEditor::timerCallback() {
    debugPanel->setSnapshot(processor.getDebugSnapshot());
}

void VXRebalanceEditor::updateLayout() {
    const float shellScale = kDefaultShellScale * uiScale;
    const int toggleHeight = juce::roundToInt(32.0f * uiScale);
    const int panelHeight = diagnosticsExpanded ? juce::roundToInt(188.0f * uiScale) : 0;
    const int width = juce::roundToInt(mainEditor->getWidth() * shellScale);
    const int height = juce::roundToInt(mainEditor->getHeight() * shellScale) + toggleHeight + panelHeight;
    setSize(width, height);
}
