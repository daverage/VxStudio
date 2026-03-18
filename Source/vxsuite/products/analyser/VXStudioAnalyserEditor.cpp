#include "VXStudioAnalyserEditor.h"

#include "../../framework/VxSuiteBlockSmoothing.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace {

constexpr std::uint64_t kStaleThresholdMs = 300;
constexpr int kUiRefreshHz = 30;
constexpr int kBackendRefreshMs = 50;
constexpr float kToneMinDb = -18.0f;
constexpr float kToneMaxDb = 18.0f;
constexpr float kDynamicsMinDb = -60.0f;
constexpr float kDynamicsMaxDb = 0.0f;
constexpr float kLowHighBoundaryHz = 200.0f;
constexpr float kMidHighBoundaryHz = 2000.0f;
constexpr float kToneAttackSeconds = 0.180f;
constexpr float kToneReleaseSeconds = 0.420f;
constexpr float kDeltaDisplaySeconds = 0.120f;
constexpr float kDynamicsSmoothingSeconds = 0.160f;
constexpr float kSummarySmoothingSeconds = 0.250f;

juce::String labelFromChars(const auto& chars) {
    return juce::String(chars.data());
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

float signedAverage(const auto& values) noexcept {
    double sum = 0.0;
    for (const auto value : values)
        sum += static_cast<double>(value);
    return static_cast<float>(sum / static_cast<double>(std::max<std::size_t>(1, values.size())));
}

juce::String signedDb(const float value, const int decimals = 1) {
    return juce::String(value >= 0.0f ? "+" : "") + juce::String(value, decimals) + " dB";
}

float toDb(const float linear, const float floorDb = -100.0f) noexcept {
    return juce::Decibels::gainToDecibels(std::max(1.0e-6f, linear), floorDb);
}

juce::String impactLabel(const float score) {
    if (score >= 2.0f)
        return "Strong";
    if (score >= 0.75f)
        return "Moderate";
    return "Low";
}

juce::String classLabel(const float spectral, const float dynamic, const float stereo) {
    const float maxValue = std::max({ spectral, dynamic, stereo });
    int nearCount = 0;
    nearCount += spectral >= maxValue * 0.8f ? 1 : 0;
    nearCount += dynamic >= maxValue * 0.8f ? 1 : 0;
    nearCount += stereo >= maxValue * 0.8f ? 1 : 0;
    if (nearCount >= 2)
        return "Mixed";
    if (maxValue == dynamic)
        return "Dynamic";
    if (maxValue == stereo)
        return "Spatial";
    return "Tone";
}

float smoothToward(const float current,
                   const float target,
                   const float attackSeconds,
                   const float releaseSeconds) noexcept {
    return vxsuite::smoothBlockToward(current, target, 15.0, 1, attackSeconds, releaseSeconds);
}

float smoothScalar(const float current, const float target, const float timeSeconds) noexcept {
    return vxsuite::smoothBlockValue(current, target, 15.0, 1, timeSeconds);
}

float smoothDisplayValue(const float current, const float target, const float timeSeconds) noexcept {
    return vxsuite::smoothBlockValue(current, target, 30.0, 1, timeSeconds);
}

std::array<juce::String, 4> buildToneSummary(const juce::String& title,
                                             const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& deltaDb,
                                             const int largestToneBand,
                                             const float lowAvg,
                                             const float midAvg,
                                             const float highAvg) {
    const float largestDelta = deltaDb[static_cast<std::size_t>(largestToneBand)];

    juce::String shape = "Flat attenuation";
    const float absLargest = std::abs(largestDelta);
    const float tilt = highAvg - lowAvg;
    if (absLargest < 0.75f && std::abs(lowAvg) < 0.5f && std::abs(midAvg) < 0.5f && std::abs(highAvg) < 0.5f) {
        shape = "No material tonal change";
    } else if (std::abs(tilt) > 2.0f) {
        shape = tilt > 0.0f ? "High tilt" : "Low tilt";
    } else if (std::abs(midAvg) > std::abs(lowAvg) + 1.0f && std::abs(midAvg) > std::abs(highAvg) + 1.0f) {
        shape = "Mid-focused change";
    } else {
        shape = largestDelta >= 0.0f ? "Broad boost" : "Flat attenuation";
    }

    return {
        title,
        "Largest change: " + signedDb(largestDelta) + " @ " + formatFrequency(bandCenterHz(largestToneBand)),
        "Low: " + signedDb(lowAvg) + "   Mid: " + signedDb(midAvg) + "   High: " + signedDb(highAvg),
        "Shape: " + shape
    };
}

std::array<juce::String, 4> buildDynamicsSummary(const juce::String& title,
                                                 const float peakDeltaDb,
                                                 const float rmsDeltaDb,
                                                 const float crestDeltaDb,
                                                 const float transientDelta) {
    juce::String transientText = "no material change";
    if (transientDelta < -0.8f)
        transientText = "reduced";
    else if (transientDelta > 0.8f)
        transientText = "enhanced";

    juce::String behaviour = "Uniform level reduction";
    if (std::abs(peakDeltaDb) < 0.5f && std::abs(rmsDeltaDb) < 0.5f)
        behaviour = "Minimal dynamic change";
    else if ((peakDeltaDb - rmsDeltaDb) < -1.2f)
        behaviour = "Compression-like peak control";
    else if ((peakDeltaDb - rmsDeltaDb) > 1.2f)
        behaviour = "Peak emphasis";

    return {
        title,
        "Peak: " + signedDb(peakDeltaDb) + "   RMS: " + signedDb(rmsDeltaDb),
        "Crest: " + signedDb(crestDeltaDb) + "   Transients: " + transientText,
        "Behaviour: " + behaviour
    };
}

} // namespace

