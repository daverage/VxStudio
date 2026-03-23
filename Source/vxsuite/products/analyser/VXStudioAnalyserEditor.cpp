#include "VXStudioAnalyserEditor.h"

#include "../../framework/VxSuiteBlockSmoothing.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace {

constexpr std::uint64_t kStaleThresholdMs = 1500;
constexpr std::uint64_t kFallbackStageThresholdMs = 5000;
constexpr std::uint64_t kSidebarSnapshotHoldMs = 3500;
constexpr int kUiRefreshHz = 24;
constexpr float kSpectrumMinDb = -78.0f;
constexpr float kSpectrumMaxDb = -18.0f;
constexpr float kDynamicsMinDb = -60.0f;
constexpr float kDynamicsMaxDb = 0.0f;
constexpr float kLowHighBoundaryHz = 200.0f;
constexpr float kMidHighBoundaryHz = 2000.0f;
constexpr float kDisplaySlopeDbPerOct = 4.5f;
constexpr float kDisplaySlopeReferenceHz = 1000.0f;

constexpr int kMaxSpectrumHistoryFrames = 300;
constexpr std::array<int, 9> kAverageTimeOptionsMs { 100, 250, 500, 1000, 1500, 2000, 3000, 5000, 10000 };
constexpr std::array<const char*, 7> kSmoothingOptions {
    "Off", "1/12 OCT", "1/9 OCT", "1/6 OCT", "1/3 OCT", "1/2 OCT", "1 OCT"
};

int averageTimeMsFromIndex(const int index) noexcept {
    const int clamped = juce::jlimit(0, static_cast<int>(kAverageTimeOptionsMs.size()) - 1, index);
    return kAverageTimeOptionsMs[static_cast<std::size_t>(clamped)];
}

juce::String labelFromChars(const auto& chars) {
    return juce::String(chars.data());
}

juce::String canonicalStageKey(juce::String text) {
    auto normalized = text.trim().toLowerCase();
    normalized = normalized.removeCharacters(" _-()[]{}");
    normalized = normalized.replace("vxsuite", "");
    normalized = normalized.replace("vxstudio", "");
    if (normalized.startsWith("vx") && normalized.length() > 2)
        normalized = normalized.substring(2);
    return normalized;
}

juce::String displayStageName(const juce::String& rawName) {
    auto name = rawName.trim();
    if (name.startsWithIgnoreCase("VX Studio "))
        name = name.fromFirstOccurrenceOf("VX Studio ", false, false);
    else if (name.startsWithIgnoreCase("VX"))
        name = name.substring(2);

    name = name.trimStart();
    if (name.isEmpty())
        return rawName;
    return name;
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

float applyDisplaySlope(const float valueDb, const float hz) noexcept {
    const float octavesFromReference = std::log2(std::max(20.0f, hz) / kDisplaySlopeReferenceHz);
    return valueDb + kDisplaySlopeDbPerOct * octavesFromReference;
}

bool hasMeaningfulBandEnergy(const float beforeLinear, const float afterLinear) noexcept {
    constexpr float kMeaningfulBandFloorDb = -72.0f;
    const float maxDb = std::max(toDb(beforeLinear, -120.0f), toDb(afterLinear, -120.0f));
    return maxDb > kMeaningfulBandFloorDb;
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
    return vxsuite::smoothBlockToward(current, target, kUiRefreshHz, 1, attackSeconds, releaseSeconds);
}

float smoothScalar(const float current, const float target, const float timeSeconds) noexcept {
    return vxsuite::smoothBlockValue(current, target, kUiRefreshHz, 1, timeSeconds);
}

float smoothDisplayValue(const float current, const float target, const float timeSeconds) noexcept {
    return vxsuite::smoothBlockValue(current, target, kUiRefreshHz, 1, timeSeconds);
}

template <typename ArrayType>
ArrayType smoothNeighbourBins(const ArrayType& values, const int radius) {
    if (radius <= 0)
        return values;
    ArrayType smoothed {};
    for (int i = 0; i < static_cast<int>(values.size()); ++i) {
        float weightedSum = 0.0f;
        float weightTotal = 0.0f;
        for (int offset = -radius; offset <= radius; ++offset) {
            const int index = juce::jlimit(0, static_cast<int>(values.size()) - 1, i + offset);
            const float weight = static_cast<float>(radius + 1 - std::abs(offset));
            weightedSum += values[static_cast<std::size_t>(index)] * weight;
            weightTotal += weight;
        }
        smoothed[static_cast<std::size_t>(i)] = weightedSum / std::max(1.0f, weightTotal);
    }
    return smoothed;
}

struct SparseToneClassification {
    bool sparse = false;
    int activeBands = 0;
    float peakDominance = 0.0f;
    float topFourDominance = 0.0f;
    std::vector<int> significantBands;
};

SparseToneClassification classifySparseTone(
    const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& beforeLinear,
    const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& afterLinear,
    const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& deltaDb) {
    SparseToneClassification out;

    float maxEnergy = 0.0f;
    std::array<float, vxsuite::analysis::kSummarySpectrumBins> bandEnergy {};
    std::array<float, vxsuite::analysis::kSummarySpectrumBins> absDelta {};
    for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
        const float energy = std::max(beforeLinear[static_cast<std::size_t>(i)],
                                      afterLinear[static_cast<std::size_t>(i)]);
        bandEnergy[static_cast<std::size_t>(i)] = energy;
        absDelta[static_cast<std::size_t>(i)] = std::abs(deltaDb[static_cast<std::size_t>(i)]);
        maxEnergy = std::max(maxEnergy, energy);
    }

    if (maxEnergy <= 1.0e-6f)
        return out;

    double totalEnergy = 0.0;
    std::array<float, 4> topEnergy { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
        const float energy = bandEnergy[static_cast<std::size_t>(i)];
        totalEnergy += energy;
        if (energy >= maxEnergy * 0.15f)
            ++out.activeBands;

        for (int slot = 0; slot < 4; ++slot) {
            if (energy > topEnergy[static_cast<std::size_t>(slot)]) {
                for (int move = 3; move > slot; --move)
                    topEnergy[static_cast<std::size_t>(move)] = topEnergy[static_cast<std::size_t>(move - 1)];
                topEnergy[static_cast<std::size_t>(slot)] = energy;
                break;
            }
        }
    }

    const double topFourEnergy = static_cast<double>(topEnergy[0] + topEnergy[1] + topEnergy[2] + topEnergy[3]);
    out.peakDominance = static_cast<float>(topEnergy[0] / std::max(1.0e-12, totalEnergy));
    out.topFourDominance = static_cast<float>(topFourEnergy / std::max(1.0, totalEnergy));

    for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
        const bool strongEnergy = bandEnergy[static_cast<std::size_t>(i)] >= maxEnergy * 0.18f;
        const bool meaningfulDelta = absDelta[static_cast<std::size_t>(i)] >= 1.0f;
        if (strongEnergy || meaningfulDelta)
            out.significantBands.push_back(i);
    }

    out.sparse = out.activeBands <= 18 || out.peakDominance >= 0.22f || out.topFourDominance >= 0.60f;
    if (!out.sparse)
        out.significantBands.clear();
    return out;
}

juce::String describeSparseBands(const std::vector<int>& bands,
                                 const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& deltaDb) {
    if (bands.empty())
        return "Sparse spectral change detected";

    juce::StringArray parts;
    const int count = std::min(3, static_cast<int>(bands.size()));
    for (int i = 0; i < count; ++i) {
        const int band = bands[static_cast<std::size_t>(i)];
        parts.add(formatFrequency(bandCenterHz(band)) + " " + signedDb(deltaDb[static_cast<std::size_t>(band)]));
    }
    return parts.joinIntoString("   ");
}

