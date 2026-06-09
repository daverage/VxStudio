#include "VXStudioAnalyserEditor.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kUiRefreshHz = 24;
constexpr float kSpectrumMinDb = -78.0f;
constexpr float kSpectrumMaxDb = -18.0f;

constexpr std::array<int, 9> kAverageTimeOptionsMs { 100, 250, 500, 1000, 1500, 2000, 3000, 5000, 10000 };
constexpr std::array<const char*, 7> kSmoothingOptions {
    "Off", "1/12 OCT", "1/9 OCT", "1/6 OCT", "1/3 OCT", "1/2 OCT", "1 OCT"
};

void hashMix(std::uint64_t& seed, const std::uint64_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
}

void hashString(std::uint64_t& seed, const juce::String& value) noexcept {
    const char* text = value.toRawUTF8();
    if (text == nullptr)
        return;
    while (*text != '\0')
        hashMix(seed, static_cast<std::uint64_t>(static_cast<unsigned char>(*text++)));
}

std::uint64_t fingerprintChainRows(const std::vector<vxanalyser::ChainRow>& rows) noexcept {
    std::uint64_t seed = 0xcbf29ce484222325ULL;
    hashMix(seed, static_cast<std::uint64_t>(rows.size()));
    for (const auto& row : rows) {
        hashMix(seed, static_cast<std::uint64_t>(row.isTrackHeader ? 1 : 0));
        hashMix(seed, row.trackStableId);
        hashMix(seed, row.instanceId);
        hashString(seed, row.stageName);
        hashString(seed, row.trackName);
    }
    return seed;
}

int averageTimeMsFromIndex(const int index) noexcept {
    const int clamped = juce::jlimit(0, static_cast<int>(kAverageTimeOptionsMs.size()) - 1, index);
    return kAverageTimeOptionsMs[static_cast<std::size_t>(clamped)];
}

float bandCenterHz(const int bandIndex) noexcept {
    constexpr float kMinFreq = 20.0f;
    constexpr float kMaxFreq = 20000.0f;
    const float norm = (static_cast<float>(bandIndex) + 0.5f)
        / static_cast<float>(vxsuite::analysis::kSummarySpectrumBins);
    return kMinFreq * std::pow(kMaxFreq / kMinFreq, norm);
}

float xForFrequency(const float hz, const juce::Rectangle<float> bounds) noexcept {
    constexpr float kMinFreq = 20.0f;
    constexpr float kMaxFreq = 20000.0f;
    const double norm = (std::log10(std::max(kMinFreq, hz)) - std::log10(kMinFreq))
        / (std::log10(kMaxFreq) - std::log10(kMinFreq));
    return bounds.getX() + static_cast<float>(norm) * bounds.getWidth();
}

juce::String formatFrequency(const float hz) {
    if (hz >= 1000.0f)
        return juce::String(hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz";
    return juce::String(juce::roundToInt(hz)) + " Hz";
}

juce::String signalQualityLabel(const vxsuite::SignalQualitySnapshot& quality) {
    const juce::String stereoHint = quality.monoScore >= 0.72f ? "Near mono"
        : quality.monoScore >= 0.42f ? "Stereo-limited"
        : "Stereo-open";
    const juce::String dynamicsHint = quality.compressionScore >= 0.72f ? "AGC / crushed"
        : quality.compressionScore >= 0.42f ? "Controlled dynamics"
        : "Natural dynamics";
    const juce::String tiltHint = quality.tiltScore >= 0.72f ? "Low-heavy / lo-fi"
        : quality.tiltScore >= 0.42f ? "Warm / tilted"
        : "Balanced spectrum";
    juce::String confidenceHint = "High trust";
    if (quality.separationConfidence < 0.35f)
        confidenceHint = "Low trust";
    else if (quality.separationConfidence < 0.68f)
        confidenceHint = "Moderate trust";
    const int pct = juce::roundToInt(100.0f * juce::jlimit(0.0f, 1.0f, quality.separationConfidence));
    return "Recording: " + stereoHint
        + "  |  Dynamics: " + dynamicsHint
        + "  |  Tone: " + tiltHint
        + "  |  DSP trust: " + confidenceHint + " (" + juce::String(pct) + "%)";
}

} // namespace