VXStudioAnalyserEditor::VXStudioAnalyserEditor(VXStudioAnalyserAudioProcessor& owner)
    : juce::AudioProcessorEditor(&owner),
      processor(owner) {
    setResizable(true, false);
    setResizeLimits(1080, 720, 1680, 1100);
    setSize(1260, 820);

    titleLabel.setText("VX Studio Analyser", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions().withHeight(30.0f).withStyle("Bold"));
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("Stage-aware explanation of what each processor changed.", juce::dontSendNotification);
    subtitleLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.74f));
    subtitleLabel.setFont(juce::FontOptions().withHeight(14.0f));
    addAndMakeVisible(subtitleLabel);

    statusLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.70f));
    statusLabel.setFont(juce::FontOptions().withHeight(12.5f));
    addAndMakeVisible(statusLabel);

    selectionLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.94f));
    selectionLabel.setFont(juce::FontOptions().withHeight(22.0f).withStyle("Bold"));
    addAndMakeVisible(selectionLabel);

    summaryLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.92f));
    summaryLabel.setFont(juce::FontOptions().withHeight(14.0f));
    summaryLabel.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(summaryLabel);

    fullChainButton.setButtonText("Full Chain");
    fullChainButton.onClick = [this] { selectFullChain(); };
    addAndMakeVisible(fullChainButton);

    toneTabButton.setButtonText("Tone");
    toneTabButton.onClick = [this] {
        currentTabIndex.store(static_cast<int>(Tab::tone));
        updateTabButtons();
        refreshRenderModel();
        applyPendingRenderModel();
    };
    addAndMakeVisible(toneTabButton);

    dynamicsTabButton.setButtonText("Dynamics");
    dynamicsTabButton.onClick = [this] {
        currentTabIndex.store(static_cast<int>(Tab::dynamics));
        updateTabButtons();
        refreshRenderModel();
        applyPendingRenderModel();
    };
    addAndMakeVisible(dynamicsTabButton);

    diagnosticsToggleButton.setButtonText("Diagnostics ▸");
    diagnosticsToggleButton.onClick = [this] {
        diagnosticsExpanded = !diagnosticsExpanded;
        diagnosticsToggleButton.setButtonText(diagnosticsExpanded ? "Diagnostics ▾" : "Diagnostics ▸");
        resized();
        repaint();
    };
    addAndMakeVisible(diagnosticsToggleButton);

    updateTabButtons();
    juce::Timer::startTimerHz(kUiRefreshHz);
    juce::HighResolutionTimer::startTimer(kBackendRefreshMs);
    refreshRenderModel();
    applyPendingRenderModel();
}

VXStudioAnalyserEditor::~VXStudioAnalyserEditor() {
    juce::Timer::stopTimer();
    juce::HighResolutionTimer::stopTimer();
}