std::array<juce::String, 4> buildToneSummary(const juce::String& title,
                                             const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& deltaDb,
                                             const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& beforeLinear,
                                             const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& afterLinear,
                                             const int largestToneBand,
                                             const bool sparseTone,
                                             const std::vector<int>& sparseToneBands,
                                             const float dryRmsDb,
                                             const float wetRmsDb,
                                             const float lowAvg,
                                             const float midAvg,
                                             const float highAvg) {
    const float largestDelta = deltaDb[static_cast<std::size_t>(largestToneBand)];
    juce::ignoreUnused(lowAvg, midAvg, highAvg);

    if (sparseTone) {
        return {
            title,
            "Dry RMS " + juce::String(dryRmsDb, 1) + " dB   Wet RMS " + juce::String(wetRmsDb, 1) + " dB",
            "Primary: " + signedDb(largestDelta) + " @ " + formatFrequency(bandCenterHz(largestToneBand)),
            describeSparseBands(sparseToneBands, deltaDb) + "   Sparse / narrowband spectrum"
        };
    }

    std::array<int, 3> strongestBands { 0, 0, 0 };
    std::array<float, 3> strongestMagnitudes { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
        if (! hasMeaningfulBandEnergy(beforeLinear[static_cast<std::size_t>(i)],
                                      afterLinear[static_cast<std::size_t>(i)]))
            continue;
        const float magnitude = std::abs(deltaDb[static_cast<std::size_t>(i)]);
        for (int slot = 0; slot < 3; ++slot) {
            if (magnitude > strongestMagnitudes[static_cast<std::size_t>(slot)]) {
                for (int move = 2; move > slot; --move) {
                    strongestMagnitudes[static_cast<std::size_t>(move)] = strongestMagnitudes[static_cast<std::size_t>(move - 1)];
                    strongestBands[static_cast<std::size_t>(move)] = strongestBands[static_cast<std::size_t>(move - 1)];
                }
                strongestMagnitudes[static_cast<std::size_t>(slot)] = magnitude;
                strongestBands[static_cast<std::size_t>(slot)] = i;
                break;
            }
        }
    }

    juce::StringArray dominantBands;
    for (int i = 0; i < 3; ++i) {
        if (strongestMagnitudes[static_cast<std::size_t>(i)] < 0.75f)
            continue;
        const int band = strongestBands[static_cast<std::size_t>(i)];
        dominantBands.add(formatFrequency(bandCenterHz(band)) + " " + signedDb(deltaDb[static_cast<std::size_t>(band)]));
    }
    const auto dominantText = dominantBands.isEmpty() ? juce::String("No strong band deltas")
                                                      : dominantBands.joinIntoString("   ");

    return {
        title,
        "Dry RMS " + juce::String(dryRmsDb, 1) + " dB   Wet RMS " + juce::String(wetRmsDb, 1) + " dB",
        "Largest: " + signedDb(largestDelta) + " @ " + formatFrequency(bandCenterHz(largestToneBand)),
        dominantText
    };
}

std::array<juce::String, 4> buildDynamicsSummary(const juce::String& title,
                                                 const float peakDeltaDb,
                                                 const float rmsDeltaDb,
                                                 const float crestDeltaDb,
                                                 const float transientDelta) {
    juce::String transientText = transientDelta < -0.8f ? "Down"
                               : transientDelta > 0.8f  ? "Up"
                               : "Stable";

    juce::String behaviour = "Broad level reduction";
    if (std::abs(peakDeltaDb) < 0.5f && std::abs(rmsDeltaDb) < 0.5f)
        behaviour = "Minimal change";
    else if ((peakDeltaDb - rmsDeltaDb) < -1.2f)
        behaviour = "Peak control";
    else if (std::abs(crestDeltaDb) > 1.5f && crestDeltaDb < 0.0f)
        behaviour = "Gentle compression";
    else if ((peakDeltaDb - rmsDeltaDb) > 1.2f)
        behaviour = "Transient reduction";

    return {
        title,
        "Peak " + signedDb(peakDeltaDb) + "   RMS " + signedDb(rmsDeltaDb),
        "Crest " + signedDb(crestDeltaDb) + "   Transients " + transientText,
        "Behaviour: " + behaviour
    };
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

    statusLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.70f));
    statusLabel.setFont(juce::FontOptions().withHeight(12.5f));
    statusLabel.setMinimumHorizontalScale(0.68f);
    addAndMakeVisible(statusLabel);

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
        refreshRenderModel();
        applyPendingRenderModel();
    };
    addAndMakeVisible(averageTimeBox);

    for (int i = 0; i < static_cast<int>(kSmoothingOptions.size()); ++i)
        smoothingBox.addItem(kSmoothingOptions[static_cast<std::size_t>(i)], i + 1);
    smoothingBox.setSelectedId(4, juce::dontSendNotification);
    smoothingBox.onChange = [this] {
        smoothingIndex.store(std::max(0, smoothingBox.getSelectedItemIndex()));
        refreshRenderModel();
        applyPendingRenderModel();
    };
    addAndMakeVisible(smoothingBox);

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
    toneTabButton.setVisible(false);
    dynamicsTabButton.setVisible(false);

    diagnosticsToggleButton.setButtonText("Diagnostics >");
    diagnosticsToggleButton.onClick = [this] {
        diagnosticsExpanded = !diagnosticsExpanded;
        diagnosticsToggleButton.setButtonText(diagnosticsExpanded ? "Diagnostics v" : "Diagnostics >");
        resized();
        repaint();
    };
    addAndMakeVisible(diagnosticsToggleButton);

    chainToggleButton.setButtonText("Hide Chain");
    chainToggleButton.onClick = [this] {
        chainCollapsed = !chainCollapsed;
        chainToggleButton.setButtonText(chainCollapsed ? "Show Chain" : "Hide Chain");
        resized();
        repaint();
    };
    addAndMakeVisible(chainToggleButton);

    updateTabButtons();
    juce::Timer::startTimerHz(kUiRefreshHz);
    refreshRenderModel();
    applyPendingRenderModel();
}