VXStudioAnalyserEditor::VXStudioAnalyserEditor(VXStudioAnalyserAudioProcessor& owner)
    : juce::AudioProcessorEditor(&owner),
      processor(owner),
      lookAndFeel(owner.theme()) {
    setLookAndFeel(&lookAndFeel);
    setResizable(true, false);
    setResizeLimits(1080, 720, 1680, 1100);
    setSize(1260, 820);

    suiteLabel.setText("VX SUITE", juce::dontSendNotification);
    suiteLabel.setFont(juce::FontOptions().withHeight(16.0f).withKerningFactor(0.16f));
    suiteLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.72f));
    addAndMakeVisible(suiteLabel);

    titleLabel.setText("VX Studio Analyser", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions().withHeight(30.0f).withStyle("Bold"));
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("Dry vs wet spectrum for the selected stage or full chain.", juce::dontSendNotification);
    subtitleLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.74f));
    subtitleLabel.setFont(juce::FontOptions().withHeight(14.0f));
    subtitleLabel.setMinimumHorizontalScale(0.75f);
    addAndMakeVisible(subtitleLabel);

    recordingLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.78f));
    recordingLabel.setFont(juce::FontOptions().withHeight(13.0f));
    recordingLabel.setMinimumHorizontalScale(0.62f);
    addAndMakeVisible(recordingLabel);

    statusLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.70f));
    statusLabel.setFont(juce::FontOptions().withHeight(12.5f));
    statusLabel.setMinimumHorizontalScale(0.68f);
    addAndMakeVisible(statusLabel);

    helpButton.onClick = [this] {
        showHelpDialog(*this, processor.getProductIdentity());
    };
    if (processor.getProductIdentity().hasHelpContent())
        addAndMakeVisible(helpButton);

    selectionLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.94f));
    selectionLabel.setFont(juce::FontOptions().withHeight(22.0f).withStyle("Bold"));
    selectionLabel.setMinimumHorizontalScale(0.62f);
    addAndMakeVisible(selectionLabel);

    summaryLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.92f));
    summaryLabel.setFont(juce::FontOptions().withHeight(13.5f));
    summaryLabel.setJustificationType(juce::Justification::topLeft);
    summaryLabel.setMinimumHorizontalScale(0.60f);
    addAndMakeVisible(summaryLabel);

    averageTimeLabel.setText("Avg Time", juce::dontSendNotification);
    averageTimeLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.72f));
    averageTimeLabel.setFont(juce::FontOptions().withHeight(12.5f));
    addAndMakeVisible(averageTimeLabel);

    smoothingLabel.setText("Smoothing", juce::dontSendNotification);
    smoothingLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.72f));
    smoothingLabel.setFont(juce::FontOptions().withHeight(12.5f));
    addAndMakeVisible(smoothingLabel);

    for (int i = 0; i < static_cast<int>(kAverageTimeOptionsMs.size()); ++i)
        averageTimeBox.addItem(juce::String(kAverageTimeOptionsMs[static_cast<std::size_t>(i)]) + " ms", i + 1);
    averageTimeBox.setSelectedId(4, juce::dontSendNotification);
    averageTimeBox.onChange = [this] {
        averageTimeIndex.store(std::max(0, averageTimeBox.getSelectedItemIndex()));
        refreshFromController();
        applyPendingRenderModel();
    };
    addAndMakeVisible(averageTimeBox);

    for (int i = 0; i < static_cast<int>(kSmoothingOptions.size()); ++i)
        smoothingBox.addItem(kSmoothingOptions[static_cast<std::size_t>(i)], i + 1);
    smoothingBox.setSelectedId(4, juce::dontSendNotification);
    smoothingBox.onChange = [this] {
        smoothingIndex.store(std::max(0, smoothingBox.getSelectedItemIndex()));
        refreshFromController();
        applyPendingRenderModel();
    };
    addAndMakeVisible(smoothingBox);

    fullChainButton.setButtonText("Full Chain");
    fullChainButton.onClick = [this] {
        controller.selectFullChain();
        refreshFromController();
        applyPendingRenderModel();
    };
    addAndMakeVisible(fullChainButton);

    diagnosticsToggleButton.setButtonText("Diagnostics >");
    diagnosticsToggleButton.onClick = [this] {
        diagnosticsExpanded = !diagnosticsExpanded;
        diagnosticsToggleButton.setButtonText(diagnosticsExpanded ? "Diagnostics v" : "Diagnostics >");
        diagnosticsEditor.setVisible(diagnosticsExpanded);
        resized();
        repaint();
    };
    addAndMakeVisible(diagnosticsToggleButton);

    copyDiagnosticsButton.setButtonText("Copy");
    copyDiagnosticsButton.onClick = [this] {
        if (currentRenderModel.diagnosticsText.isNotEmpty()) {
            juce::SystemClipboard::copyTextToClipboard(currentRenderModel.diagnosticsText);
            copyDiagnosticsButton.setButtonText("Copied");
            juce::Timer::callAfterDelay(1200, [button = juce::Component::SafePointer<juce::TextButton>(&copyDiagnosticsButton)] {
                if (button != nullptr)
                    button->setButtonText("Copy");
            });
        }
    };
    copyDiagnosticsButton.setTooltip("Copy diagnostics text to the clipboard");
    addAndMakeVisible(copyDiagnosticsButton);

    diagnosticsEditor.setMultiLine(true, false);
    diagnosticsEditor.setReadOnly(true);
    diagnosticsEditor.setScrollbarsShown(true);
    diagnosticsEditor.setCaretVisible(false);
    diagnosticsEditor.setPopupMenuEnabled(true);
    diagnosticsEditor.setFont(juce::FontOptions().withHeight(12.0f));
    diagnosticsEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    diagnosticsEditor.setColour(juce::TextEditor::outlineColourId,    juce::Colours::transparentBlack);
    diagnosticsEditor.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    diagnosticsEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white.withAlpha(0.82f));
    diagnosticsEditor.setVisible(false);
    addAndMakeVisible(diagnosticsEditor);

    chainToggleButton.setButtonText("Hide Chain");
    chainToggleButton.onClick = [this] {
        chainCollapsed = !chainCollapsed;
        chainToggleButton.setButtonText(chainCollapsed ? "Show Chain" : "Hide Chain");
        resized();
        repaint();
    };
    addAndMakeVisible(chainToggleButton);

    refreshButton.setButtonText("Refresh");
    refreshButton.onClick = [this] {
        controller.selectFullChain();
        refreshFromController();
        applyPendingRenderModel();
    };
    addAndMakeVisible(refreshButton);

    juce::Timer::startTimerHz(kUiRefreshHz);
    refreshFromController();
    applyPendingRenderModel();
}