void VXStudioAnalyserEditor::paint(juce::Graphics& g) {
    const auto& theme = processor.theme();
    const auto bg = colourFromRgb(theme.backgroundRgb);
    const auto panel = colourFromRgb(theme.panelRgb);
    const auto accent = colourFromRgb(theme.accentRgb);
    const auto text = colourFromRgb(theme.textRgb);

    g.fillAll(bg);

    auto local = getLocalBounds().toFloat();
    juce::ColourGradient wash(bg.brighter(0.10f), local.getTopLeft(),
                              accent.withAlpha(0.12f), local.getBottomRight(), false);
    g.setGradientFill(wash);
    g.fillRect(local);

    g.setColour(panel.withAlpha(0.96f));
    g.fillRoundedRectangle(chainBounds.toFloat(), 22.0f);
    g.fillRoundedRectangle(contentBounds.toFloat(), 22.0f);
    g.setColour(text.withAlpha(0.08f));
    g.drawRoundedRectangle(chainBounds.toFloat(), 22.0f, 1.0f);
    g.drawRoundedRectangle(contentBounds.toFloat(), 22.0f, 1.0f);

    if (diagnosticsExpanded) {
        g.setColour(panel.brighter(0.03f).withAlpha(0.96f));
        g.fillRoundedRectangle(diagnosticsBounds.toFloat(), 16.0f);
        g.setColour(text.withAlpha(0.08f));
        g.drawRoundedRectangle(diagnosticsBounds.toFloat(), 16.0f, 1.0f);
    }

    if (!currentRenderModel.valid) {
        g.setColour(text.withAlpha(0.84f));
        g.setFont(juce::FontOptions().withHeight(24.0f).withStyle("Bold"));
        g.drawFittedText("Waiting for live signal", plotBounds.reduced(40, 80), juce::Justification::centred, 1);
        g.setFont(juce::FontOptions().withHeight(14.0f));
        g.setColour(text.withAlpha(0.60f));
        g.drawFittedText("Insert VX Studio Analyser last in the chain. It will show the dry baseline even before other VX stages join.",
                         plotBounds.reduced(80, 130),
                         juce::Justification::centred,
                         2);
        return;
    }

    auto plot = plotBounds.toFloat();
    if (static_cast<Tab>(currentTabIndex.load()) == Tab::tone) {
        const auto zeroY = juce::jmap(0.0f, kToneMinDb, kToneMaxDb, plot.getBottom(), plot.getY());
        g.setColour(text.withAlpha(0.07f));
        for (float db : { 18.0f, 9.0f, 0.0f, -9.0f, -18.0f }) {
            const float y = juce::jmap(db, kToneMinDb, kToneMaxDb, plot.getBottom(), plot.getY());
            g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
        }

        for (float hz : { 50.0f, 100.0f, 1000.0f, 10000.0f }) {
            const float x = xForFrequency(hz, plot);
            g.setColour(text.withAlpha(0.07f));
            g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
        }

        for (float hz : { kLowHighBoundaryHz, kMidHighBoundaryHz }) {
            const float x = xForFrequency(hz, plot);
            g.setColour(text.withAlpha(0.12f));
            g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
        }

        g.setColour(text.withAlpha(0.22f));
        g.drawHorizontalLine(juce::roundToInt(zeroY), plot.getX(), plot.getRight());

        auto deltaPath = makeTonePath(currentRenderModel.deltaToneDb, plot);
        juce::Path deltaFill(deltaPath);
        deltaFill.lineTo(plot.getRight(), zeroY);
        deltaFill.lineTo(plot.getX(), zeroY);
        deltaFill.closeSubPath();

        g.setColour(juce::Colour(0xffffa84f).withAlpha(0.18f));
        g.fillPath(deltaFill);
        g.setColour(juce::Colour(0xffb8c2cf).withAlpha(0.34f));
        g.strokePath(makeTonePath(currentRenderModel.beforeToneDb, plot), juce::PathStrokeType(1.1f));
        g.setColour(accent.withAlpha(0.85f));
        g.strokePath(makeTonePath(currentRenderModel.afterToneDb, plot), juce::PathStrokeType(1.35f));
        g.setColour(juce::Colour(0xffffa84f).withAlpha(0.96f));
        g.strokePath(deltaPath, juce::PathStrokeType(2.2f));

        const float markerX = xForFrequency(bandCenterHz(currentRenderModel.largestToneBand), plot);
        const float markerY = juce::jmap(currentRenderModel.deltaToneDb[static_cast<std::size_t>(currentRenderModel.largestToneBand)],
                                         kToneMinDb,
                                         kToneMaxDb,
                                         plot.getBottom(),
                                         plot.getY());
        g.setColour(juce::Colour(0xffffd7a3));
        g.fillEllipse(markerX - 4.0f, markerY - 4.0f, 8.0f, 8.0f);

        g.setColour(text.withAlpha(0.78f));
        g.setFont(juce::FontOptions().withHeight(12.0f));
        for (float hz : { 50.0f, 100.0f, 1000.0f, 10000.0f }) {
            const float x = xForFrequency(hz, plot);
            g.drawText(formatFrequency(hz),
                       juce::Rectangle<float>(x - 28.0f, plot.getBottom() - 20.0f, 56.0f, 16.0f),
                       juce::Justification::centred,
                       false);
        }
        for (float db : { 18.0f, 9.0f, 0.0f, -9.0f, -18.0f }) {
            const float y = juce::jmap(db, kToneMinDb, kToneMaxDb, plot.getBottom(), plot.getY());
            g.drawText(juce::String(db, 0) + " dB",
                       juce::Rectangle<float>(plot.getX() - 2.0f, y - 8.0f, 52.0f, 16.0f),
                       juce::Justification::left,
                       false);
        }
    } else {
        g.setColour(text.withAlpha(0.07f));
        for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f }) {
            const float y = juce::jmap(db, kDynamicsMinDb, kDynamicsMaxDb, plot.getBottom(), plot.getY());
            g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
        }

        g.setColour(juce::Colour(0xffb8c2cf).withAlpha(0.44f));
        g.strokePath(makeDynamicsPath(currentRenderModel.beforeDynamicsDb, plot), juce::PathStrokeType(1.4f));
        g.setColour(accent.withAlpha(0.94f));
        g.strokePath(makeDynamicsPath(currentRenderModel.afterDynamicsDb, plot), juce::PathStrokeType(2.0f));

        g.setColour(text.withAlpha(0.78f));
        g.setFont(juce::FontOptions().withHeight(12.0f));
        g.drawText("Past", juce::Rectangle<float>(plot.getX(), plot.getBottom() - 20.0f, 60.0f, 16.0f), juce::Justification::left, false);
        g.drawText("Now", juce::Rectangle<float>(plot.getRight() - 60.0f, plot.getBottom() - 20.0f, 60.0f, 16.0f), juce::Justification::right, false);
        for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f }) {
            const float y = juce::jmap(db, kDynamicsMinDb, kDynamicsMaxDb, plot.getBottom(), plot.getY());
            g.drawText(juce::String(db, 0) + " dBFS",
                       juce::Rectangle<float>(plot.getX() - 2.0f, y - 8.0f, 62.0f, 16.0f),
                       juce::Justification::left,
                       false);
        }
    }

    if (diagnosticsExpanded) {
        auto diag = diagnosticsBounds.reduced(16, 12);
        g.setColour(text.withAlpha(0.82f));
        g.setFont(juce::FontOptions().withHeight(13.0f));
        g.drawFittedText(currentRenderModel.diagnosticsText, diag, juce::Justification::topLeft, 12);
    }
}