VXStudioAnalyserEditor::~VXStudioAnalyserEditor() {
    juce::Timer::stopTimer();
    setLookAndFeel(nullptr);
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

    const auto plotFrame = plotBounds.toFloat().expanded(0.0f, 0.0f);
    juce::ColourGradient plotGradient(panel.brighter(0.02f), plotFrame.getTopLeft(),
                                      juce::Colour(0xff0c1319), plotFrame.getBottomRight(), false);
    g.setGradientFill(plotGradient);
    g.fillRoundedRectangle(plotFrame, 18.0f);
    g.setColour(text.withAlpha(0.05f));
    g.drawRoundedRectangle(plotFrame, 18.0f, 1.0f);

    if (!chainCollapsed) {
        const auto railHeader = chainBounds.toFloat().reduced(16.0f, 16.0f).removeFromTop(32.0f);
        g.setColour(text.withAlpha(0.62f));
        g.setFont(juce::FontOptions().withHeight(12.0f).withKerningFactor(0.1f));
        g.drawText("STAGE CHAIN", railHeader, juce::Justification::centredLeft, false);

        for (std::size_t i = 0; i < stageRowBounds.size() && i < currentRenderModel.chainRows.size(); ++i) {
            auto rowBounds = stageRowBounds[i].toFloat();
            const auto& row = currentRenderModel.chainRows[i];
            const auto rowFill = row.selected ? accent.withAlpha(0.18f) : juce::Colours::white.withAlpha(0.035f);
            g.setColour(rowFill);
            g.fillRoundedRectangle(rowBounds, 12.0f);
            g.setColour(row.selected ? accent.withAlpha(0.42f) : text.withAlpha(0.10f));
            g.drawRoundedRectangle(rowBounds, 12.0f, 1.0f);

            const auto indicator = rowBounds.removeFromLeft(5.0f);
            const auto impactColour = row.typeLabel == "Dynamic" ? juce::Colour(0xff63d0ff)
                                     : row.typeLabel == "Tone"    ? juce::Colour(0xffffb15c)
                                     : row.typeLabel == "Sparse"  ? juce::Colour(0xffd9d38b)
                                     : row.typeLabel == "Waiting" ? juce::Colours::white.withAlpha(0.25f)
                                     : juce::Colour(0xff8cd9bf);
            g.setColour(impactColour.withAlpha(row.selected ? 0.90f : 0.55f));
            g.fillRoundedRectangle(indicator.reduced(0.0f, 7.0f), 2.0f);

            auto content = rowBounds.reduced(14.0f, 8.0f);
            auto top = content.removeFromTop(20.0f);
            const float statusWidth = std::min(88.0f, top.getWidth() * 0.28f);
            g.setColour(text.withAlpha(0.95f));
            g.setFont(juce::FontOptions().withHeight(16.0f).withStyle("Bold"));
            g.drawFittedText(row.stageName,
                             top.removeFromLeft(top.getWidth() - statusWidth).toNearestInt(),
                             juce::Justification::centredLeft,
                             1);
            g.setColour(text.withAlpha(0.62f));
            g.setFont(juce::FontOptions().withHeight(12.0f));
            g.drawFittedText(row.stateText, top.toNearestInt(), juce::Justification::centredRight, 1);

            const auto secondLine = row.typeLabel == "Waiting"
                ? juce::String("Waiting for telemetry")
                : row.impactText + "  |  " + row.typeLabel
                    + (row.freqHint.isEmpty() ? "" : "  " + row.freqHint);
            g.setColour(text.withAlpha(0.68f));
            g.setFont(juce::FontOptions().withHeight(11.8f));
            g.drawFittedText(secondLine,
                             content.withTrimmedTop(2.0f).toNearestInt(),
                             juce::Justification::centredLeft,
                             2);
        }
    }

    if (diagnosticsExpanded) {
        g.setColour(panel.brighter(0.03f).withAlpha(0.96f));
        g.fillRoundedRectangle(diagnosticsBounds.toFloat(), 16.0f);
        g.setColour(text.withAlpha(0.08f));
        g.drawRoundedRectangle(diagnosticsBounds.toFloat(), 16.0f, 1.0f);
    }

    if (!currentRenderModel.valid) {
        g.setColour(text.withAlpha(0.84f));
        g.setFont(juce::FontOptions().withHeight(24.0f).withStyle("Bold"));
        const bool isBypassed = currentRenderModel.bypassed;
        g.drawFittedText(isBypassed ? "Analyser is bypassed" : "Waiting for live signal",
                         plotBounds.reduced(40, 80), juce::Justification::centred, 1);
        g.setFont(juce::FontOptions().withHeight(14.0f));
        g.setColour(text.withAlpha(0.60f));
        g.drawFittedText(isBypassed
                             ? "Enable the plugin in the host to resume analysis."
                             : "Insert VX Studio Analyser last in the chain. It will show the dry baseline even before other VX stages join.",
                         plotBounds.reduced(80, 130),
                         juce::Justification::centred,
                         2);
        return;
    }

    auto plot = plotBounds.toFloat().reduced(56.0f, 20.0f);
    {

        // Minor grid lines
        g.setColour(text.withAlpha(0.06f));
        for (float db : { -72.0f, -66.0f, -60.0f, -54.0f, -48.0f, -42.0f, -36.0f, -30.0f, -24.0f }) {
            const float y = juce::jmap(db, kSpectrumMinDb, kSpectrumMaxDb, plot.getBottom(), plot.getY());
            g.drawHorizontalLine(juce::roundToInt(y), plot.getX(), plot.getRight());
        }

        // Minor frequency lines
        for (float hz : { 50.0f, 200.0f, 500.0f, 2000.0f, 5000.0f }) {
            const float x = xForFrequency(hz, plot);
            g.setColour(text.withAlpha(0.06f));
            g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
        }

        // Key frequency lines — slightly stronger
        for (float hz : { 100.0f, 1000.0f, 10000.0f }) {
            const float x = xForFrequency(hz, plot);
            g.setColour(text.withAlpha(0.14f));
            g.drawVerticalLine(juce::roundToInt(x), plot.getY(), plot.getBottom());
        }

        // Reference top line
        g.setColour(text.withAlpha(0.35f));
        g.drawHorizontalLine(juce::roundToInt(plot.getY()), plot.getX(), plot.getRight());

        auto dryStroke = makeTonePath(currentRenderModel.beforeToneDb, plot);
        auto wetStroke = makeTonePath(currentRenderModel.afterToneDb, plot);

        juce::Path dryFill = dryStroke;
        dryFill.lineTo(plot.getRight(), plot.getBottom());
        dryFill.lineTo(plot.getX(), plot.getBottom());
        dryFill.closeSubPath();
        g.setColour(juce::Colour(0xffb8c2cf).withAlpha(0.06f));
        g.fillPath(dryFill);

        juce::Path wetFill = wetStroke;
        wetFill.lineTo(plot.getRight(), plot.getBottom());
        wetFill.lineTo(plot.getX(), plot.getBottom());
        wetFill.closeSubPath();
        g.setColour(accent.withAlpha(0.16f));
        g.fillPath(wetFill);

        const auto additiveColour = juce::Colour(0xffa7df5a).withAlpha(0.18f);
        const auto subtractiveColour = juce::Colour(0xffffa15e).withAlpha(0.18f);
        for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins - 1; ++i) {
            const auto indexA = static_cast<std::size_t>(i);
            const auto indexB = static_cast<std::size_t>(i + 1);
            const float x1 = xForFrequency(bandCenterHz(i), plot);
            const float x2 = xForFrequency(bandCenterHz(i + 1), plot);
            const float beforeY1 = juce::jmap(currentRenderModel.beforeToneDb[indexA], kSpectrumMinDb, kSpectrumMaxDb, plot.getBottom(), plot.getY());
            const float beforeY2 = juce::jmap(currentRenderModel.beforeToneDb[indexB], kSpectrumMinDb, kSpectrumMaxDb, plot.getBottom(), plot.getY());
            const float afterY1 = juce::jmap(currentRenderModel.afterToneDb[indexA], kSpectrumMinDb, kSpectrumMaxDb, plot.getBottom(), plot.getY());
            const float afterY2 = juce::jmap(currentRenderModel.afterToneDb[indexB], kSpectrumMinDb, kSpectrumMaxDb, plot.getBottom(), plot.getY());

            juce::Path diffBand;
            diffBand.startNewSubPath(x1, beforeY1);
            diffBand.lineTo(x2, beforeY2);
            diffBand.lineTo(x2, afterY2);
            diffBand.lineTo(x1, afterY1);
            diffBand.closeSubPath();

            const float avgDelta = 0.5f * ((currentRenderModel.afterToneDb[indexA] - currentRenderModel.beforeToneDb[indexA])
                                         + (currentRenderModel.afterToneDb[indexB] - currentRenderModel.beforeToneDb[indexB]));
            g.setColour(avgDelta >= 0.0f ? additiveColour : subtractiveColour);
            g.fillPath(diffBand);
        }

        g.setColour(juce::Colour(0xffb8c2cf).withAlpha(0.50f));
        g.strokePath(dryStroke, juce::PathStrokeType(1.6f));
        g.setColour(accent.withAlpha(0.96f));
        g.strokePath(wetStroke, juce::PathStrokeType(2.1f));

        const float markerX = xForFrequency(bandCenterHz(currentRenderModel.largestToneBand), plot);
        const float markerY = juce::jmap(currentRenderModel.afterToneDb[static_cast<std::size_t>(currentRenderModel.largestToneBand)],
                                         kSpectrumMinDb,
                                         kSpectrumMaxDb,
                                         plot.getBottom(),
                                         plot.getY());
        g.setColour(juce::Colour(0xffffd7a3));
        g.fillEllipse(markerX - 4.0f, markerY - 4.0f, 8.0f, 8.0f);

        g.setColour(text.withAlpha(0.78f));
        g.setFont(juce::FontOptions().withHeight(12.0f));
        for (float hz : { 20.0f, 50.0f, 100.0f, 1000.0f, 10000.0f, 20000.0f }) {
            const float x = xForFrequency(hz, plot);
            g.drawText(formatFrequency(hz),
                       juce::Rectangle<float>(x - 30.0f, plot.getBottom() + 6.0f, 60.0f, 16.0f),
                       juce::Justification::centred,
                       false);
        }
        for (float db : { -18.0f, -30.0f, -42.0f, -54.0f, -66.0f, -78.0f }) {
            const float y = juce::jmap(db, kSpectrumMinDb, kSpectrumMaxDb, plot.getBottom(), plot.getY());
            g.drawText(juce::String(db, 0) + " dB",
                       juce::Rectangle<float>(plot.getX() - 2.0f, y - 8.0f, 58.0f, 16.0f),
                       juce::Justification::left,
                       false);
        }
    }

    if (diagnosticsExpanded) {
        auto diag = diagnosticsBounds.reduced(16, 12);
        g.setColour(text.withAlpha(0.82f));
        const float diagFontHeight = diagnosticsBounds.getHeight() >= 180 ? 13.0f : 12.0f;
        const int maxLines = diagnosticsBounds.getHeight() >= 180 ? 14 : 10;
        g.setFont(juce::FontOptions().withHeight(diagFontHeight));
        g.drawFittedText(currentRenderModel.diagnosticsText, diag, juce::Justification::topLeft, maxLines);
    }
}