VXStudioAnalyserEditor::~VXStudioAnalyserEditor() {
    juce::Timer::stopTimer();
    setLookAndFeel(nullptr);
}

// ---------------------------------------------------------------------------
// refreshFromController — delegates all business logic to AnalyserController.
// The editor builds the AnalyserContext from the processor and passes it in.
// ---------------------------------------------------------------------------
void VXStudioAnalyserEditor::refreshFromController() {
    vxanalyser::AnalyserContext ctx;
    ctx.trackStableId    = processor.trackStableId();
    ctx.trackDisplayName = processor.trackDisplayName();
    ctx.analysisDomainId = processor.analysisDomainId();
    ctx.nowMs            = static_cast<std::uint64_t>(juce::Time::currentTimeMillis());
    ctx.isActive         = processor.isAnalyserActive();
    ctx.liveInputValid   = processor.liveInputSummaryValid();
    if (ctx.liveInputValid)
        ctx.liveInput = processor.liveInputSummary();

    controller.refresh(ctx, currentAverageTimeSeconds(), currentSpectrumSmoothingRadius());
    currentRenderModel = controller.model();

    const auto currentFingerprint = fingerprintChainRows(currentRenderModel.chainRows);
    if (currentFingerprint != lastObservedChainFingerprint && !controller.isFullChainSelected()) {
        controller.selectFullChain();
        controller.refresh(ctx, currentAverageTimeSeconds(), currentSpectrumSmoothingRadius());
        currentRenderModel = controller.model();
    }

    lastObservedChainFingerprint = fingerprintChainRows(currentRenderModel.chainRows);
}

void VXStudioAnalyserEditor::timerCallback() {
    refreshFromController();
    applyPendingRenderModel();
}