void VXStudioAnalyserEditor::resized() {
    auto area = getLocalBounds().reduced(20, 18);
    auto header = area.removeFromTop(74);
    titleLabel.setBounds(header.removeFromTop(34));
    subtitleLabel.setBounds(header.removeFromTop(22));
    statusLabel.setBounds(header.removeFromTop(18));

    area.removeFromTop(8);
    chainBounds = area.removeFromLeft(300);
    area.removeFromLeft(14);
    contentBounds = area;

    auto chainArea = chainBounds.reduced(16, 16);
    fullChainButton.setBounds(chainArea.removeFromTop(30));
    chainArea.removeFromTop(12);
    for (auto& button : stageButtons) {
        if (!button->isVisible())
            continue;
        button->setBounds(chainArea.removeFromTop(42));
        chainArea.removeFromTop(6);
    }

    auto contentArea = contentBounds.reduced(22, 18);
    selectionLabel.setBounds(contentArea.removeFromTop(32));
    summaryBounds = contentArea.removeFromTop(84);
    summaryLabel.setBounds(summaryBounds);
    contentArea.removeFromTop(8);
    tabsBounds = contentArea.removeFromTop(34);
    auto tabs = tabsBounds;
    toneTabButton.setBounds(tabs.removeFromLeft(98));
    tabs.removeFromLeft(8);
    dynamicsTabButton.setBounds(tabs.removeFromLeft(110));
    contentArea.removeFromTop(8);
    diagnosticsBounds = contentArea.removeFromBottom(diagnosticsExpanded ? 136 : 28);
    diagnosticsToggleButton.setBounds(diagnosticsBounds.removeFromTop(28));
    if (!diagnosticsExpanded)
        diagnosticsBounds = {};
    contentArea.removeFromTop(8);
    plotBounds = contentArea.toNearestInt();
}

void VXStudioAnalyserEditor::timerCallback() {
    applyPendingRenderModel();
}

void VXStudioAnalyserEditor::hiResTimerCallback() {
    refreshRenderModel();
}