void VXStudioAnalyserEditor::resized() {
    auto area = getLocalBounds().reduced(20, 18);
    auto header = area.removeFromTop(88);
    suiteLabel.setBounds(header.removeFromTop(18));
    titleLabel.setBounds(header.removeFromTop(34));
    subtitleLabel.setBounds(header.removeFromTop(20));
    statusLabel.setBounds(header.removeFromTop(20));

    area.removeFromTop(8);
    const int availableWidth = area.getWidth();
    const int desiredChainWidth = juce::jlimit(220, 340, availableWidth / 4);
    const int actualChainWidth = chainCollapsed ? 0 : desiredChainWidth;
    chainBounds = area.removeFromLeft(actualChainWidth);
    if (!chainCollapsed)
        area.removeFromLeft(14);
    contentBounds = area;

    auto chainArea = chainBounds.reduced(16, 16);
    constexpr int kRailHeaderHeight = 32;
    if (!chainCollapsed) {
        chainArea.removeFromTop(kRailHeaderHeight + 10);
        fullChainButton.setBounds(chainArea.removeFromTop(34));
        chainArea.removeFromTop(14);
        stageRowBounds.clear();
        constexpr int kStageRowHeight = 58;
        for (std::size_t i = 0; i < currentRenderModel.chainRows.size(); ++i) {
            if (chainArea.getHeight() < 40)
                break;
            const auto rowBounds = chainArea.removeFromTop(kStageRowHeight);
            stageRowBounds.push_back(rowBounds);
            chainArea.removeFromTop(10);
        }
    } else {
        fullChainButton.setBounds({});
        stageRowBounds.clear();
    }

    auto contentArea = contentBounds.reduced(22, 18);
    const bool compactControls = contentArea.getWidth() < 840;
    selectionLabel.setBounds(contentArea.removeFromTop(compactControls ? 36 : 34));
    const int summaryHeight = compactControls ? 96 : 82;
    summaryBounds = contentArea.removeFromTop(summaryHeight);
    summaryLabel.setBounds(summaryBounds.reduced(0, 2));
    tabsBounds = {};
    toneTabButton.setBounds({});
    dynamicsTabButton.setBounds({});
    auto controlsArea = contentArea.removeFromTop(compactControls ? 72 : 34);
    if (compactControls) {
        auto rowOne = controlsArea.removeFromTop(32);
        averageTimeLabel.setBounds(rowOne.removeFromLeft(86));
        averageTimeBox.setBounds(rowOne.removeFromLeft(146));
        rowOne.removeFromLeft(16);
        smoothingLabel.setBounds(rowOne.removeFromLeft(96));
        smoothingBox.setBounds(rowOne.removeFromLeft(146));

        controlsArea.removeFromTop(6);
        auto rowTwo = controlsArea.removeFromTop(32);
        chainToggleButton.setBounds(rowTwo.removeFromLeft(140));
    } else {
        auto controlsRow = controlsArea.removeFromTop(32);
        averageTimeLabel.setBounds(controlsRow.removeFromLeft(86));
        averageTimeBox.setBounds(controlsRow.removeFromLeft(144));
        controlsRow.removeFromLeft(16);
        smoothingLabel.setBounds(controlsRow.removeFromLeft(96));
        smoothingBox.setBounds(controlsRow.removeFromLeft(144));
        controlsRow.removeFromLeft(18);
        chainToggleButton.setBounds(controlsRow.removeFromLeft(140));
    }
    contentArea.removeFromTop(8);
    const int diagnosticsHeight = diagnosticsExpanded
        ? juce::jlimit(136, 220, contentArea.getHeight() / 3)
        : 28;
    diagnosticsBounds = contentArea.removeFromBottom(diagnosticsHeight);
    diagnosticsToggleButton.setBounds(diagnosticsBounds.removeFromTop(28));
    if (!diagnosticsExpanded)
        diagnosticsBounds = {};
    contentArea.removeFromTop(8);
    plotBounds = contentArea.toNearestInt();
}

void VXStudioAnalyserEditor::mouseUp(const juce::MouseEvent& event) {
    const auto localPosition = event.getEventRelativeTo(this).position.toInt();
    for (int index = 0; index < static_cast<int>(stageRowBounds.size()); ++index) {
        if (stageRowBounds[static_cast<std::size_t>(index)].contains(localPosition)) {
            selectStage(index);
            return;
        }
    }
}

void VXStudioAnalyserEditor::timerCallback() {
    refreshRenderModel();
    applyPendingRenderModel();
}