void VXStudioAnalyserEditor::paint(juce::Graphics& g) {
    const auto& theme = processor.theme();
    const auto bg     = colourFromRgb(theme.backgroundRgb);
    const auto panel  = colourFromRgb(theme.panelRgb);
    const auto accent = colourFromRgb(theme.accentRgb);
    const auto text   = colourFromRgb(theme.textRgb);

    g.fillAll(bg);

    auto local = getLocalBounds().toFloat();
    juce::ColourGradient wash(bg.brighter(0.10f), local.getTopLeft(),
                              accent.withAlpha(0.12f), local.getBottomRight(), false);
    g.setGradientFill(wash);
    g.fillRect(local);

    if (!chainCollapsed) {
        g.setColour(juce::Colours::black.withAlpha(0.18f));
        g.fillRoundedRectangle(chainBounds.toFloat().translated(0.0f, 8.0f), 22.0f);
    }
    g.fillRoundedRectangle(contentBounds.toFloat().translated(0.0f, 8.0f), 22.0f);

    g.setColour(panel.withAlpha(0.98f));
    if (!chainCollapsed)
        g.fillRoundedRectangle(chainBounds.toFloat(), 22.0f);
    g.fillRoundedRectangle(contentBounds.toFloat(), 22.0f);
    g.setColour(text.withAlpha(0.08f));
    if (!chainCollapsed)
        g.drawRoundedRectangle(chainBounds.toFloat(), 22.0f, 1.0f);
    g.drawRoundedRectangle(contentBounds.toFloat(), 22.0f, 1.0f);

    g.setColour(juce::Colour(0xff16161e));
    g.fillRect(juce::Rectangle<float>(local.getX(), local.getY(), local.getWidth(), 2.0f));

    g.setColour(accent.withAlpha(0.08f));
    g.fillRoundedRectangle(summaryBounds.toFloat().expanded(10.0f, 8.0f), 16.0f);
    g.setColour(text.withAlpha(0.06f));
    g.drawRoundedRectangle(summaryBounds.toFloat().expanded(10.0f, 8.0f), 16.0f, 1.0f);

    const auto plotFrame = plotBounds.toFloat();
    juce::ColourGradient plotGrad(panel.brighter(0.02f), plotFrame.getTopLeft(),
                                  juce::Colour(0xff0c1319), plotFrame.getBottomRight(), false);
    g.setGradientFill(plotGrad);
    g.fillRoundedRectangle(plotFrame, 18.0f);
    g.setColour(text.withAlpha(0.05f));
    g.drawRoundedRectangle(plotFrame, 18.0f, 1.0f);

    // Stage chain.
    if (!chainCollapsed) {
        const auto railHeader = chainBounds.toFloat().reduced(16.0f, 16.0f).removeFromTop(32.0f);
        g.setColour(text.withAlpha(0.62f));
        g.setFont(juce::FontOptions().withHeight(12.0f).withKerningFactor(0.1f));
        g.drawText("STAGE CHAIN", railHeader, juce::Justification::centredLeft, false);

        for (std::size_t i = 0; i < stageRowBounds.size(); ++i) {
            const int logicalIdx = i < stageRowLogicalIndices.size()
                ? stageRowLogicalIndices[i] : static_cast<int>(i);
            if (logicalIdx < 0 || logicalIdx >= static_cast<int>(currentRenderModel.chainRows.size()))
                continue;
            auto rowBounds = stageRowBounds[i].toFloat();
            const auto& row = currentRenderModel.chainRows[static_cast<std::size_t>(logicalIdx)];

            if (row.isTrackHeader) {
                const bool hasColour = row.trackColour.getAlpha() > 0
                                    && row.trackColour != juce::Colour(0);
                const auto headerAccent = hasColour
                    ? row.trackColour.withAlpha(0.85f)
                    : accent.withAlpha(0.55f);

                g.setColour(headerAccent);
                g.fillRoundedRectangle(rowBounds.removeFromLeft(3.0f).reduced(0.0f, 4.0f), 1.5f);

                g.setColour(hasColour ? headerAccent : text.withAlpha(0.52f));
                g.setFont(juce::FontOptions().withHeight(11.0f).withKerningFactor(0.10f).withStyle("Bold"));
                const auto labelBounds = rowBounds.reduced(6.0f, 0.0f);
                g.drawText(row.stageName.toUpperCase(), labelBounds.toNearestInt(), juce::Justification::centredLeft, false);

                const float lineY     = rowBounds.getCentreY();
                const float labelEnd  = rowBounds.getX() + 8.0f
                    + g.getCurrentFont().getStringWidthFloat(row.stageName.toUpperCase()) + 8.0f;
                g.setColour(hasColour ? headerAccent.withAlpha(0.25f) : text.withAlpha(0.10f));
                g.drawHorizontalLine(juce::roundToInt(lineY), labelEnd, rowBounds.getRight() - 4.0f);
                continue;
            }

            const auto rowFill = row.inactive
                ? juce::Colours::white.withAlpha(0.018f)
                : row.selected ? accent.withAlpha(0.18f)
                               : juce::Colours::white.withAlpha(0.035f);
            g.setColour(rowFill);
            g.fillRoundedRectangle(rowBounds, 12.0f);
            g.setColour(row.inactive ? text.withAlpha(0.05f)
                                     : row.selected ? accent.withAlpha(0.42f)
                                                    : text.withAlpha(0.10f));
            g.drawRoundedRectangle(rowBounds, 12.0f, 1.0f);

            const auto indicator = rowBounds.removeFromLeft(5.0f);
            const auto impactColour = row.typeLabel == "Dynamic" ? juce::Colour(0xff63d0ff)
                                     : row.typeLabel == "Tone"    ? juce::Colour(0xffffb15c)
                                     : row.typeLabel == "Sparse"  ? juce::Colour(0xffd9d38b)
                                     : row.typeLabel == "Waiting" ? juce::Colours::white.withAlpha(0.25f)
                                     : juce::Colour(0xff8cd9bf);
            g.setColour(row.inactive
                            ? juce::Colours::white.withAlpha(0.14f)
                            : impactColour.withAlpha(row.selected ? 0.90f : 0.55f));
            g.fillRoundedRectangle(indicator.reduced(0.0f, 7.0f), 2.0f);

            auto content = rowBounds.reduced(14.0f, 8.0f);
            auto top     = content.removeFromTop(20.0f);
            const float statusWidth = std::min(88.0f, top.getWidth() * 0.28f);
            g.setColour(text.withAlpha(row.inactive ? 0.45f : 0.95f));
            g.setFont(juce::FontOptions().withHeight(16.0f).withStyle("Bold"));
            g.drawFittedText(row.stageName,
                             top.removeFromLeft(top.getWidth() - statusWidth).toNearestInt(),
                             juce::Justification::centredLeft, 1);
            g.setColour(text.withAlpha(row.inactive ? 0.38f : 0.62f));
            g.setFont(juce::FontOptions().withHeight(12.0f));
            g.drawFittedText(row.stateText, top.toNearestInt(), juce::Justification::centredRight, 1);

            const auto secondLine = row.inactive ? juce::String("Inactive")
                                 : row.typeLabel == "Waiting"
                ? juce::String("Waiting for telemetry")
                : row.impactText + "  |  " + row.typeLabel
                    + (row.freqHint.isEmpty() ? "" : "  " + row.freqHint);
            g.setColour(text.withAlpha(row.inactive ? 0.34f : 0.68f));
            g.setFont(juce::FontOptions().withHeight(11.8f));
            g.drawFittedText(secondLine,
                             content.withTrimmedTop(2.0f).toNearestInt(),
                             juce::Justification::centredLeft, 2);
        }
    }

    if (diagnosticsExpanded) {
        g.setColour(panel.brighter(0.03f).withAlpha(0.96f));
        g.fillRoundedRectangle(diagnosticsBounds.toFloat(), 16.0f);
        g.setColour(text.withAlpha(0.08f));
        g.drawRoundedRectangle(diagnosticsBounds.toFloat(), 16.0f, 1.0f);
    }

    if (!currentRenderModel.valid) {
        const bool isBypassed = currentRenderModel.bypassed;
        auto plotRegion = plotBounds.toFloat().reduced(80.0f, 60.0f);
        if (plotRegion.getHeight() < 40.0f)
            return;
        auto upper = plotRegion.removeFromTop(plotRegion.getHeight() * 0.5f);
        g.setColour(text.withAlpha(0.84f));
        g.setFont(juce::FontOptions().withHeight(24.0f).withStyle("Bold"));

        juce::String headline, detail;
        if (isBypassed) {
            headline = "Analyser disabled";
            detail   = "Enable the plugin in the host to resume analysis.";
        } else if (processor.trackStableId() == 0) {
            headline = "Track identity unavailable";
            detail   = "Waiting for the host to provide a stable track ID. Play or re-open the project to trigger track property updates.";
        } else {
            headline = "No confirmed VX stages on this track";
            detail   = "Load VX Suite plugins on the same track, before the analyser. Only stages with a confirmed matching track ID are shown.";
        }

        g.drawFittedText(headline, upper.toNearestInt(), juce::Justification::centredBottom, 1);
        g.setFont(juce::FontOptions().withHeight(14.0f));
        g.setColour(text.withAlpha(0.60f));
        g.drawFittedText(detail, plotRegion.withTrimmedTop(12.0f).toNearestInt(),
                         juce::Justification::centredTop, 3);
        return;
    }

    auto plot = plotBounds.toFloat().reduced(60.0f, 28.0f);
    {
        g.setColour(text.withAlpha(0.06f));
        for (float db : { -72.0f, -66.0f, -60.0f, -54.0f, -48.0f, -42.0f, -36.0f, -30.0f, -24.0f }) {
            const float y = juce::jmap(db, kSpectrumMinDb, kSpectrumMaxDb, plot.getBottom(), plot.getY());
            g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
        }
        for (float hz : { 50.0f, 200.0f, 500.0f, 2000.0f, 5000.0f }) {
            const float x = xForFrequency(hz, plot);
            g.setColour(text.withAlpha(0.06f));
            g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
        }
        for (float hz : { 100.0f, 1000.0f, 10000.0f }) {
            const float x = xForFrequency(hz, plot);
            g.setColour(text.withAlpha(0.14f));
            g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
        }
        g.setColour(text.withAlpha(0.35f));
        g.drawHorizontalLine(juce::roundToInt(plot.getY()), plot.getX(), plot.getRight());

        auto dryStroke = makeTonePath(currentRenderModel.beforeToneDb, plot);
        auto wetStroke = makeTonePath(currentRenderModel.afterToneDb,  plot);

        juce::Path dryFill = dryStroke;
        dryFill.lineTo(plot.getRight(), plot.getBottom());
        dryFill.lineTo(plot.getX(),     plot.getBottom());
        dryFill.closeSubPath();
        g.setColour(juce::Colour(0xffb8c2cf).withAlpha(0.06f));
        g.fillPath(dryFill);

        juce::Path wetFill = wetStroke;
        wetFill.lineTo(plot.getRight(), plot.getBottom());
        wetFill.lineTo(plot.getX(),     plot.getBottom());
        wetFill.closeSubPath();
        g.setColour(accent.withAlpha(0.16f));
        g.fillPath(wetFill);

        const auto additiveColour    = juce::Colour(0xffa7df5a).withAlpha(0.18f);
        const auto subtractiveColour = juce::Colour(0xffffa15e).withAlpha(0.18f);
        for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins - 1; ++i) {
            const auto ia = static_cast<std::size_t>(i);
            const auto ib = static_cast<std::size_t>(i + 1);
            const float x1 = xForFrequency(bandCenterHz(i),     plot);
            const float x2 = xForFrequency(bandCenterHz(i + 1), plot);
            const float bY1 = juce::jmap(currentRenderModel.beforeToneDb[ia], kSpectrumMinDb, kSpectrumMaxDb, plot.getBottom(), plot.getY());
            const float bY2 = juce::jmap(currentRenderModel.beforeToneDb[ib], kSpectrumMinDb, kSpectrumMaxDb, plot.getBottom(), plot.getY());
            const float aY1 = juce::jmap(currentRenderModel.afterToneDb[ia],  kSpectrumMinDb, kSpectrumMaxDb, plot.getBottom(), plot.getY());
            const float aY2 = juce::jmap(currentRenderModel.afterToneDb[ib],  kSpectrumMinDb, kSpectrumMaxDb, plot.getBottom(), plot.getY());
            juce::Path diff;
            diff.startNewSubPath(x1, bY1);
            diff.lineTo(x2, bY2);
            diff.lineTo(x2, aY2);
            diff.lineTo(x1, aY1);
            diff.closeSubPath();
            const float avgDelta = 0.5f * ((currentRenderModel.afterToneDb[ia] - currentRenderModel.beforeToneDb[ia])
                                          + (currentRenderModel.afterToneDb[ib] - currentRenderModel.beforeToneDb[ib]));
            g.setColour(avgDelta >= 0.0f ? additiveColour : subtractiveColour);
            g.fillPath(diff);
        }

        g.setColour(juce::Colour(0xffb8c2cf).withAlpha(0.50f));
        g.strokePath(dryStroke, juce::PathStrokeType(1.6f));
        g.setColour(accent.withAlpha(0.96f));
        g.strokePath(wetStroke, juce::PathStrokeType(2.1f));

        const float markerX = xForFrequency(bandCenterHz(currentRenderModel.largestToneBand), plot);
        const float markerY = juce::jmap(
            currentRenderModel.afterToneDb[static_cast<std::size_t>(currentRenderModel.largestToneBand)],
            kSpectrumMinDb, kSpectrumMaxDb, plot.getBottom(), plot.getY());
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.fillEllipse(markerX - 5.0f, markerY - 5.0f, 10.0f, 10.0f);
        g.setColour(juce::Colour(0xffffd7a3));
        g.fillEllipse(markerX - 4.0f, markerY - 4.0f, 8.0f, 8.0f);

        g.setColour(text.withAlpha(0.52f));
        g.setFont(juce::FontOptions().withHeight(11.5f));
        for (float hz : { 20.0f, 50.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f }) {
            const float x = xForFrequency(hz, plot);
            g.drawVerticalLine(juce::roundToInt(x), plot.getBottom() + 1.0f, plot.getBottom() + 5.0f);
            g.drawText(formatFrequency(hz),
                       juce::Rectangle<float>(x - 30.0f, plot.getBottom() + 6.0f, 60.0f, 16.0f),
                       juce::Justification::centred, false);
        }
        for (float db : { -18.0f, -30.0f, -42.0f, -54.0f, -66.0f, -78.0f }) {
            const float y = juce::jmap(db, kSpectrumMinDb, kSpectrumMaxDb, plot.getBottom(), plot.getY());
            g.drawHorizontalLine(juce::roundToInt(y), plot.getX() - 5.0f, plot.getX());
            g.drawText(juce::String(db, 0) + " dB",
                       juce::Rectangle<float>(plot.getX() - 58.0f, y - 8.0f, 52.0f, 16.0f),
                       juce::Justification::centredRight, false);
        }
    }

    // Spectrum legend — drawn in the top-right corner of the plot area.
    if (currentRenderModel.valid) {
        const auto legendBase = plotBounds.toFloat().reduced(68.0f, 36.0f).getTopRight();
        const float legendX = legendBase.x - 190.0f;
        const float legendY = legendBase.y + 8.0f;
        g.setFont(juce::FontOptions().withHeight(11.0f));

        const std::array<std::pair<juce::Colour, const char*>, 4> items {{
            { juce::Colour(0xffb8c2cf).withAlpha(0.85f), "Dry baseline" },
            { accent.withAlpha(0.92f),                   "Wet output"   },
            { juce::Colour(0xffa7df5a).withAlpha(0.80f), "Added energy" },
            { juce::Colour(0xffffa15e).withAlpha(0.80f), "Reduced energy" }
        }};

        float ly = legendY;
        for (const auto& [col, label] : items) {
            g.setColour(col);
            g.fillRoundedRectangle(legendX, ly + 2.0f, 14.0f, 8.0f, 2.0f);
            g.setColour(text.withAlpha(0.62f));
            g.drawText(label, legendX + 18.0f, ly, 160.0f, 13.0f, juce::Justification::centredLeft, false);
            ly += 15.0f;
        }
    }

    g.setColour(text.withAlpha(0.45f));
    g.setFont(juce::FontOptions().withHeight(12.0f));
    g.drawFittedText("DSP v" + juce::String(processor.getProductIdentity().dspVersion.data())
                        + "   FW v" + juce::String(vxsuite::versions::framework.data())
                        + "    (c) Andrzej Marczewski 2026",
                     getLocalBounds().reduced(24, 18),
                     juce::Justification::bottomRight, 1);
}