void VXStudioAnalyserEditor::refreshRenderModel() {
    const auto nowMs = static_cast<std::uint64_t>(juce::Time::currentTimeMillis());
    std::vector<StageEntry> externalStages;
    std::optional<StageEntry> analyserStage;

    for (int slotIndex = 0; slotIndex < vxsuite::analysis::StageRegistry::instance().maxSlots(); ++slotIndex) {
        vxsuite::analysis::StageView stage;
        if (!vxsuite::analysis::StageRegistry::instance().readStage(slotIndex, stage))
            continue;
        if (!stage.active || stage.analysisDomainId != processor.analysisDomainId())
            continue;
        if (labelFromChars(stage.telemetry.identity.pluginFamily) != "VXSuite")
            continue;
        if ((nowMs - stage.telemetry.state.timestampMs) > kStaleThresholdMs)
            continue;

        StageEntry entry;
        entry.view = stage;
        entry.stageId = labelFromChars(stage.telemetry.identity.stageId);
        entry.stageName = labelFromChars(stage.telemetry.identity.stageName);
        entry.stateText = stage.telemetry.state.isBypassed ? "Bypassed" : "Active";
        float spectralDeltaSum = 0.0f;
        for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
            spectralDeltaSum += std::abs(toDb(stage.telemetry.outputSummary.spectrum[static_cast<std::size_t>(i)])
                                         - toDb(stage.telemetry.inputSummary.spectrum[static_cast<std::size_t>(i)]));
        }
        entry.spectralChange = spectralDeltaSum / static_cast<float>(vxsuite::analysis::kSummarySpectrumBins);
        entry.dynamicChange = std::abs(toDb(stage.telemetry.outputSummary.rms, -120.0f)
                                       - toDb(stage.telemetry.inputSummary.rms, -120.0f));
        entry.stereoChange = std::abs(stage.telemetry.outputSummary.stereoWidth - stage.telemetry.inputSummary.stereoWidth)
                           + std::abs(stage.telemetry.outputSummary.correlation - stage.telemetry.inputSummary.correlation);
        entry.impactScore = 0.45f * entry.spectralChange + 0.45f * entry.dynamicChange + 0.10f * entry.stereoChange;
        entry.impactText = impactLabel(entry.impactScore);
        entry.classText = classLabel(entry.spectralChange, entry.dynamicChange, entry.stereoChange);

        if (entry.stageId == processor.stageIdString()) {
            analyserStage = entry;
            continue;
        }

        if (stage.telemetry.state.isSilent)
            continue;
        externalStages.push_back(std::move(entry));
    }

    std::sort(externalStages.begin(), externalStages.end(), [](const auto& a, const auto& b) {
        return a.view.telemetry.identity.localOrderId < b.view.telemetry.identity.localOrderId;
    });

    int selectedIndexValue = selectedStageIndex.load();
    bool fullChain = fullChainSelected.load();
    if (selectedIndexValue >= static_cast<int>(externalStages.size())) {
        selectedIndexValue = -1;
        selectedStageIndex.store(-1);
        fullChain = true;
        fullChainSelected.store(true);
    }

    RenderModel model;
    model.generation = ++renderGeneration;
    model.statusText =
        "Domain " + juce::String(static_cast<juce::int64>(processor.analysisDomainId()))
        + " | live stages: " + juce::String(static_cast<int>(externalStages.size()))
        + " | selection: "
        + ((fullChain || selectedIndexValue < 0)
               ? juce::String("Full Chain")
               : externalStages[static_cast<std::size_t>(selectedIndexValue)].stageName);

    model.chainRows.reserve(externalStages.size());
    for (int index = 0; index < static_cast<int>(externalStages.size()); ++index) {
        const auto& stage = externalStages[static_cast<std::size_t>(index)];
        model.chainRows.push_back({
            stage.stageName,
            stage.stateText,
            stage.impactText,
            stage.classText,
            !fullChain && index == selectedIndexValue
        });
    }

    vxsuite::analysis::AnalysisSummary before {};
    vxsuite::analysis::AnalysisSummary after {};
    juce::String selectionKey = "empty";
    juce::String scopeLabel;

    if (!externalStages.empty()) {
        if (fullChain || selectedIndexValue < 0) {
            before = externalStages.front().view.telemetry.inputSummary;
            after = externalStages.back().view.telemetry.outputSummary;
            scopeLabel = "Full Chain";
            selectionKey = "full";
        } else {
            const auto& stage = externalStages[static_cast<std::size_t>(selectedIndexValue)];
            before = stage.view.telemetry.inputSummary;
            after = stage.view.telemetry.outputSummary;
            scopeLabel = stage.stageName;
            selectionKey = "stage:" + stage.stageId + ":" + juce::String(selectedIndexValue);
        }
        model.valid = true;
    } else if (analyserStage.has_value()) {
        before = analyserStage->view.telemetry.inputSummary;
        after = analyserStage->view.telemetry.outputSummary;
        scopeLabel = "Full Chain";
        selectionKey = "dry-only";
        model.valid = true;
    }

    if (model.valid) {
        const bool resetSmoothing = !backendState.initialized || backendState.selectionKey != selectionKey;
        backendState.selectionKey = selectionKey;
        backendState.initialized = true;

        for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
            const float beforeTarget = std::max(1.0e-6f, before.spectrum[static_cast<std::size_t>(i)]);
            const float afterTarget = std::max(1.0e-6f, after.spectrum[static_cast<std::size_t>(i)]);
            if (resetSmoothing) {
                backendState.beforeToneLinear[static_cast<std::size_t>(i)] = beforeTarget;
                backendState.afterToneLinear[static_cast<std::size_t>(i)] = afterTarget;
            } else {
                backendState.beforeToneLinear[static_cast<std::size_t>(i)] =
                    smoothToward(backendState.beforeToneLinear[static_cast<std::size_t>(i)],
                                 beforeTarget,
                                 kToneAttackSeconds,
                                 kToneReleaseSeconds);
                backendState.afterToneLinear[static_cast<std::size_t>(i)] =
                    smoothToward(backendState.afterToneLinear[static_cast<std::size_t>(i)],
                                 afterTarget,
                                 kToneAttackSeconds,
                                 kToneReleaseSeconds);
            }
        }

        float beforeMeanDb = 0.0f;
        float afterMeanDb = 0.0f;
        for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
            beforeMeanDb += toDb(backendState.beforeToneLinear[static_cast<std::size_t>(i)]);
            afterMeanDb += toDb(backendState.afterToneLinear[static_cast<std::size_t>(i)]);
        }
        beforeMeanDb /= static_cast<float>(vxsuite::analysis::kSummarySpectrumBins);
        afterMeanDb /= static_cast<float>(vxsuite::analysis::kSummarySpectrumBins);
        const float referenceDb = 0.5f * (beforeMeanDb + afterMeanDb);

        float lowSum = 0.0f;
        float midSum = 0.0f;
        float highSum = 0.0f;
        int lowCount = 0;
        int midCount = 0;
        int highCount = 0;
        float largestDelta = 0.0f;
        int largestBand = 0;

        for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
            const float beforeDb = juce::jlimit(kToneMinDb, kToneMaxDb,
                                                toDb(backendState.beforeToneLinear[static_cast<std::size_t>(i)]) - referenceDb);
            const float afterDb = juce::jlimit(kToneMinDb, kToneMaxDb,
                                               toDb(backendState.afterToneLinear[static_cast<std::size_t>(i)]) - referenceDb);
            const float deltaTarget = juce::jlimit(kToneMinDb,
                                                   kToneMaxDb,
                                                   toDb(backendState.afterToneLinear[static_cast<std::size_t>(i)])
                                                       - toDb(backendState.beforeToneLinear[static_cast<std::size_t>(i)]));
            if (resetSmoothing) {
                backendState.deltaToneDb[static_cast<std::size_t>(i)] = deltaTarget;
            } else {
                backendState.deltaToneDb[static_cast<std::size_t>(i)] =
                    smoothDisplayValue(backendState.deltaToneDb[static_cast<std::size_t>(i)],
                                       deltaTarget,
                                       kDeltaDisplaySeconds);
            }

            model.beforeToneDb[static_cast<std::size_t>(i)] = beforeDb;
            model.afterToneDb[static_cast<std::size_t>(i)] = afterDb;
            model.deltaToneDb[static_cast<std::size_t>(i)] = backendState.deltaToneDb[static_cast<std::size_t>(i)];

            const float hz = bandCenterHz(i);
            if (hz < kLowHighBoundaryHz) {
                lowSum += model.deltaToneDb[static_cast<std::size_t>(i)];
                ++lowCount;
            } else if (hz < kMidHighBoundaryHz) {
                midSum += model.deltaToneDb[static_cast<std::size_t>(i)];
                ++midCount;
            } else {
                highSum += model.deltaToneDb[static_cast<std::size_t>(i)];
                ++highCount;
            }

            if (std::abs(model.deltaToneDb[static_cast<std::size_t>(i)]) > std::abs(largestDelta)) {
                largestDelta = model.deltaToneDb[static_cast<std::size_t>(i)];
                largestBand = i;
            }
        }

        model.largestToneBand = largestBand;
        const float lowAvg = lowCount > 0 ? lowSum / static_cast<float>(lowCount) : 0.0f;
        const float midAvg = midCount > 0 ? midSum / static_cast<float>(midCount) : 0.0f;
        const float highAvg = highCount > 0 ? highSum / static_cast<float>(highCount) : 0.0f;

        backendState.largestToneDeltaDb = resetSmoothing ? largestDelta
                                                         : smoothScalar(backendState.largestToneDeltaDb, largestDelta, kSummarySmoothingSeconds);
        backendState.lowToneDeltaDb = resetSmoothing ? lowAvg
                                                     : smoothScalar(backendState.lowToneDeltaDb, lowAvg, kSummarySmoothingSeconds);
        backendState.midToneDeltaDb = resetSmoothing ? midAvg
                                                     : smoothScalar(backendState.midToneDeltaDb, midAvg, kSummarySmoothingSeconds);
        backendState.highToneDeltaDb = resetSmoothing ? highAvg
                                                      : smoothScalar(backendState.highToneDeltaDb, highAvg, kSummarySmoothingSeconds);

        for (int i = 0; i < vxsuite::analysis::kSummaryEnvelopeBins; ++i) {
            const float beforeTarget = std::max(1.0e-6f, before.envelope[static_cast<std::size_t>(i)]);
            const float afterTarget = std::max(1.0e-6f, after.envelope[static_cast<std::size_t>(i)]);
            if (resetSmoothing) {
                backendState.beforeDynamicsLinear[static_cast<std::size_t>(i)] = beforeTarget;
                backendState.afterDynamicsLinear[static_cast<std::size_t>(i)] = afterTarget;
            } else {
                backendState.beforeDynamicsLinear[static_cast<std::size_t>(i)] =
                    smoothScalar(backendState.beforeDynamicsLinear[static_cast<std::size_t>(i)],
                                 beforeTarget,
                                 kDynamicsSmoothingSeconds);
                backendState.afterDynamicsLinear[static_cast<std::size_t>(i)] =
                    smoothScalar(backendState.afterDynamicsLinear[static_cast<std::size_t>(i)],
                                 afterTarget,
                                 kDynamicsSmoothingSeconds);
            }

            model.beforeDynamicsDb[static_cast<std::size_t>(i)] =
                juce::jlimit(kDynamicsMinDb, kDynamicsMaxDb, toDb(backendState.beforeDynamicsLinear[static_cast<std::size_t>(i)], -120.0f));
            model.afterDynamicsDb[static_cast<std::size_t>(i)] =
                juce::jlimit(kDynamicsMinDb, kDynamicsMaxDb, toDb(backendState.afterDynamicsLinear[static_cast<std::size_t>(i)], -120.0f));
        }

        const float peakDeltaTarget = toDb(after.peak, -120.0f) - toDb(before.peak, -120.0f);
        const float rmsDeltaTarget = toDb(after.rms, -120.0f) - toDb(before.rms, -120.0f);
        const float crestDeltaTarget = toDb(std::max(1.0e-6f, after.crestFactor), -120.0f)
            - toDb(std::max(1.0e-6f, before.crestFactor), -120.0f);
        const float transientDeltaTarget = after.transientScore - before.transientScore;
        backendState.peakDeltaDb = resetSmoothing ? peakDeltaTarget
                                                  : smoothScalar(backendState.peakDeltaDb, peakDeltaTarget, kSummarySmoothingSeconds);
        backendState.rmsDeltaDb = resetSmoothing ? rmsDeltaTarget
                                                 : smoothScalar(backendState.rmsDeltaDb, rmsDeltaTarget, kSummarySmoothingSeconds);
        backendState.crestDeltaDb = resetSmoothing ? crestDeltaTarget
                                                   : smoothScalar(backendState.crestDeltaDb, crestDeltaTarget, kSummarySmoothingSeconds);
        backendState.transientDelta = resetSmoothing ? transientDeltaTarget
                                                     : smoothScalar(backendState.transientDelta, transientDeltaTarget, kSummarySmoothingSeconds);

        const auto currentTab = static_cast<Tab>(currentTabIndex.load());
        model.selectionTitle = scopeLabel;
        model.summaryLines = currentTab == Tab::tone
            ? buildToneSummary("Tone -> " + scopeLabel,
                               model.deltaToneDb,
                               model.largestToneBand,
                               backendState.lowToneDeltaDb,
                               backendState.midToneDeltaDb,
                               backendState.highToneDeltaDb)
            : buildDynamicsSummary("Dynamics -> " + scopeLabel,
                                   backendState.peakDeltaDb,
                                   backendState.rmsDeltaDb,
                                   backendState.crestDeltaDb,
                                   backendState.transientDelta);
        model.diagnosticsText =
            "Domain: " + juce::String(static_cast<juce::int64>(processor.analysisDomainId()))
            + "\nStage count: " + juce::String(static_cast<int>(externalStages.size()))
            + "\nFreshness: <= " + juce::String(static_cast<int>(kStaleThresholdMs)) + " ms"
            + "\nOrder confidence: Deterministic via domain + localOrderId"
            + "\nCapabilities: Tone + Dynamics Tier 1"
            + "\nSelection key: " + selectionKey;
    }

    {
        const juce::SpinLock::ScopedLockType lock(renderModelLock);
        pendingRenderModel = std::move(model);
    }
    pendingGeneration.store(renderGeneration);
}