void VXStudioAnalyserEditor::refreshRenderModel() {
    struct SnapshotEntry {
        int order = 0;
        juce::String productName;
        juce::String displayName;
        juce::String canonicalKey;
        juce::String shortTag;
        bool silent = true;
        std::int64_t lastPublishMs = 0;
    };

    const auto nowMs = static_cast<std::uint64_t>(juce::Time::currentTimeMillis());
    std::vector<StageEntry> externalStages;
    std::vector<SnapshotEntry> snapshotStages;
    std::optional<StageEntry> analyserStage;
    externalStages.reserve(vxsuite::analysis::StageRegistry::instance().maxSlots());
    snapshotStages.reserve(vxsuite::spectrum::SnapshotRegistry::instance().maxSlots());

    for (int slotIndex = 0; slotIndex < vxsuite::analysis::StageRegistry::instance().maxSlots(); ++slotIndex) {
        vxsuite::analysis::StageView stage;
        if (!vxsuite::analysis::StageRegistry::instance().readStage(slotIndex, stage))
            continue;
        if (!stage.active)
            continue;
        if (labelFromChars(stage.telemetry.identity.pluginFamily) != "VXSuite")
            continue;
        const auto stageAgeMs = nowMs - stage.telemetry.state.timestampMs;
        if (stageAgeMs > kStaleThresholdMs)
            continue;
        if (stage.analysisDomainId != processor.analysisDomainId())
            continue;

        StageEntry entry;
        entry.view = stage;
        entry.stageId = labelFromChars(stage.telemetry.identity.stageId);
        entry.stageName = labelFromChars(stage.telemetry.identity.stageName);
        entry.stateText = stage.telemetry.state.isBypassed ? "Bypassed"
                        : stage.telemetry.state.isSilent ? "Silent"
                                                         : "Active";
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> stageDeltaDb {};
        float spectralDeltaSum = 0.0f;
        float largestStageDelta = 0.0f;
        int largestStageBand = 0;
        for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
            const float deltaDb = toDb(stage.telemetry.outputSummary.spectrum[static_cast<std::size_t>(i)])
                                - toDb(stage.telemetry.inputSummary.spectrum[static_cast<std::size_t>(i)]);
            stageDeltaDb[static_cast<std::size_t>(i)] = deltaDb;
            spectralDeltaSum += std::abs(deltaDb);
            if (std::abs(deltaDb) > std::abs(largestStageDelta)) {
                largestStageDelta = deltaDb;
                largestStageBand = i;
            }
        }
        entry.spectralChange = spectralDeltaSum / static_cast<float>(vxsuite::analysis::kSummarySpectrumBins);
        entry.dynamicChange = std::abs(toDb(stage.telemetry.outputSummary.rms, -120.0f)
                                       - toDb(stage.telemetry.inputSummary.rms, -120.0f));
        entry.stereoChange = std::abs(stage.telemetry.outputSummary.stereoWidth - stage.telemetry.inputSummary.stereoWidth)
                           + std::abs(stage.telemetry.outputSummary.correlation - stage.telemetry.inputSummary.correlation);
        entry.impactScore = 0.45f * entry.spectralChange + 0.45f * entry.dynamicChange + 0.10f * entry.stereoChange;
        entry.impactText = signedDb(juce::jlimit(-24.0f, 24.0f, largestStageDelta));
        const auto sparseStage = classifySparseTone(stage.telemetry.inputSummary.spectrum,
                                                    stage.telemetry.outputSummary.spectrum,
                                                    stageDeltaDb);
        entry.typeLabel = sparseStage.sparse ? "Sparse"
                                             : classLabel(entry.spectralChange, entry.dynamicChange, entry.stereoChange);
        entry.freqHint = "@" + formatFrequency(bandCenterHz(largestStageBand));

        if (entry.stageId == processor.stageIdString()) {
            analyserStage = entry;
            continue;
        }

        externalStages.push_back(std::move(entry));
    }

    for (int slotIndex = 0; slotIndex < vxsuite::spectrum::SnapshotRegistry::instance().maxSlots(); ++slotIndex) {
        vxsuite::spectrum::SnapshotView snapshot;
        if (!vxsuite::spectrum::SnapshotRegistry::instance().readSlot(slotIndex, snapshot))
            continue;

        const auto shortTag = labelFromChars(snapshot.shortTag);
        if (shortTag == "VSA")
            continue;

        if ((nowMs - static_cast<std::uint64_t>(std::max<std::int64_t>(0, snapshot.lastPublishMs))) > kFallbackStageThresholdMs)
            continue;

        snapshotStages.push_back({
            snapshot.order,
            labelFromChars(snapshot.productName),
            displayStageName(labelFromChars(snapshot.productName)),
            canonicalStageKey(labelFromChars(snapshot.productName)),
            shortTag,
            snapshot.silent,
            snapshot.lastPublishMs
        });
    }

    std::sort(snapshotStages.begin(), snapshotStages.end(), [](const auto& a, const auto& b) {
        return a.order < b.order;
    });

    for (const auto& snapshot : snapshotStages) {
        const auto existing = std::find_if(sidebarSnapshotCache.begin(),
                                           sidebarSnapshotCache.end(),
                                           [&](const SidebarSnapshotCacheEntry& cached) {
                                               return cached.canonicalKey == snapshot.canonicalKey;
                                           });
        if (existing != sidebarSnapshotCache.end()) {
            existing->order = snapshot.order;
            existing->productName = snapshot.productName;
            existing->displayName = snapshot.displayName;
            existing->canonicalKey = snapshot.canonicalKey;
            existing->shortTag = snapshot.shortTag;
            existing->silent = snapshot.silent;
            existing->lastPublishMs = snapshot.lastPublishMs;
        } else {
            sidebarSnapshotCache.push_back({
                snapshot.order,
                snapshot.productName,
                snapshot.displayName,
                snapshot.canonicalKey,
                snapshot.shortTag,
                snapshot.silent,
                snapshot.lastPublishMs
            });
        }
    }

    sidebarSnapshotCache.erase(std::remove_if(sidebarSnapshotCache.begin(),
                                              sidebarSnapshotCache.end(),
                                              [&](const SidebarSnapshotCacheEntry& cached) {
                                                  return (nowMs - static_cast<std::uint64_t>(
                                                             std::max<std::int64_t>(0, cached.lastPublishMs)))
                                                      > kSidebarSnapshotHoldMs;
                                              }),
                               sidebarSnapshotCache.end());

    std::sort(sidebarSnapshotCache.begin(), sidebarSnapshotCache.end(), [](const auto& a, const auto& b) {
        if (a.order != b.order)
            return a.order < b.order;
        return a.displayName < b.displayName;
    });

    std::sort(externalStages.begin(), externalStages.end(), [](const auto& a, const auto& b) {
        return a.view.telemetry.identity.localOrderId < b.view.telemetry.identity.localOrderId;
    });

    const bool hasAnalyserSignal = analyserStage.has_value()
        && analyserStage->view.telemetry.inputSummary.rms > 1.0e-6f;

    // Build canonical key set from domain stages so we can filter the sidebar.
    std::vector<juce::String> chainStageKeys;
    chainStageKeys.reserve(externalStages.size() * 2);
    for (const auto& stage : externalStages) {
        chainStageKeys.push_back(canonicalStageKey(stage.stageName));
        chainStageKeys.push_back(canonicalStageKey(stage.stageId));
    }

    // When we have a live signal, strip the sidebar of snapshots that don't belong
    // to our domain (i.e. plugins from other tracks).
    if (hasAnalyserSignal && !chainStageKeys.empty()) {
        sidebarSnapshotCache.erase(
            std::remove_if(sidebarSnapshotCache.begin(), sidebarSnapshotCache.end(),
                [&](const SidebarSnapshotCacheEntry& cached) {
                    return std::none_of(chainStageKeys.begin(), chainStageKeys.end(),
                        [&](const juce::String& key) { return key == cached.canonicalKey; });
                }),
            sidebarSnapshotCache.end());
    }

    struct StageMatchKey {
        juce::String nameKey;
        juce::String idKey;
    };
    std::vector<StageMatchKey> externalStageKeys;
    externalStageKeys.reserve(externalStages.size());
    for (const auto& stage : externalStages) {
        externalStageKeys.push_back({
            canonicalStageKey(stage.stageName),
            canonicalStageKey(stage.stageId)
        });
    }

    int selectedIndexValue = selectedStageIndex.load();
    bool fullChain = fullChainSelected.load();
    const int maxSelectableRows = !sidebarSnapshotCache.empty()
        ? static_cast<int>(sidebarSnapshotCache.size())
        : static_cast<int>(externalStages.size());
    if (selectedIndexValue >= maxSelectableRows) {
        selectedIndexValue = -1;
        selectedStageIndex.store(-1);
        fullChain = true;
        fullChainSelected.store(true);
    }

    if (analyserStage.has_value() && analyserStage->view.telemetry.state.isBypassed) {
        currentRenderModel = {};
        currentRenderModel.bypassed = true;
        currentRenderModel.valid = false;
        currentRenderModel.selectionTitle = "Bypassed";
        currentRenderModel.statusText = "Analyser is bypassed";
        currentRenderModel.summaryLines = { "Analyser bypassed", "Plugin is disabled in the host", "", "" };
        return;
    }

    RenderModel model;
    model.snapshotFallback = externalStages.empty() && !snapshotStages.empty();
    int matchedTelemetryRows = 0;

    if (!sidebarSnapshotCache.empty() || !externalStages.empty()) {
        model.chainRows.reserve(sidebarSnapshotCache.size() + externalStages.size());
        model.chainRowStageIndices.reserve(sidebarSnapshotCache.size() + externalStages.size());
        std::vector<bool> matchedExternalStages(static_cast<std::size_t>(externalStages.size()), false);
        for (const auto& snapshot : sidebarSnapshotCache) {
            int matchedStageIndex = -1;
            for (int index = 0; index < static_cast<int>(externalStages.size()); ++index) {
                const auto& stageNameKey = externalStageKeys[static_cast<std::size_t>(index)].nameKey;
                const auto& stageIdKey = externalStageKeys[static_cast<std::size_t>(index)].idKey;
                if (stageNameKey == snapshot.canonicalKey
                    || stageIdKey == snapshot.canonicalKey) {
                    matchedStageIndex = index;
                    break;
                }
            }

            if (matchedStageIndex >= 0) {
                ++matchedTelemetryRows;
                matchedExternalStages[static_cast<std::size_t>(matchedStageIndex)] = true;
                const auto& stage = externalStages[static_cast<std::size_t>(matchedStageIndex)];
                model.chainRows.push_back({
                    snapshot.displayName,
                    stage.stateText,
                    stage.impactText,
                    stage.typeLabel,
                    stage.freqHint,
                    !fullChain && static_cast<int>(model.chainRows.size()) == selectedIndexValue
                });
                model.chainRowStageIndices.push_back(matchedStageIndex);
            } else {
                model.chainRows.push_back({
                    snapshot.displayName,
                    (nowMs - static_cast<std::uint64_t>(std::max<std::int64_t>(0, snapshot.lastPublishMs))) > kStaleThresholdMs
                        ? "Holding"
                        : (snapshot.silent ? "Silent" : "Live"),
                    "",
                    "Waiting",
                    "",
                    !fullChain && static_cast<int>(model.chainRows.size()) == selectedIndexValue
                });
                model.chainRowStageIndices.push_back(-1);
            }
        }

        for (int index = 0; index < static_cast<int>(externalStages.size()); ++index) {
            if (index < static_cast<int>(matchedExternalStages.size())
                && matchedExternalStages[static_cast<std::size_t>(index)]) {
                continue;
            }
            const auto& stage = externalStages[static_cast<std::size_t>(index)];
            model.chainRows.push_back({
                displayStageName(stage.stageName),
                stage.stateText,
                stage.impactText,
                stage.typeLabel,
                stage.freqHint,
                !fullChain && static_cast<int>(model.chainRows.size()) == selectedIndexValue
            });
            model.chainRowStageIndices.push_back(index);
        }
    }

    juce::String selectedLabel = "Full Chain";
    if (!fullChain && selectedIndexValue >= 0 && selectedIndexValue < static_cast<int>(model.chainRows.size()))
        selectedLabel = model.chainRows[static_cast<std::size_t>(selectedIndexValue)].stageName;
    model.statusText =
        "Domain " + juce::String(static_cast<juce::int64>(processor.analysisDomainId()))
        + " | live chain: " + juce::String(static_cast<int>(sidebarSnapshotCache.size()))
        + " | telemetry: " + juce::String(matchedTelemetryRows)
        + " | selection: " + selectedLabel;

    const bool incompleteStageCoverage =
        !sidebarSnapshotCache.empty() && matchedTelemetryRows < static_cast<int>(sidebarSnapshotCache.size());

    vxsuite::analysis::AnalysisSummary before {};
    vxsuite::analysis::AnalysisSummary after {};
    juce::String selectionKey = "empty";
    juce::String scopeLabel;

    if (fullChain && incompleteStageCoverage && analyserStage.has_value()) {
        before = analyserStage->view.telemetry.inputSummary;
        after = analyserStage->view.telemetry.outputSummary;
        scopeLabel = "Chain Syncing";
        selectionKey = "partial-telemetry";
        model.valid = true;
    } else if (!externalStages.empty()) {
        if (fullChain || selectedIndexValue < 0) {
            int firstMatchedStageIndex = -1;
            int lastMatchedStageIndex = -1;
            for (const int stageIndex : model.chainRowStageIndices) {
                if (stageIndex < 0 || stageIndex >= static_cast<int>(externalStages.size()))
                    continue;
                if (firstMatchedStageIndex < 0)
                    firstMatchedStageIndex = stageIndex;
                lastMatchedStageIndex = stageIndex;
            }

            if (firstMatchedStageIndex >= 0 && lastMatchedStageIndex >= 0) {
                before = externalStages[static_cast<std::size_t>(firstMatchedStageIndex)].view.telemetry.inputSummary;
                after = analyserStage.has_value()
                    ? analyserStage->view.telemetry.inputSummary
                    : externalStages[static_cast<std::size_t>(lastMatchedStageIndex)].view.telemetry.outputSummary;
            } else {
                before = externalStages.front().view.telemetry.inputSummary;
                after = analyserStage.has_value()
                    ? analyserStage->view.telemetry.inputSummary
                    : externalStages.back().view.telemetry.outputSummary;
            }
            scopeLabel = "Full Chain";
            selectionKey = "full";
        } else {
            int matchedStageIndex = -1;
            if (selectedIndexValue >= 0
                && selectedIndexValue < static_cast<int>(model.chainRowStageIndices.size())) {
                matchedStageIndex = model.chainRowStageIndices[static_cast<std::size_t>(selectedIndexValue)];
            }

            if (matchedStageIndex >= 0 && matchedStageIndex < static_cast<int>(externalStages.size())) {
                const auto& stage = externalStages[static_cast<std::size_t>(matchedStageIndex)];
                before = stage.view.telemetry.inputSummary;
                after = stage.view.telemetry.outputSummary;
                scopeLabel = model.chainRows[static_cast<std::size_t>(selectedIndexValue)].stageName;
                selectionKey = "stage:" + stage.stageId + ":" + juce::String(selectedIndexValue);
            } else if (analyserStage.has_value()) {
                before = analyserStage->view.telemetry.inputSummary;
                after = analyserStage->view.telemetry.outputSummary;
                scopeLabel = model.chainRows[static_cast<std::size_t>(selectedIndexValue)].stageName + " (Waiting)";
                selectionKey = "waiting:" + juce::String(selectedIndexValue);
            }
        }
        model.valid = !selectionKey.isEmpty() && selectionKey != "empty";
    } else if (analyserStage.has_value()) {
        before = analyserStage->view.telemetry.inputSummary;
        after = analyserStage->view.telemetry.outputSummary;
        scopeLabel = "Analyser Only";
        selectionKey = "dry-only";
        model.valid = true;
    }

    if (model.valid) {
        const bool resetSmoothing = !backendState.initialized || backendState.selectionKey != selectionKey;
        backendState.selectionKey = selectionKey;
        backendState.initialized = true;
        const float averageSeconds = currentAverageTimeSeconds();
        const float deltaDisplaySeconds = std::max(0.10f, averageSeconds * 0.75f);
        const float summarySmoothingSeconds = std::max(0.12f, averageSeconds * 0.60f);
        const int smoothingRadius = currentSpectrumSmoothingRadius();

        auto smoothedBeforeTone = model.beforeToneDb;
        auto smoothedAfterTone = model.afterToneDb;
        auto smoothedDeltaTone = model.deltaToneDb;

        if (resetSmoothing) {
            backendState.spectrumHistory.clear();
            backendState.beforeToneLinearSum.fill(0.0f);
            backendState.afterToneLinearSum.fill(0.0f);
        }

        BackendState::SpectrumHistoryFrame historyFrame;
        historyFrame.timestampMs = nowMs;
        for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
            historyFrame.beforeLinear[static_cast<std::size_t>(i)] =
                std::max(1.0e-6f, before.spectrum[static_cast<std::size_t>(i)]);
            historyFrame.afterLinear[static_cast<std::size_t>(i)] =
                std::max(1.0e-6f, after.spectrum[static_cast<std::size_t>(i)]);
            backendState.beforeToneLinearSum[static_cast<std::size_t>(i)] += historyFrame.beforeLinear[static_cast<std::size_t>(i)];
            backendState.afterToneLinearSum[static_cast<std::size_t>(i)] += historyFrame.afterLinear[static_cast<std::size_t>(i)];
        }
        backendState.spectrumHistory.push_back(historyFrame);

        const auto averageWindowMs = static_cast<std::uint64_t>(std::max(100.0f, averageSeconds * 1000.0f));
        while (backendState.spectrumHistory.size() > 1
               && ((nowMs - backendState.spectrumHistory.front().timestampMs) > averageWindowMs
                   || static_cast<int>(backendState.spectrumHistory.size()) > kMaxSpectrumHistoryFrames)) {
            const auto& expired = backendState.spectrumHistory.front();
            for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
                backendState.beforeToneLinearSum[static_cast<std::size_t>(i)] -= expired.beforeLinear[static_cast<std::size_t>(i)];
                backendState.afterToneLinearSum[static_cast<std::size_t>(i)] -= expired.afterLinear[static_cast<std::size_t>(i)];
            }
            backendState.spectrumHistory.pop_front();
        }

        const float historyScale = 1.0f / static_cast<float>(std::max<std::size_t>(1, backendState.spectrumHistory.size()));

        for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
            backendState.beforeToneLinear[static_cast<std::size_t>(i)] =
                std::max(1.0e-6f, backendState.beforeToneLinearSum[static_cast<std::size_t>(i)] * historyScale);
            backendState.afterToneLinear[static_cast<std::size_t>(i)] =
                std::max(1.0e-6f, backendState.afterToneLinearSum[static_cast<std::size_t>(i)] * historyScale);
        }

        float beforeMeanDb = 0.0f;
        float afterMeanDb = 0.0f;
        for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
            const float beforeBandDb = toDb(backendState.beforeToneLinear[static_cast<std::size_t>(i)], -120.0f);
            const float afterBandDb = toDb(backendState.afterToneLinear[static_cast<std::size_t>(i)], -120.0f);
            beforeMeanDb += beforeBandDb;
            afterMeanDb += afterBandDb;
        }
        beforeMeanDb /= static_cast<float>(vxsuite::analysis::kSummarySpectrumBins);
        afterMeanDb /= static_cast<float>(vxsuite::analysis::kSummarySpectrumBins);

        float lowSum = 0.0f;
        float midSum = 0.0f;
        float highSum = 0.0f;
        int lowCount = 0;
        int midCount = 0;
        int highCount = 0;
        float largestDelta = 0.0f;
        int largestBand = 0;

        for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
            const float hz = bandCenterHz(i);
            const float beforeLinear = backendState.beforeToneLinear[static_cast<std::size_t>(i)];
            const float afterLinear = backendState.afterToneLinear[static_cast<std::size_t>(i)];
            const float beforeDb = juce::jlimit(kSpectrumMinDb,
                                                kSpectrumMaxDb,
                                                applyDisplaySlope(toDb(beforeLinear, -120.0f),
                                                                  hz));
            const float afterDb = juce::jlimit(kSpectrumMinDb,
                                               kSpectrumMaxDb,
                                               applyDisplaySlope(toDb(afterLinear, -120.0f),
                                                                 hz));
            const float deltaTarget = juce::jlimit(-24.0f,
                                                   24.0f,
                                                   toDb(afterLinear)
                                                       - toDb(beforeLinear));
            if (resetSmoothing) {
                backendState.deltaToneDb[static_cast<std::size_t>(i)] = deltaTarget;
            } else {
                backendState.deltaToneDb[static_cast<std::size_t>(i)] =
                    smoothDisplayValue(backendState.deltaToneDb[static_cast<std::size_t>(i)],
                                       deltaTarget,
                                       deltaDisplaySeconds);
            }

            backendState.displayBeforeToneDb[static_cast<std::size_t>(i)] = beforeDb;
            backendState.displayAfterToneDb[static_cast<std::size_t>(i)] = afterDb;
            smoothedBeforeTone[static_cast<std::size_t>(i)] = backendState.displayBeforeToneDb[static_cast<std::size_t>(i)];
            smoothedAfterTone[static_cast<std::size_t>(i)] = backendState.displayAfterToneDb[static_cast<std::size_t>(i)];
            const float rawDelta = backendState.deltaToneDb[static_cast<std::size_t>(i)];
            const float deltaDisplayTarget = std::abs(rawDelta) < 1.0f ? 0.0f : rawDelta;
            if (resetSmoothing) {
                backendState.displayDeltaToneDb[static_cast<std::size_t>(i)] = deltaDisplayTarget;
            } else {
                backendState.displayDeltaToneDb[static_cast<std::size_t>(i)] =
                    smoothScalar(backendState.displayDeltaToneDb[static_cast<std::size_t>(i)],
                                 deltaDisplayTarget,
                                 averageSeconds);
            }
            smoothedDeltaTone[static_cast<std::size_t>(i)] = backendState.displayDeltaToneDb[static_cast<std::size_t>(i)];

            if (hz < kLowHighBoundaryHz) {
                lowSum += smoothedDeltaTone[static_cast<std::size_t>(i)];
                ++lowCount;
            } else if (hz < kMidHighBoundaryHz) {
                midSum += smoothedDeltaTone[static_cast<std::size_t>(i)];
                ++midCount;
            } else {
                highSum += smoothedDeltaTone[static_cast<std::size_t>(i)];
                ++highCount;
            }

            if (hasMeaningfulBandEnergy(beforeLinear, afterLinear)
                && std::abs(smoothedDeltaTone[static_cast<std::size_t>(i)]) > std::abs(largestDelta)) {
                largestDelta = smoothedDeltaTone[static_cast<std::size_t>(i)];
                largestBand = i;
            }
        }

        const auto sparseTone = classifySparseTone(backendState.beforeToneLinear,
                                                   backendState.afterToneLinear,
                                                   smoothedDeltaTone);
        model.sparseTone = sparseTone.sparse;
        model.sparseToneBands = sparseTone.significantBands;

        if (model.sparseTone) {
            model.beforeToneDb = smoothedBeforeTone;
            model.afterToneDb = smoothedAfterTone;
            model.deltaToneDb = smoothedDeltaTone;
        } else {
            model.beforeToneDb = smoothNeighbourBins(smoothedBeforeTone, smoothingRadius);
            model.afterToneDb = smoothNeighbourBins(smoothedAfterTone, smoothingRadius);
            model.deltaToneDb = smoothNeighbourBins(smoothedDeltaTone, smoothingRadius);
        }

        model.largestToneBand = largestBand;
        const float lowAvg = lowCount > 0 ? lowSum / static_cast<float>(lowCount) : 0.0f;
        const float midAvg = midCount > 0 ? midSum / static_cast<float>(midCount) : 0.0f;
        const float highAvg = highCount > 0 ? highSum / static_cast<float>(highCount) : 0.0f;

        backendState.largestToneDeltaDb = resetSmoothing ? largestDelta
                                                         : smoothScalar(backendState.largestToneDeltaDb, largestDelta, summarySmoothingSeconds);
        backendState.lowToneDeltaDb = resetSmoothing ? lowAvg
                                                     : smoothScalar(backendState.lowToneDeltaDb, lowAvg, summarySmoothingSeconds);
        backendState.midToneDeltaDb = resetSmoothing ? midAvg
                                                     : smoothScalar(backendState.midToneDeltaDb, midAvg, summarySmoothingSeconds);
        backendState.highToneDeltaDb = resetSmoothing ? highAvg
                                                      : smoothScalar(backendState.highToneDeltaDb, highAvg, summarySmoothingSeconds);

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
                                 averageSeconds);
                backendState.afterDynamicsLinear[static_cast<std::size_t>(i)] =
                    smoothScalar(backendState.afterDynamicsLinear[static_cast<std::size_t>(i)],
                                 afterTarget,
                                 averageSeconds);
            }

            model.beforeDynamicsDb[static_cast<std::size_t>(i)] =
                juce::jlimit(kDynamicsMinDb, kDynamicsMaxDb, toDb(backendState.beforeDynamicsLinear[static_cast<std::size_t>(i)], -120.0f));
            model.afterDynamicsDb[static_cast<std::size_t>(i)] =
                juce::jlimit(kDynamicsMinDb, kDynamicsMaxDb, toDb(backendState.afterDynamicsLinear[static_cast<std::size_t>(i)], -120.0f));
        }

        model.beforeDynamicsDb = smoothNeighbourBins(model.beforeDynamicsDb, std::max(1, smoothingRadius));
        model.afterDynamicsDb = smoothNeighbourBins(model.afterDynamicsDb, std::max(1, smoothingRadius));

        const float peakDeltaTarget = toDb(after.peak, -120.0f) - toDb(before.peak, -120.0f);
        const float rmsDeltaTarget = toDb(after.rms, -120.0f) - toDb(before.rms, -120.0f);
        const float crestDeltaTarget = toDb(std::max(1.0e-6f, after.crestFactor), -120.0f)
            - toDb(std::max(1.0e-6f, before.crestFactor), -120.0f);
        const float transientDeltaTarget = after.transientScore - before.transientScore;
        backendState.peakDeltaDb = resetSmoothing ? peakDeltaTarget
                                                  : smoothScalar(backendState.peakDeltaDb, peakDeltaTarget, summarySmoothingSeconds);
        backendState.rmsDeltaDb = resetSmoothing ? rmsDeltaTarget
                                                 : smoothScalar(backendState.rmsDeltaDb, rmsDeltaTarget, summarySmoothingSeconds);
        backendState.crestDeltaDb = resetSmoothing ? crestDeltaTarget
                                                   : smoothScalar(backendState.crestDeltaDb, crestDeltaTarget, summarySmoothingSeconds);
        backendState.transientDelta = resetSmoothing ? transientDeltaTarget
                                                     : smoothScalar(backendState.transientDelta, transientDeltaTarget, summarySmoothingSeconds);

        model.selectionTitle = "Spectrum  |  " + scopeLabel;
        if (selectionKey == "partial-telemetry") {
            model.summaryLines = {
                "Spectrum -> Chain Syncing",
                "Visible telemetry " + juce::String(matchedTelemetryRows)
                    + " / snapshots " + juce::String(static_cast<int>(sidebarSnapshotCache.size())),
                "Waiting for complete stage telemetry before building full-chain comparison",
                "Current graph is analyser passthrough only to avoid false spikes"
            };
        } else if (selectionKey.startsWith("waiting:")) {
            model.summaryLines = {
                "Spectrum -> Waiting for telemetry",
                "Live chain row is present, but this stage has not published matched telemetry yet",
                "Sidebar is driven by the active VX chain, analysis will appear as soon as telemetry arrives",
                "Current graph is analyser passthrough only to avoid false spikes"
            };
        } else {
            model.summaryLines = buildToneSummary("Spectrum -> " + scopeLabel,
                                                  model.deltaToneDb,
                                                  backendState.beforeToneLinear,
                                                  backendState.afterToneLinear,
                                                  model.largestToneBand,
                                                  model.sparseTone,
                                                  model.sparseToneBands,
                                                  toDb(before.rms, -120.0f),
                                                  toDb(after.rms, -120.0f),
                                                  backendState.lowToneDeltaDb,
                                                  backendState.midToneDeltaDb,
                                                  backendState.highToneDeltaDb);
        }
        juce::String snapshotList = "hidden";
        if (diagnosticsExpanded) {
            juce::StringArray snapshotNames;
            snapshotNames.ensureStorageAllocated(static_cast<int>(sidebarSnapshotCache.size()));
            for (const auto& snapshot : sidebarSnapshotCache)
                snapshotNames.add(snapshot.productName + " (" + snapshot.shortTag + ")");
            snapshotList = snapshotNames.isEmpty() ? juce::String("none") : snapshotNames.joinIntoString(", ");
        }

        model.diagnosticsText =
            "Domain: " + juce::String(static_cast<juce::int64>(processor.analysisDomainId()))
            + "\nStage count: " + juce::String(static_cast<int>(externalStages.size()))
            + "\nSnapshot count: " + juce::String(static_cast<int>(sidebarSnapshotCache.size()))
            + "\nMatched rows: " + juce::String(matchedTelemetryRows)
            + "\nStage source: Current domain"
            + "\nSidebar source: Live chain snapshots"
            + "\nCoverage: " + juce::String(incompleteStageCoverage ? "Partial" : "Complete")
            + "\nFreshness: <= " + juce::String(static_cast<int>(kStaleThresholdMs)) + " ms"
            + "\nOrder confidence: Deterministic via domain + localOrderId"
            + "\nCapabilities: Dry/Wet Spectrum Tier 1"
            + "\nSpectrum render: Overlay mode"
            + "\nAvg time: " + juce::String(averageSeconds, 2) + " s"
            + "\nSmoothing: " + juce::String(kSmoothingOptions[static_cast<std::size_t>(
                  juce::jlimit(0, static_cast<int>(kSmoothingOptions.size()) - 1, smoothingIndex.load()))])
            + "\nSnapshots: " + snapshotList
            + "\nFallback mode: " + juce::String(selectionKey == "dry-only" ? "Analyser passthrough only" : "Normal stage comparison")
            + "\nSelection key: " + selectionKey;
    }

    currentRenderModel = std::move(model);
}

