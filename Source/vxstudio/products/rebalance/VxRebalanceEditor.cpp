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
        case vxsuite::rebalance::vocalsSource:
            return juce::Colour(0xfff69d72);
        case vxsuite::rebalance::drumsSource:
            return juce::Colour(0xff64d0ff);
        case vxsuite::rebalance::bassSource:
            return juce::Colour(0xff8edc8c);
        case vxsuite::rebalance::guitarSource:
            return juce::Colour(0xffffcb74);
        default:
            return juce::Colour(0xffb694ff);
    }
}

constexpr int kUiRefreshHz = 15;
constexpr float kDefaultShellScale = 0.82f;
#if VXSTUDIO_REBALANCE_AI_VARIANT
constexpr const char* kAiModeParam = "aiMode";
#endif

} // namespace

class VXRebalanceEditor::DebugPanel final : public juce::Component {
public:
    void setSnapshot(const vxsuite::rebalance::DebugSnapshot& next) {
        snapshot = next;
        repaint();
    }

#if VXSTUDIO_REBALANCE_AI_VARIANT
    void setAiSnapshot(const vxsuite::rebalance::ai::RealtimeStemSplitter::DebugSnapshot& next) {
        aiSnapshot = next;
        repaint();
    }

    void setAiStatusText(juce::String next) {
        aiStatusText = std::move(next);
        repaint();
    }
#endif

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().reduced(12);
        g.setColour(juce::Colour(0xff141722));
        g.fillRoundedRectangle(bounds.toFloat(), 14.0f);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(bounds.toFloat(), 14.0f, 1.0f);

        auto header = bounds.removeFromTop(26);
        g.setColour(juce::Colours::white.withAlpha(0.88f));
        g.setFont(juce::FontOptions(13.0f));
        g.drawText(
#if VXSTUDIO_REBALANCE_AI_VARIANT
            "Diagnostics | AI Stem Mixer",
#else
            "Diagnostics | Dominant Bin Ownership",
#endif
            header, juce::Justification::centredLeft, false);

        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText(
#if VXSTUDIO_REBALANCE_AI_VARIANT
            "Stem Conf "
#else
            "Conf "
#endif
                + juce::String(snapshot.overallConfidence * 100.0f, 0) + "%  |  Frame "
                + juce::String(snapshot.frameCounter),
            header, juce::Justification::centredRight, false);

#if VXSTUDIO_REBALANCE_AI_VARIANT
        auto aiRow = bounds.removeFromTop(18);
        g.setColour(juce::Colours::white.withAlpha(0.52f));
        g.setFont(juce::FontOptions(11.0f));
        const juce::String aiState = aiSnapshot.available
            ? (aiSnapshot.hasFrame ? "AI Splitter" : "AI Waiting")
            : "AI Standby";
        auto aiText = aiState
            + "  Split Conf " + juce::String(aiSnapshot.latestConfidence * 100.0f, 0) + "%"
            + "  In " + juce::String(static_cast<juce::int64>(aiSnapshot.submittedFrames))
            + "  Done " + juce::String(static_cast<juce::int64>(aiSnapshot.completedFrames))
            + "  Fail " + juce::String(static_cast<juce::int64>(aiSnapshot.failedFrames))
            + "  Drop " + juce::String(static_cast<juce::int64>(aiSnapshot.droppedFrames));
        if (aiStatusText.isNotEmpty())
            aiText += "  |  " + aiStatusText;
        g.drawText(aiText, aiRow, juce::Justification::centredLeft, true);
#endif

        auto legend = bounds.removeFromTop(20);
        constexpr std::array<const char*, vxsuite::rebalance::kSourceCount> labels {
            "Vox", "Drums", "Bass", "Gtr", "Other"
        };
        const int slotWidth = legend.getWidth() / vxsuite::rebalance::kSourceCount;

        for (int s = 0; s < vxsuite::rebalance::kSourceCount; ++s) {
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

        const float binWidth = plotFrame.getWidth() / static_cast<float>(vxsuite::rebalance::kDebugBins);
        for (int i = 0; i < vxsuite::rebalance::kDebugBins; ++i) {
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
    vxsuite::rebalance::DebugSnapshot snapshot;
#if VXSTUDIO_REBALANCE_AI_VARIANT
    vxsuite::rebalance::ai::RealtimeStemSplitter::DebugSnapshot aiSnapshot;
    juce::String aiStatusText;
#endif
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
#if VXSTUDIO_REBALANCE_AI_VARIANT
    aiModeLabel.setText("AI Mode", juce::dontSendNotification);
    aiModeLabel.setJustificationType(juce::Justification::centredRight);
    aiModeLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.70f));
    aiModeBox.addItem("AI Assist", 1);
    aiModeBox.addItem("AI Strong", 2);
    aiModeBox.setJustificationType(juce::Justification::centred);
    aiModeBox.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff171018));
    aiModeBox.setColour(juce::ComboBox::outlineColourId, juce::Colours::white.withAlpha(0.16f));
    aiModeBox.setColour(juce::ComboBox::textColourId, juce::Colours::white.withAlpha(0.88f));
    aiModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.getValueTreeState(), kAiModeParam, aiModeBox);
    addAndMakeVisible(aiModeLabel);
    addAndMakeVisible(aiModeBox);
#endif
    addAndMakeVisible(*debugPanel);
    setResizable(true, false);
    setResizeLimits(juce::roundToInt(920.0f * kDefaultShellScale),
                    juce::roundToInt(640.0f * kDefaultShellScale) + 32,
                    juce::roundToInt(1280.0f * kDefaultShellScale),
                    juce::roundToInt(940.0f * kDefaultShellScale) + 32 + 188);
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
    const int footerHeight = toggleHeight + panelHeight;

    const int innerWidth  = juce::roundToInt(static_cast<float>(getWidth())  / shellScale);
    const int innerHeight = juce::roundToInt(static_cast<float>(getHeight() - footerHeight) / shellScale);

    mainEditor->setSize(innerWidth, innerHeight);
    mainEditor->setTransform(juce::AffineTransform::scale(shellScale));
    mainEditor->setBounds(0, 0, innerWidth, innerHeight);

    const int mainHeight = getHeight() - footerHeight;
    auto footer = juce::Rectangle<int>(0, mainHeight, getWidth(), footerHeight).reduced(12, 8);
    auto topRow = footer.removeFromTop(toggleHeight);
    diagnosticsToggleButton.setBounds(topRow.removeFromRight(170));
#if VXSTUDIO_REBALANCE_AI_VARIANT
    topRow.removeFromRight(12);
    aiModeBox.setBounds(topRow.removeFromRight(132));
    aiModeLabel.setBounds(topRow.removeFromRight(74));
#endif
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
#if VXSTUDIO_REBALANCE_AI_VARIANT
    debugPanel->setAiSnapshot(processor.getAiDebugSnapshot());
    debugPanel->setAiStatusText(processor.getAiStatusText());
#endif
}

void VXRebalanceEditor::updateLayout() {
    const float shellScale = kDefaultShellScale * uiScale;
    const int toggleHeight = juce::roundToInt(32.0f * uiScale);
    const int panelHeight = diagnosticsExpanded ? juce::roundToInt(188.0f * uiScale) : 0;
    const int width = juce::roundToInt(mainEditor->getWidth() * shellScale);
    const int height = juce::roundToInt(mainEditor->getHeight() * shellScale) + toggleHeight + panelHeight;
    setSize(width, height);
}