void VXStudioAnalyserEditor::applyPendingRenderModel() {
    const auto latestGeneration = pendingGeneration.load();
    if (latestGeneration == 0 || latestGeneration == appliedGeneration)
        return;

    {
        const juce::SpinLock::ScopedLockType lock(renderModelLock);
        currentRenderModel = pendingRenderModel;
    }
    appliedGeneration = latestGeneration;

    statusLabel.setText(currentRenderModel.statusText, juce::dontSendNotification);
    selectionLabel.setText(currentRenderModel.selectionTitle, juce::dontSendNotification);
    summaryLabel.setText(currentRenderModel.summaryLines[0] + "\n"
                             + currentRenderModel.summaryLines[1] + "\n"
                             + currentRenderModel.summaryLines[2] + "\n"
                             + currentRenderModel.summaryLines[3],
                         juce::dontSendNotification);
    rebuildStageButtons();
    repaint();
}

void VXStudioAnalyserEditor::rebuildStageButtons() {
    const int needed = static_cast<int>(currentRenderModel.chainRows.size());
    while (static_cast<int>(stageButtons.size()) < needed) {
        auto button = std::make_unique<juce::TextButton>();
        const int index = static_cast<int>(stageButtons.size());
        button->onClick = [this, index] { selectStage(index); };
        addAndMakeVisible(*button);
        stageButtons.push_back(std::move(button));
    }

    for (auto& button : stageButtons)
        button->setVisible(false);

    for (int index = 0; index < needed; ++index) {
        auto& button = stageButtons[static_cast<std::size_t>(index)];
        const auto& row = currentRenderModel.chainRows[static_cast<std::size_t>(index)];
        button->setButtonText(row.stageName + "   " + row.stateText + "   " + row.impactText + " " + row.classText);
        button->setColour(juce::TextButton::buttonColourId,
                          row.selected ? colourFromRgb(processor.theme().accentRgb, 0.40f)
                                       : juce::Colours::white.withAlpha(0.06f));
        button->setColour(juce::TextButton::textColourOffId, juce::Colours::white.withAlpha(0.92f));
        button->setVisible(true);
    }

    fullChainButton.setColour(juce::TextButton::buttonColourId,
                              fullChainSelected.load() ? colourFromRgb(processor.theme().accentRgb, 0.40f)
                                                       : juce::Colours::white.withAlpha(0.06f));
    resized();
}