void VXStudioAnalyserEditor::applyPendingRenderModel() {
    statusLabel.setText(currentRenderModel.statusText, juce::dontSendNotification);
    selectionLabel.setText(currentRenderModel.selectionTitle, juce::dontSendNotification);
    summaryLabel.setText(currentRenderModel.summaryLines[1] + "\n"
                             + currentRenderModel.summaryLines[2] + "\n"
                             + currentRenderModel.summaryLines[3],
                         juce::dontSendNotification);
    rebuildStageButtons();
    repaint();
}

void VXStudioAnalyserEditor::rebuildStageButtons() {
    fullChainButton.setColour(juce::TextButton::buttonColourId,
                              fullChainSelected.load() ? colourFromRgb(processor.theme().accentRgb, 0.40f)
                                                       : juce::Colours::white.withAlpha(0.06f));
    const auto newRowCount = currentRenderModel.chainRows.size();
    if (newRowCount != prevChainRowCount) {
        prevChainRowCount = newRowCount;
        resized();
    }
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
    toneTabButton.setVisible(false);
    dynamicsTabButton.setVisible(false);
}

float VXStudioAnalyserEditor::currentAverageTimeSeconds() const noexcept {
    return static_cast<float>(averageTimeMsFromIndex(averageTimeIndex.load())) / 1000.0f;
}