void VXStudioAnalyserEditor::resized() {
    auto area   = getLocalBounds().reduced(20, 18);
    auto header = area.removeFromTop(116);
    auto topRow = header.removeFromTop(18);
    if (processor.getProductIdentity().hasHelpContent()) {
        helpButton.setBounds(topRow.removeFromRight(92));
        topRow.removeFromRight(12);
    }
    suiteLabel.setBounds(topRow);
    titleLabel.setBounds(header.removeFromTop(34));
    subtitleLabel.setBounds(header.removeFromTop(20));
    recordingLabel.setBounds(header.removeFromTop(18));
    statusLabel.setBounds(header.removeFromTop(20));

    area.removeFromTop(8);
    const int availableWidth  = area.getWidth();
    const int desiredChainW   = juce::jlimit(220, 340, availableWidth / 4);
    const int actualChainW    = chainCollapsed ? 0 : desiredChainW;
    chainBounds   = area.removeFromLeft(actualChainW);
    if (!chainCollapsed)
        area.removeFromLeft(14);
    contentBounds = area;

    auto chainArea = chainBounds.reduced(16, 16);
    constexpr int kRailHeaderH = 32;
    if (!chainCollapsed) {
        chainArea.removeFromTop(kRailHeaderH + 10);
        fullChainButton.setBounds(chainArea.removeFromTop(34));
        chainArea.removeFromTop(14);
        stageRowBounds.clear();
        stageRowLogicalIndices.clear();

        constexpr int kStageRowH  = 58;
        constexpr int kHeaderRowH = 24;
        constexpr int kRowSpacing = 10;

        int totalH = 0;
        for (const auto& row : currentRenderModel.chainRows)
            totalH += (row.isTrackHeader ? kHeaderRowH : kStageRowH) + kRowSpacing;
        maxChainScroll    = std::max(0, totalH - chainArea.getHeight());
        chainScrollOffset = std::min(chainScrollOffset, maxChainScroll);

        auto scrollableArea = chainArea.translated(0, -chainScrollOffset);
        for (std::size_t i = 0; i < currentRenderModel.chainRows.size(); ++i) {
            const int rowH = currentRenderModel.chainRows[i].isTrackHeader ? kHeaderRowH : kStageRowH;
            const auto rb  = scrollableArea.removeFromTop(rowH);
            if (rb.intersects(chainArea)) {
                stageRowBounds.push_back(rb);
                stageRowLogicalIndices.push_back(static_cast<int>(i));
            }
            scrollableArea.removeFromTop(kRowSpacing);
        }
    } else {
        fullChainButton.setBounds({});
        stageRowBounds.clear();
        stageRowLogicalIndices.clear();
        chainScrollOffset = 0;
        maxChainScroll    = 0;
    }

    auto contentArea = contentBounds.reduced(22, 18);
    const bool compact = contentArea.getWidth() < 840;
    selectionLabel.setBounds(contentArea.removeFromTop(compact ? 36 : 34));
    const int summaryH = compact ? 96 : 82;
    summaryBounds = contentArea.removeFromTop(summaryH);
    summaryLabel.setBounds(summaryBounds.reduced(0, 2));
    auto controlsArea = contentArea.removeFromTop(compact ? 72 : 34);
    if (compact) {
        auto r1 = controlsArea.removeFromTop(32);
        averageTimeLabel.setBounds(r1.removeFromLeft(86));
        averageTimeBox.setBounds(r1.removeFromLeft(146));
        r1.removeFromLeft(16);
        smoothingLabel.setBounds(r1.removeFromLeft(96));
        smoothingBox.setBounds(r1.removeFromLeft(146));
        controlsArea.removeFromTop(6);
        auto r2 = controlsArea.removeFromTop(32);
        chainToggleButton.setBounds(r2.removeFromLeft(130));
        r2.removeFromLeft(10);
        refreshButton.setBounds(r2.removeFromLeft(90));
    } else {
        auto r = controlsArea.removeFromTop(32);
        averageTimeLabel.setBounds(r.removeFromLeft(86));
        averageTimeBox.setBounds(r.removeFromLeft(144));
        r.removeFromLeft(16);
        smoothingLabel.setBounds(r.removeFromLeft(96));
        smoothingBox.setBounds(r.removeFromLeft(144));
        r.removeFromLeft(18);
        chainToggleButton.setBounds(r.removeFromLeft(130));
        r.removeFromLeft(10);
        refreshButton.setBounds(r.removeFromLeft(90));
    }
    contentArea.removeFromTop(8);
    const int diagH = diagnosticsExpanded
        ? juce::jlimit(136, 220, contentArea.getHeight() / 3)
        : 28;
    diagnosticsBounds = contentArea.removeFromBottom(diagH);
    auto diagnosticsHeader = diagnosticsBounds.removeFromTop(28);
    copyDiagnosticsButton.setBounds(diagnosticsHeader.removeFromRight(154));
    diagnosticsHeader.removeFromRight(10);
    diagnosticsToggleButton.setBounds(diagnosticsHeader.removeFromRight(122));
    if (diagnosticsExpanded)
        diagnosticsEditor.setBounds(diagnosticsBounds.reduced(8, 4));
    else
        diagnosticsBounds = {};
    contentArea.removeFromTop(8);
    plotBounds = contentArea.toNearestInt();
    applyTextFit();
}