void VXStudioAnalyserEditor::selectStage(const int index) {
    if (index < 0 || index >= static_cast<int>(currentRenderModel.chainRows.size()))
        return;
    selectedStageIndex.store(index);
    fullChainSelected.store(false);
    refreshRenderModel();
    applyPendingRenderModel();
}

void VXStudioAnalyserEditor::selectFullChain() {
    selectedStageIndex.store(-1);
    fullChainSelected.store(true);
    refreshRenderModel();
    applyPendingRenderModel();
}

void VXStudioAnalyserEditor::updateTabButtons() {
    const bool toneSelected = static_cast<Tab>(currentTabIndex.load()) == Tab::tone;
    toneTabButton.setColour(juce::TextButton::buttonColourId,
                            toneSelected ? colourFromRgb(processor.theme().accentRgb, 0.44f)
                                         : juce::Colours::white.withAlpha(0.04f));
    dynamicsTabButton.setColour(juce::TextButton::buttonColourId,
                                toneSelected ? juce::Colours::white.withAlpha(0.04f)
                                             : colourFromRgb(processor.theme().accentRgb, 0.44f));
}

juce::Path VXStudioAnalyserEditor::makeTonePath(
    const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& valuesDb,
    const juce::Rectangle<float> bounds) const {
    juce::Path path;
    for (int i = 0; i < static_cast<int>(valuesDb.size()); ++i) {
        const float x = xForFrequency(bandCenterHz(i), bounds);
        const float y = juce::jmap(valuesDb[static_cast<std::size_t>(i)], kToneMinDb, kToneMaxDb, bounds.getBottom(), bounds.getY());
        if (i == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }
    return path;
}

juce::Path VXStudioAnalyserEditor::makeDynamicsPath(
    const std::array<float, vxsuite::analysis::kSummaryEnvelopeBins>& valuesDb,
    const juce::Rectangle<float> bounds) const {
    juce::Path path;
    for (int i = 0; i < static_cast<int>(valuesDb.size()); ++i) {
        const float x = bounds.getX() + (bounds.getWidth() * static_cast<float>(i))
            / static_cast<float>(std::max(1, static_cast<int>(valuesDb.size()) - 1));
        const float y = juce::jmap(valuesDb[static_cast<std::size_t>(i)],
                                   kDynamicsMinDb,
                                   kDynamicsMaxDb,
                                   bounds.getBottom(),
                                   bounds.getY());
        if (i == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }
    return path;
}

juce::Colour VXStudioAnalyserEditor::colourFromRgb(const std::array<float, 3>& rgb, const float alpha) const noexcept {
    return juce::Colour::fromFloatRGBA(rgb[0], rgb[1], rgb[2], alpha);
}