int VXStudioAnalyserEditor::currentSpectrumSmoothingRadius() const noexcept {
    const float binsPerOctave = static_cast<float>(vxsuite::analysis::kSummarySpectrumBins)
        / std::log2(20000.0f / 20.0f);

    float smoothingOctaves = 0.0f;
    switch (juce::jlimit(0, static_cast<int>(kSmoothingOptions.size()) - 1, smoothingIndex.load())) {
        case 0: smoothingOctaves = 0.0f; break;
        case 1: smoothingOctaves = 1.0f / 12.0f; break;
        case 2: smoothingOctaves = 1.0f / 9.0f; break;
        case 3: smoothingOctaves = 1.0f / 6.0f; break;
        case 4: smoothingOctaves = 1.0f / 3.0f; break;
        case 5: smoothingOctaves = 1.0f / 2.0f; break;
        case 6: smoothingOctaves = 1.0f; break;
        default: smoothingOctaves = 1.0f / 6.0f; break;
    }

    return juce::jlimit(0,
                        static_cast<int>(vxsuite::analysis::kSummarySpectrumBins / 8),
                        juce::roundToInt(binsPerOctave * smoothingOctaves));
}

juce::Path VXStudioAnalyserEditor::makeTonePath(
    const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& valuesDb,
    const juce::Rectangle<float> bounds) const {
    juce::Path path;
    const int n = static_cast<int>(valuesDb.size());
    if (n < 2)
        return path;

    std::array<juce::Point<float>, vxsuite::analysis::kSummarySpectrumBins> pts {};
    for (int i = 0; i < n; ++i) {
        pts[static_cast<std::size_t>(i)] = {
            xForFrequency(bandCenterHz(i), bounds),
            juce::jmap(valuesDb[static_cast<std::size_t>(i)], kSpectrumMinDb, kSpectrumMaxDb, bounds.getBottom(), bounds.getY())
        };
    }

    path.startNewSubPath(pts[0]);
    for (int i = 1; i < n; ++i)
        path.lineTo(pts[static_cast<std::size_t>(i)]);
    return path;
}