void VXStudioAnalyserEditor::applyTextFit() {
    vxsuite::fitLabelFontToBounds(suiteLabel,       16.0f, 13.0f);
    vxsuite::fitLabelFontToBounds(titleLabel,       30.0f, 22.0f);
    vxsuite::fitLabelFontToBounds(subtitleLabel,    14.0f, 12.0f);
    vxsuite::fitLabelFontToBounds(recordingLabel,   13.0f, 11.0f);
    vxsuite::fitLabelFontToBounds(statusLabel,      12.5f, 11.0f);
    vxsuite::fitLabelFontToBounds(selectionLabel,   22.0f, 17.0f);
    vxsuite::fitLabelFontToBounds(summaryLabel,     13.5f, 11.5f);
    vxsuite::fitLabelFontToBounds(averageTimeLabel, 12.5f, 11.0f);
    vxsuite::fitLabelFontToBounds(smoothingLabel,   12.5f, 11.0f);
}

void VXStudioAnalyserEditor::mouseUp(const juce::MouseEvent& event) {
    const auto pos = event.getEventRelativeTo(this).position.toInt();
    for (int i = 0; i < static_cast<int>(stageRowBounds.size()); ++i) {
        if (!stageRowBounds[static_cast<std::size_t>(i)].contains(pos))
            continue;
        const int logIdx = i < static_cast<int>(stageRowLogicalIndices.size())
            ? stageRowLogicalIndices[static_cast<std::size_t>(i)] : i;
        if (logIdx < 0 || logIdx >= static_cast<int>(currentRenderModel.chainRows.size()))
            return;
        const auto& row = currentRenderModel.chainRows[static_cast<std::size_t>(logIdx)];
        if (row.isTrackHeader) {
            controller.selectTrack(row.trackStableId);
        } else if (!row.inactive && row.instanceId != 0) {
            const bool additive = event.mods.isCommandDown() || event.mods.isCtrlDown();
            controller.selectStage(row.instanceId, additive);
        }
        refreshFromController();
        applyPendingRenderModel();
        return;
    }
}