juce::Path VXStudioAnalyserEditor::makeDynamicsPath(
    const std::array<float, vxsuite::analysis::kSummaryEnvelopeBins>& valuesDb,
    const juce::Rectangle<float> bounds) const {
    juce::Path path;
    std::array<juce::Point<float>, vxsuite::analysis::kSummaryEnvelopeBins> points {};
    for (int i = 0; i < static_cast<int>(valuesDb.size()); ++i) {
        points[static_cast<std::size_t>(i)] = {
            bounds.getX() + (bounds.getWidth() * static_cast<float>(i))
                / static_cast<float>(std::max(1, static_cast<int>(valuesDb.size()) - 1)),
            juce::jmap(valuesDb[static_cast<std::size_t>(i)],
                       kDynamicsMinDb,
                       kDynamicsMaxDb,
                       bounds.getBottom(),
                       bounds.getY())
        };
    }

    if (points.empty())
        return path;

    path.startNewSubPath(points.front());
    for (int i = 1; i < static_cast<int>(points.size()) - 1; ++i) {
        const auto current = points[static_cast<std::size_t>(i)];
        const auto next = points[static_cast<std::size_t>(i + 1)];
        const auto mid = juce::Point<float>((current.x + next.x) * 0.5f, (current.y + next.y) * 0.5f);
        path.quadraticTo(current, mid);
    }
    path.lineTo(points.back());
    return path;
}

juce::Colour VXStudioAnalyserEditor::colourFromRgb(const std::array<float, 3>& rgb, const float alpha) const noexcept {
    return juce::Colour::fromFloatRGBA(rgb[0], rgb[1], rgb[2], alpha);
}