void VXStudioAnalyserEditor::mouseWheelMove(const juce::MouseEvent& event,
                                             const juce::MouseWheelDetails& wheel) {
    if (!chainBounds.contains(event.getEventRelativeTo(this).position.toInt()))
        return;
    constexpr int kStep = 30;
    chainScrollOffset = juce::jlimit(0, maxChainScroll,
                                     chainScrollOffset + (wheel.deltaY > 0 ? -kStep : kStep));
    resized();
    repaint();
}

void VXStudioAnalyserEditor::visibilityChanged() {
    if (isVisible()) {
        juce::Timer::startTimerHz(kUiRefreshHz);
        refreshFromController();
        applyPendingRenderModel();
    } else {
        juce::Timer::stopTimer();
    }
}

void VXStudioAnalyserEditor::applyPendingRenderModel() {
    recordingLabel.setText(signalQualityLabel(processor.getSignalQualitySnapshot()),
                           juce::dontSendNotification);

    statusLabel.setText(currentRenderModel.statusText, juce::dontSendNotification);
    copyDiagnosticsButton.setEnabled(currentRenderModel.diagnosticsText.isNotEmpty());
    if (diagnosticsExpanded)
        diagnosticsEditor.setText(currentRenderModel.diagnosticsText, false);
    selectionLabel.setText(currentRenderModel.selectionTitle, juce::dontSendNotification);
    summaryLabel.setText(currentRenderModel.summaryLines[0] + "\n"
                             + currentRenderModel.summaryLines[1] + "\n"
                             + currentRenderModel.summaryLines[2],
                         juce::dontSendNotification);
    rebuildStageButtons();
    repaint();
}

void VXStudioAnalyserEditor::rebuildStageButtons() {
    const auto& theme = processor.theme();
    fullChainButton.setColour(juce::TextButton::buttonColourId,
                              controller.isFullChainSelected()
                                  ? colourFromRgb(theme.accentRgb, 0.40f)
                                  : juce::Colours::white.withAlpha(0.06f));

    // Rebuild row layout if chain contents changed (fingerprint check).
    const std::uint64_t fp = fingerprintChainRows(currentRenderModel.chainRows);
    if (fp != chainRowsFingerprint) {
        chainRowsFingerprint = fp;
        resized();
    }
}

// ---------------------------------------------------------------------------
// Debug helpers
// ---------------------------------------------------------------------------

void VXStudioAnalyserEditor::debugRefreshNow() {
    refreshFromController();
    applyPendingRenderModel();
}

int VXStudioAnalyserEditor::debugVisibleChainRowCount() const noexcept {
    int count = 0;
    for (const auto& row : currentRenderModel.chainRows)
        if (!row.isTrackHeader) ++count;
    return count;
}

juce::String VXStudioAnalyserEditor::debugChainRowStateText(const int index) const {
    int stageIdx = 0;
    for (const auto& row : currentRenderModel.chainRows) {
        if (row.isTrackHeader) continue;
        if (stageIdx == index) return row.stateText;
        ++stageIdx;
    }
    return {};
}

// ---------------------------------------------------------------------------
// Rendering helpers
// ---------------------------------------------------------------------------

float VXStudioAnalyserEditor::currentAverageTimeSeconds() const noexcept {
    return static_cast<float>(averageTimeMsFromIndex(averageTimeIndex.load())) / 1000.0f;
}

int VXStudioAnalyserEditor::currentSpectrumSmoothingRadius() const noexcept {
    const float binsPerOct = static_cast<float>(vxsuite::analysis::kSummarySpectrumBins)
        / std::log2(20000.0f / 20.0f);
    float oct = 0.0f;
    switch (juce::jlimit(0, static_cast<int>(kSmoothingOptions.size()) - 1, smoothingIndex.load())) {
        case 0: oct = 0.0f;        break;
        case 1: oct = 1.0f / 12.0f; break;
        case 2: oct = 1.0f / 9.0f;  break;
        case 3: oct = 1.0f / 6.0f;  break;
        case 4: oct = 1.0f / 3.0f;  break;
        case 5: oct = 1.0f / 2.0f;  break;
        case 6: oct = 1.0f;          break;
        default: oct = 1.0f / 6.0f; break;
    }
    return juce::jlimit(0,
                        static_cast<int>(vxsuite::analysis::kSummarySpectrumBins / 8),
                        juce::roundToInt(binsPerOct * oct));
}

juce::Path VXStudioAnalyserEditor::makeTonePath(
    const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& valuesDb,
    const juce::Rectangle<float> bounds) const {
    juce::Path path;
    const int n = static_cast<int>(valuesDb.size());
    if (n < 2) return path;

    std::array<juce::Point<float>, vxsuite::analysis::kSummarySpectrumBins> pts {};
    for (int i = 0; i < n; ++i) {
        pts[static_cast<std::size_t>(i)] = {
            xForFrequency(bandCenterHz(i), bounds),
            juce::jmap(valuesDb[static_cast<std::size_t>(i)],
                       kSpectrumMinDb, kSpectrumMaxDb, bounds.getBottom(), bounds.getY())
        };
    }
    path.startNewSubPath(pts[0]);
    for (int i = 1; i < n; ++i)
        path.lineTo(pts[static_cast<std::size_t>(i)]);
    return path;
}

juce::Colour VXStudioAnalyserEditor::colourFromRgb(const std::array<float, 3>& rgb,
                                                     const float alpha) const noexcept {
    return juce::Colour::fromFloatRGBA(rgb[0], rgb[1], rgb[2], alpha);
}
