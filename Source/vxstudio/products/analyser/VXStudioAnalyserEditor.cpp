#include "VXStudioAnalyserEditor.h"

#include "../../framework/VxStudioBlockSmoothing.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_set>

namespace {

constexpr std::uint64_t kStaleThresholdMs = 8000;
constexpr int kUiRefreshHz = 24;
constexpr float kSpectrumMinDb = -78.0f;
constexpr float kSpectrumMaxDb = -18.0f;
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

float smoothScalar(const float current, const float target, const float timeSeconds) noexcept {
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

std::array<juce::String, 3> buildToneSummary(const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& deltaDb,
                                             const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& beforeLinear,
                                             const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& afterLinear,
                                             const int largestToneBand,
                                             const bool sparseTone,
                                             const std::vector<int>& sparseToneBands,
                                             const float dryRmsDb,
                                             const float wetRmsDb) {
    const float largestDelta = deltaDb[static_cast<std::size_t>(largestToneBand)];

    if (sparseTone) {
        return {
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
        "Dry RMS " + juce::String(dryRmsDb, 1) + " dB   Wet RMS " + juce::String(wetRmsDb, 1) + " dB",
        "Largest: " + signedDb(largestDelta) + " @ " + formatFrequency(bandCenterHz(largestToneBand)),
        dominantText
    };
}

// --- SelectionSummary: the dry/wet pair for the current selection mode. ---

struct SelectionSummary {
    bool valid        = false;
    bool analyserOnly = false;   // true: showing analyser's own input, no external stages
    vxsuite::analysis::AnalysisSummary dry;
    vxsuite::analysis::AnalysisSummary wet;
    juce::String title;
    juce::String scopeKey;  // changes → reset smoothing history
};

// Spectrum/rms aggregated in linear domain. Never average dB.
vxsuite::analysis::AnalysisSummary aggregateSummaries(
    const std::vector<vxsuite::analysis::AnalysisSummary>& list) {
    if (list.empty()) return {};
    if (list.size() == 1) return list[0];
    vxsuite::analysis::AnalysisSummary out {};
    double rmsSquaredSum = 0.0;
    for (const auto& s : list) {
        for (std::size_t i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i)
            out.spectrum[i] += s.spectrum[i];
        rmsSquaredSum += static_cast<double>(s.rms) * s.rms;
        out.peak         = std::max(out.peak, s.peak);
        out.stereoWidth  += s.stereoWidth;
        out.correlation  += s.correlation;
    }
    const float n = static_cast<float>(list.size());
    for (auto& v : out.spectrum) v /= n;
    out.rms         = static_cast<float>(std::sqrt(rmsSquaredSum / list.size()));
    out.stereoWidth /= n;
    out.correlation /= n;
    return out;
}

// Full chain (filtered or all-track): stages must be sorted by (trackStableId, localOrderId).
// dry = linear-averaged per-track first-active-stage input.
// wet = analyser's own live input (real mixed output) if found, else per-track last output average.
template <typename StageEntry, typename OptStageEntry>
SelectionSummary buildChainSummary(const std::vector<StageEntry>& stages,
                                    const OptStageEntry& analyserStage,
                                    const juce::String& title,
                                    const juce::String& scopeKey) {
    std::vector<vxsuite::analysis::AnalysisSummary> dryList, wetList;
    std::uint64_t prevTrack = ~std::uint64_t(0);
    for (const auto& s : stages) {
        if (s.stale || s.view.telemetry.state.isBypassed) continue;
        if (s.trackStableId != prevTrack) {
            prevTrack = s.trackStableId;
            dryList.push_back(s.view.telemetry.inputSummary);
            wetList.push_back(s.view.telemetry.outputSummary);
        } else {
            // Same track: advance wet endpoint to this stage's output (latest in chain order).
            wetList.back() = s.view.telemetry.outputSummary;
        }
    }
    if (dryList.empty()) return {};
    SelectionSummary r;
    r.valid    = true;
    r.dry      = aggregateSummaries(dryList);
    // Analyser's input is the actual mixed wet signal — prefer it over per-track last outputs.
    r.wet      = analyserStage.has_value()
                    ? analyserStage->view.telemetry.inputSummary
                    : aggregateSummaries(wetList);
    r.title    = title;
    r.scopeKey = scopeKey;
    return r;
}

// Individual stage: dry = its input, wet = its output.
template <typename StageEntry>
SelectionSummary buildIndividualSummary(const StageEntry& stage) {
    SelectionSummary r;
    r.valid    = true;
    r.dry      = stage.view.telemetry.inputSummary;
    r.wet      = stage.view.telemetry.outputSummary;
    r.title    = displayStageName(stage.stageName);
    r.scopeKey = "stage:" + juce::String(static_cast<juce::int64>(stage.view.telemetry.identity.instanceId));
    return r;
}

// Multi-select: stages sorted by localOrderId, dry = earliest input, wet = latest output.
template <typename StageEntry>
SelectionSummary buildMultiSummary(const std::vector<const StageEntry*>& sel) {
    jassert(!sel.empty());
    SelectionSummary r;
    r.valid = true;
    r.dry   = sel.front()->view.telemetry.inputSummary;
    r.wet   = sel.back()->view.telemetry.outputSummary;
    r.title = juce::String((int) sel.size()) + " Stages";
    r.scopeKey = "multi";
    for (const auto* e : sel)
        r.scopeKey += ":" + juce::String(static_cast<juce::int64>(e->view.telemetry.identity.instanceId));
    return r;
}

// Analyser-only fallback: dry = wet = analyser's live input. Model is still valid.
template <typename StageEntry>
SelectionSummary buildAnalyserFallbackSummary(const StageEntry& analyserStage) {
    SelectionSummary r;
    r.valid        = true;
    r.analyserOnly = true;
    r.dry          = analyserStage.view.telemetry.inputSummary;
    r.wet          = analyserStage.view.telemetry.inputSummary;
    r.title        = "Live Input";
    r.scopeKey     = "dry-only";
    return r;
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

    const int confidencePercent = juce::roundToInt(100.0f * juce::jlimit(0.0f, 1.0f, quality.separationConfidence));
    return "Recording: " + stereoHint
        + "  |  Dynamics: " + dynamicsHint
        + "  |  Tone: " + tiltHint
        + "  |  DSP trust: " + confidenceHint + " (" + juce::String(confidencePercent) + "%)";
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

    diagnosticsToggleButton.setButtonText("Diagnostics >");
    diagnosticsToggleButton.onClick = [this] {
        diagnosticsExpanded = !diagnosticsExpanded;
        diagnosticsToggleButton.setButtonText(diagnosticsExpanded ? "Diagnostics v" : "Diagnostics >");
        diagnosticsEditor.setVisible(diagnosticsExpanded);
        resized();
        repaint();
    };
    addAndMakeVisible(diagnosticsToggleButton);

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

                // Coloured left pip
                g.setColour(headerAccent);
                g.fillRoundedRectangle(rowBounds.removeFromLeft(3.0f).reduced(0.0f, 4.0f).toFloat(), 1.5f);

                g.setColour(hasColour ? headerAccent : text.withAlpha(0.52f));
                g.setFont(juce::FontOptions().withHeight(11.0f).withKerningFactor(0.10f).withStyle("Bold"));
                const auto labelBounds = rowBounds.reduced(6.0f, 0.0f);
                g.drawText(row.stageName.toUpperCase(), labelBounds.toNearestInt(), juce::Justification::centredLeft, false);

                const float lineY = rowBounds.getCentreY();
                const float labelEnd = rowBounds.getX() + 8.0f
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
            auto top = content.removeFromTop(20.0f);
            const float statusWidth = std::min(88.0f, top.getWidth() * 0.28f);
            g.setColour(text.withAlpha(row.inactive ? 0.45f : 0.95f));
            g.setFont(juce::FontOptions().withHeight(16.0f).withStyle("Bold"));
            g.drawFittedText(row.stageName,
                             top.removeFromLeft(top.getWidth() - statusWidth).toNearestInt(),
                             juce::Justification::centredLeft,
                             1);
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
        const bool isBypassed = currentRenderModel.bypassed;
        auto plotRegion = plotBounds.toFloat().reduced(80.0f, 60.0f);
        if (plotRegion.getHeight() < 40.0f)
            return;
        auto upper = plotRegion.removeFromTop(plotRegion.getHeight() * 0.5f);
        g.setColour(text.withAlpha(0.84f));
        g.setFont(juce::FontOptions().withHeight(24.0f).withStyle("Bold"));
        g.drawFittedText(isBypassed ? "Analyser disabled" : "Waiting for live signal",
                         upper.toNearestInt(), juce::Justification::centredBottom, 1);
        g.setFont(juce::FontOptions().withHeight(14.0f));
        g.setColour(text.withAlpha(0.60f));
        g.drawFittedText(isBypassed
                             ? "Enable the plugin in the host to resume analysis."
                             : "Insert VX Studio Analyser last in the chain. It will show the dry baseline even before other VX stages join.",
                         plotRegion.withTrimmedTop(12.0f).toNearestInt(),
                         juce::Justification::centredTop,
                         2);
        return;
    }

    auto plot = plotBounds.toFloat().reduced(60.0f, 28.0f);
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

        // Key frequency lines  -  slightly stronger
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
                       juce::Justification::centred,
                       false);
        }

        for (float db : { -18.0f, -30.0f, -42.0f, -54.0f, -66.0f, -78.0f }) {
            const float y = juce::jmap(db, kSpectrumMinDb, kSpectrumMaxDb, plot.getBottom(), plot.getY());
            g.drawHorizontalLine(juce::roundToInt(y), plot.getX() - 5.0f, plot.getX());
            g.drawText(juce::String(db, 0) + " dB",
                       juce::Rectangle<float>(plot.getX() - 58.0f, y - 8.0f, 52.0f, 16.0f),
                       juce::Justification::centredRight,
                       false);
        }
    }

    g.setColour(text.withAlpha(0.45f));
    g.setFont(juce::FontOptions().withHeight(12.0f));
    g.drawFittedText("DSP v" + juce::String(processor.getProductIdentity().dspVersion.data())
                        + "   FW v" + juce::String(vxsuite::versions::framework.data())
                        + "    (c) Andrzej Marczewski 2026",
                     getLocalBounds().reduced(24, 18),
                     juce::Justification::bottomRight,
                     1);
}

void VXStudioAnalyserEditor::resized() {
    auto area = getLocalBounds().reduced(20, 18);
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
        stageRowLogicalIndices.clear();

        constexpr int kStageRowHeight = 58;
        constexpr int kHeaderRowHeight = 24;
        constexpr int kRowSpacing = 10;

        // Total scroll height accounts for mixed header/stage row sizes.
        int totalRowsHeight = 0;
        for (const auto& row : currentRenderModel.chainRows)
            totalRowsHeight += (row.isTrackHeader ? kHeaderRowHeight : kStageRowHeight) + kRowSpacing;
        maxChainScroll = std::max(0, totalRowsHeight - chainArea.getHeight());
        chainScrollOffset = std::min(chainScrollOffset, maxChainScroll);

        auto scrollableArea = chainArea.translated(0, -chainScrollOffset);
        for (std::size_t i = 0; i < currentRenderModel.chainRows.size(); ++i) {
            const int rowH = currentRenderModel.chainRows[i].isTrackHeader ? kHeaderRowHeight : kStageRowHeight;
            const auto rowBounds = scrollableArea.removeFromTop(rowH);
            if (rowBounds.intersects(chainArea)) {
                stageRowBounds.push_back(rowBounds);
                stageRowLogicalIndices.push_back(static_cast<int>(i));
            }
            scrollableArea.removeFromTop(kRowSpacing);
        }
    } else {
        fullChainButton.setBounds({});
        stageRowBounds.clear();
        stageRowLogicalIndices.clear();
        chainScrollOffset = 0;
        maxChainScroll = 0;
    }

    auto contentArea = contentBounds.reduced(22, 18);
    const bool compactControls = contentArea.getWidth() < 840;
    selectionLabel.setBounds(contentArea.removeFromTop(compactControls ? 36 : 34));
    const int summaryHeight = compactControls ? 96 : 82;
    summaryBounds = contentArea.removeFromTop(summaryHeight);
    summaryLabel.setBounds(summaryBounds.reduced(0, 2));
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
    if (diagnosticsExpanded)
        diagnosticsEditor.setBounds(diagnosticsBounds.reduced(8, 4));
    else
        diagnosticsBounds = {};
    contentArea.removeFromTop(8);
    plotBounds = contentArea.toNearestInt();
    applyTextFit();
}

void VXStudioAnalyserEditor::applyTextFit() {
    vxsuite::fitLabelFontToBounds(suiteLabel, 16.0f, 13.0f);
    vxsuite::fitLabelFontToBounds(titleLabel, 30.0f, 22.0f);
    vxsuite::fitLabelFontToBounds(subtitleLabel, 14.0f, 12.0f);
    vxsuite::fitLabelFontToBounds(recordingLabel, 13.0f, 11.0f);
    vxsuite::fitLabelFontToBounds(statusLabel, 12.5f, 11.0f);
    vxsuite::fitLabelFontToBounds(selectionLabel, 22.0f, 17.0f);
    vxsuite::fitLabelFontToBounds(summaryLabel, 13.5f, 11.5f);
    vxsuite::fitLabelFontToBounds(averageTimeLabel, 12.5f, 11.0f);
    vxsuite::fitLabelFontToBounds(smoothingLabel, 12.5f, 11.0f);
}

void VXStudioAnalyserEditor::mouseUp(const juce::MouseEvent& event) {
    const auto localPos = event.getEventRelativeTo(this).position.toInt();
    for (int index = 0; index < static_cast<int>(stageRowBounds.size()); ++index) {
        if (!stageRowBounds[static_cast<std::size_t>(index)].contains(localPos))
            continue;
        const int logicalIndex = index < static_cast<int>(stageRowLogicalIndices.size())
            ? stageRowLogicalIndices[static_cast<std::size_t>(index)]
            : index;
        if (logicalIndex < 0 || logicalIndex >= static_cast<int>(currentRenderModel.chainRows.size()))
            return;
        const auto& row = currentRenderModel.chainRows[static_cast<std::size_t>(logicalIndex)];
        if (row.isTrackHeader) {
            // Track header click: select all stages on that trackStableId.
            selectTrack(row.trackStableId);
        } else if (!row.inactive) {
            const auto instanceId = currentRenderModel.chainRowStageInstanceIds[static_cast<std::size_t>(logicalIndex)];
            const bool addToSelection = event.mods.isCommandDown() || event.mods.isCtrlDown();
            selectStage(instanceId, addToSelection);
        }
        return;
    }
}

void VXStudioAnalyserEditor::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) {
    const auto localPosition = event.getEventRelativeTo(this).position.toInt();
    if (!chainBounds.contains(localPosition))
        return;

    constexpr int kScrollStep = 30;
    int scrollDelta = wheel.deltaY > 0 ? -kScrollStep : kScrollStep;
    chainScrollOffset = juce::jlimit(0, maxChainScroll, chainScrollOffset + scrollDelta);
    resized();
    repaint();
}

void VXStudioAnalyserEditor::visibilityChanged() {
    if (isVisible()) {
        // When editor becomes visible: start timer and immediately refresh
        juce::Timer::startTimerHz(kUiRefreshHz);
        refreshRenderModel();
        applyPendingRenderModel();
    } else {
        // When editor is hidden: stop timer to avoid wasted 24 Hz scans
        juce::Timer::stopTimer();
    }
}

void VXStudioAnalyserEditor::timerCallback() {
    refreshRenderModel();
    applyPendingRenderModel();
}

void VXStudioAnalyserEditor::refreshRenderModel() {
    const auto nowMs = static_cast<std::uint64_t>(juce::Time::currentTimeMillis());

    // --- Registry scan ---
    std::vector<StageEntry> externalStages;
    std::optional<StageEntry> analyserStage;
    int diagTotal = 0, diagActive = 0, diagVx = 0, diagNonStale = 0;

    externalStages.reserve(vxsuite::analysis::StageRegistry::instance().maxSlots());

    for (int slotIndex = 0; slotIndex < vxsuite::analysis::StageRegistry::instance().maxSlots(); ++slotIndex) {
        vxsuite::analysis::StageView stage;
        if (!vxsuite::analysis::StageRegistry::instance().readStage(slotIndex, stage))
            continue;
        ++diagTotal;
        if (!stage.active) continue;
        ++diagActive;
        if (labelFromChars(stage.telemetry.identity.pluginFamily) != "VXSuite") continue;
        ++diagVx;
        const bool isStale = (nowMs - stage.telemetry.state.timestampMs) > kStaleThresholdMs;
        if (!isStale) ++diagNonStale;

        StageEntry entry;
        entry.view      = stage;
        entry.stageId   = labelFromChars(stage.telemetry.identity.stageId);
        entry.stageName = labelFromChars(stage.telemetry.identity.stageName);
        entry.stale     = isStale;

        // Track metadata — stableId drives grouping; name is display label only.
        {
            const auto info = vxsuite::analysis::StageRegistry::instance()
                                  .getTrackInfo(stage.telemetry.identity.instanceId);
            entry.trackStableId  = info.stableId;   // 0 = unknown track
            entry.trackIsReliable = info.isReliable;
            entry.trackName      = info.name;
            entry.trackColour    = info.colourARGB != 0 ? juce::Colour(info.colourARGB) : juce::Colour(0);
        }

        if (isStale) {
            entry.stateText = "Inactive";
            if (entry.stageId == processor.stageIdString())
                analyserStage = entry;
            continue;
        }

        entry.stateText = stage.telemetry.state.isBypassed ? "Bypassed"
                        : !stage.telemetry.state.isLive    ? "Inactive"
                        : stage.telemetry.state.isSilent   ? "Silent"
                                                           : "Active";

        std::array<float, vxsuite::analysis::kSummarySpectrumBins> stageDeltaDb {};
        float spectralSum = 0.0f, largestDelta = 0.0f;
        int largestBand = 0;
        for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
            const float d = toDb(stage.telemetry.outputSummary.spectrum[static_cast<std::size_t>(i)])
                          - toDb(stage.telemetry.inputSummary.spectrum[static_cast<std::size_t>(i)]);
            stageDeltaDb[static_cast<std::size_t>(i)] = d;
            spectralSum += std::abs(d);
            if (std::abs(d) > std::abs(largestDelta)) { largestDelta = d; largestBand = i; }
        }
        entry.spectralChange = spectralSum / static_cast<float>(vxsuite::analysis::kSummarySpectrumBins);
        entry.dynamicChange  = std::abs(toDb(stage.telemetry.outputSummary.rms, -120.0f)
                                         - toDb(stage.telemetry.inputSummary.rms, -120.0f));
        entry.stereoChange   = std::abs(stage.telemetry.outputSummary.stereoWidth - stage.telemetry.inputSummary.stereoWidth)
                             + std::abs(stage.telemetry.outputSummary.correlation  - stage.telemetry.inputSummary.correlation);
        entry.impactScore    = 0.45f * entry.spectralChange + 0.45f * entry.dynamicChange + 0.10f * entry.stereoChange;
        entry.impactText     = signedDb(juce::jlimit(-24.0f, 24.0f, largestDelta));
        const auto sparse    = classifySparseTone(stage.telemetry.inputSummary.spectrum,
                                                   stage.telemetry.outputSummary.spectrum,
                                                   stageDeltaDb);
        entry.typeLabel = sparse.sparse ? "Sparse" : classLabel(entry.spectralChange, entry.dynamicChange, entry.stereoChange);
        entry.freqHint  = "@" + formatFrequency(bandCenterHz(largestBand));

        if (entry.stageId == processor.stageIdString()) {
            analyserStage = entry;
            continue;
        }
        externalStages.push_back(std::move(entry));
    }

    // Scope filter — reliability-aware soft rejection:
    //   Only remove stages with a CONFIRMED different track (trackIsReliable && trackStableId != 0 && mismatch).
    //   Unknown-track stages (trackStableId == 0) and unreliable identities are always kept.
    //   If the analyser itself has no track ID yet, nothing is removed.
    const auto analyserDomain   = processor.analysisDomainId();
    const auto analyserTrack    = processor.trackStableId();
    const bool trackUnavailable = (analyserTrack == 0);
    const int diagPreFilter     = static_cast<int>(externalStages.size());
    int diagUnknownTrack = 0, diagKnownTrack = 0, diagMatchingTrack = 0, diagRejected = 0;
    for (const auto& s : externalStages) {
        if (s.trackStableId == 0)         ++diagUnknownTrack;
        else                              ++diagKnownTrack;
        if (!trackUnavailable && s.trackIsReliable && s.trackStableId != 0 && s.trackStableId == analyserTrack)
            ++diagMatchingTrack;
    }

    // Count domain matches for diagnostic purposes only.
    const int diagDomainMatch = static_cast<int>(std::count_if(externalStages.begin(), externalStages.end(),
        [analyserDomain](const StageEntry& s) { return s.view.analysisDomainId == analyserDomain; }));

    if (!trackUnavailable) {
        // We know which track the analyser is on. Remove only stages confirmed to be on a different track.
        externalStages.erase(
            std::remove_if(externalStages.begin(), externalStages.end(),
                [analyserTrack, &diagRejected](const StageEntry& s) {
                    if (s.trackIsReliable && s.trackStableId != 0 && s.trackStableId != analyserTrack) {
                        ++diagRejected;
                        return true;
                    }
                    return false;
                }),
            externalStages.end());
    }
    // If trackUnavailable: keep everything — we can't reliably exclude any stage.

    const int diagPostFilter = static_cast<int>(externalStages.size());

    // Sort by (localOrderId, instanceId) — all stages are now on the same track.
    std::stable_sort(externalStages.begin(), externalStages.end(), [](const StageEntry& a, const StageEntry& b) {
        if (a.view.telemetry.identity.localOrderId != b.view.telemetry.identity.localOrderId)
            return a.view.telemetry.identity.localOrderId < b.view.telemetry.identity.localOrderId;
        return a.view.telemetry.identity.instanceId < b.view.telemetry.identity.instanceId;
    });

    // Collect indices of active (non-stale, non-bypassed) stages for summary computation.
    std::vector<int> activeStageIndices;
    activeStageIndices.reserve(externalStages.size());
    for (int i = 0; i < static_cast<int>(externalStages.size()); ++i) {
        const auto& s = externalStages[static_cast<std::size_t>(i)];
        if (!s.stale && !s.view.telemetry.state.isBypassed)
            activeStageIndices.push_back(i);
    }

    // Validate selectedInstanceIds — remove any instance that no longer appears.
    {
        std::unordered_set<std::uint64_t> live;
        for (const auto& s : externalStages)
            live.insert(s.view.telemetry.identity.instanceId);
        for (auto it = selectedInstanceIds.begin(); it != selectedInstanceIds.end(); )
            it = live.count(*it) ? std::next(it) : selectedInstanceIds.erase(it);
        if (selectedInstanceIds.empty() && !fullChainSelectedValue)
            fullChainSelectedValue = true;
    }

    // Bypassed analyser — show nothing.
    if (analyserStage.has_value() && !analyserStage->stale
        && analyserStage->view.telemetry.state.isBypassed) {
        currentRenderModel = {};
        currentRenderModel.bypassed       = true;
        currentRenderModel.selectionTitle = "Analyser Disabled";
        currentRenderModel.statusText     = "Analyser is disabled";
        currentRenderModel.summaryLines   = { "Enable the plugin in the host to resume analysis.", "", "" };
        return;
    }

    RenderModel model;
    model.chainRows.reserve(externalStages.size() + 8);
    model.chainRowStageIndices.reserve(externalStages.size() + 8);
    model.chainRowStageInstanceIds.reserve(externalStages.size() + 8);

    // Build chain rows grouped by trackStableId. Headers carry the grouping key so
    // track-header clicks can call selectTrack(row.trackStableId) directly.
    std::uint64_t currentGroupId = ~std::uint64_t(0);
    for (int i = 0; i < static_cast<int>(externalStages.size()); ++i) {
        const auto& stage = externalStages[static_cast<std::size_t>(i)];
        if (stage.trackStableId != currentGroupId) {
            currentGroupId = stage.trackStableId;
            juce::String headerName = stage.trackName;
            if (headerName.isEmpty())
                headerName = (stage.trackStableId != 0)
                    ? ("Track " + juce::String::toHexString(static_cast<juce::int64>(stage.trackStableId)).substring(0, 6).toUpperCase())
                    : "Unknown Track";
            ChainRow hdr;
            hdr.stageName    = headerName;
            hdr.trackColour  = stage.trackColour;
            hdr.trackStableId = stage.trackStableId;
            hdr.isTrackHeader = true;
            model.chainRows.push_back(std::move(hdr));
            model.chainRowStageIndices.push_back(-1);
            model.chainRowStageInstanceIds.push_back(0);
        }
        const bool isInactive = stage.stale || stage.view.telemetry.state.isBypassed || !stage.view.telemetry.state.isLive;
        const bool isSelected = !isInactive && selectedInstanceIds.count(stage.view.telemetry.identity.instanceId) > 0;
        ChainRow row;
        row.stageName    = displayStageName(stage.stageName);
        row.stateText    = stage.stateText;
        row.impactText   = stage.impactText;
        row.typeLabel    = stage.typeLabel;
        row.freqHint     = stage.freqHint;
        row.trackName    = stage.trackName;
        row.trackColour  = stage.trackColour;
        row.trackStableId = stage.trackStableId;
        row.inactive     = isInactive;
        row.selected     = isSelected;
        model.chainRows.push_back(std::move(row));
        model.chainRowStageIndices.push_back(isInactive ? -1 : i);
        model.chainRowStageInstanceIds.push_back(stage.view.telemetry.identity.instanceId);
    }

    // --- Compute dry/wet SelectionSummary for the current selection mode ---
    SelectionSummary sel;

    if (activeStageIndices.empty()) {
        // No active external VX stages — show analyser's own input so the plot is never blank.
        if (analyserStage.has_value() && !analyserStage->stale)
            sel = buildAnalyserFallbackSummary(*analyserStage);
    } else if (fullChainSelectedValue) {
        const juce::String scopeKey = analyserTrack != 0
            ? "track:" + juce::String(static_cast<juce::int64>(analyserTrack))
            : "full";
        sel = buildChainSummary(externalStages, analyserStage, "Full Chain", scopeKey);
    } else if (!selectedInstanceIds.empty()) {
        // Gather only the selected active stages (externalStages already sorted by (trackStableId, localOrderId)).
        std::vector<const StageEntry*> picked;
        for (int i : activeStageIndices) {
            const auto& s = externalStages[static_cast<std::size_t>(i)];
            if (selectedInstanceIds.count(s.view.telemetry.identity.instanceId) > 0)
                picked.push_back(&s);
        }
        if (picked.size() == 1)
            sel = buildIndividualSummary(*picked.front());
        else if (!picked.empty())
            sel = buildMultiSummary(picked);
        else {
            // All selected stages became inactive — fall back to full chain.
            fullChainSelectedValue = true;
            sel = buildChainSummary(externalStages, analyserStage, "Full Chain", "full");
        }
    } else {
        fullChainSelectedValue = true;
        sel = buildChainSummary(externalStages, analyserStage, "Full Chain", "full");
    }

    model.valid        = sel.valid;
    model.analyserOnly = sel.analyserOnly;

    // Status text
    juce::String selLabel = fullChainSelectedValue ? "Full Chain"
        : selectedInstanceIds.size() == 1 ? sel.title
        : juce::String((int) selectedInstanceIds.size()) + " stages";
    const juce::String trackLabel = trackUnavailable
        ? "Awaiting track ID"
        : processor.trackDisplayName().isNotEmpty()
            ? ("Track: " + processor.trackDisplayName())
            : ("Track: " + juce::String::toHexString(static_cast<juce::int64>(analyserTrack)).substring(0, 6).toUpperCase());
    model.statusText =
        trackLabel
        + " | active: " + juce::String((int) activeStageIndices.size())
        + " | " + selLabel
        + (sel.analyserOnly ? " | No VX stages found" : "");

    model.selectionTitle = sel.valid
        ? ("Spectrum  |  " + (sel.title.isEmpty() ? "Full Chain" : sel.title))
        : "";

    // --- Spectrum smoothing pipeline (unchanged from previous) ---
    if (model.valid) {
        const auto& before = sel.dry;
        const auto& after  = sel.wet;

        const bool resetSmoothing = !backendState.initialized || backendState.selectionKey != sel.scopeKey;
        backendState.selectionKey = sel.scopeKey;
        backendState.initialized  = true;

        const float averageSeconds         = currentAverageTimeSeconds();
        const float deltaDisplaySeconds    = std::max(0.10f, averageSeconds * 0.75f);
        const float summarySmoothingSeconds = std::max(0.12f, averageSeconds * 0.60f);
        const int smoothingRadius          = currentSpectrumSmoothingRadius();

        auto smoothedBefore = model.beforeToneDb;
        auto smoothedAfter  = model.afterToneDb;
        auto smoothedDelta  = model.deltaToneDb;

        if (resetSmoothing) {
            backendState.spectrumHistory.clear();
            backendState.beforeToneLinearSum.fill(0.0f);
            backendState.afterToneLinearSum.fill(0.0f);
        }

        BackendState::SpectrumHistoryFrame frame;
        frame.timestampMs = nowMs;
        for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
            frame.beforeLinear[static_cast<std::size_t>(i)] = std::max(1.0e-6f, before.spectrum[static_cast<std::size_t>(i)]);
            frame.afterLinear[static_cast<std::size_t>(i)]  = std::max(1.0e-6f, after.spectrum[static_cast<std::size_t>(i)]);
            backendState.beforeToneLinearSum[static_cast<std::size_t>(i)] += frame.beforeLinear[static_cast<std::size_t>(i)];
            backendState.afterToneLinearSum[static_cast<std::size_t>(i)]  += frame.afterLinear[static_cast<std::size_t>(i)];
        }

        const auto windowMs = static_cast<std::uint64_t>(std::max(100.0f, averageSeconds * 1000.0f));
        while (static_cast<int>(backendState.spectrumHistory.size()) >= kMaxSpectrumHistoryFrames) {
            const auto& exp = backendState.spectrumHistory.front();
            for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
                backendState.beforeToneLinearSum[static_cast<std::size_t>(i)] -= exp.beforeLinear[static_cast<std::size_t>(i)];
                backendState.afterToneLinearSum[static_cast<std::size_t>(i)]  -= exp.afterLinear[static_cast<std::size_t>(i)];
            }
            backendState.spectrumHistory.pop_front();
        }
        backendState.spectrumHistory.push_back(frame);
        while (backendState.spectrumHistory.size() > 1
               && (nowMs - backendState.spectrumHistory.front().timestampMs) > windowMs) {
            const auto& exp = backendState.spectrumHistory.front();
            for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
                backendState.beforeToneLinearSum[static_cast<std::size_t>(i)] -= exp.beforeLinear[static_cast<std::size_t>(i)];
                backendState.afterToneLinearSum[static_cast<std::size_t>(i)]  -= exp.afterLinear[static_cast<std::size_t>(i)];
            }
            backendState.spectrumHistory.pop_front();
        }

        const float histScale = 1.0f / static_cast<float>(std::max<std::size_t>(1, backendState.spectrumHistory.size()));
        for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
            backendState.beforeToneLinear[static_cast<std::size_t>(i)] = std::max(1.0e-6f, backendState.beforeToneLinearSum[static_cast<std::size_t>(i)] * histScale);
            backendState.afterToneLinear[static_cast<std::size_t>(i)]  = std::max(1.0e-6f, backendState.afterToneLinearSum[static_cast<std::size_t>(i)]  * histScale);
        }

        float largestDeltaAcc = 0.0f;
        int   largestBandAcc  = 0;
        for (int i = 0; i < vxsuite::analysis::kSummarySpectrumBins; ++i) {
            const float hz      = bandCenterHz(i);
            const float bLin    = backendState.beforeToneLinear[static_cast<std::size_t>(i)];
            const float aLin    = backendState.afterToneLinear[static_cast<std::size_t>(i)];
            const float bDb     = juce::jlimit(kSpectrumMinDb, kSpectrumMaxDb, applyDisplaySlope(toDb(bLin, -120.0f), hz));
            const float aDb     = juce::jlimit(kSpectrumMinDb, kSpectrumMaxDb, applyDisplaySlope(toDb(aLin, -120.0f), hz));
            const float dTarget = juce::jlimit(-24.0f, 24.0f, toDb(aLin) - toDb(bLin));

            backendState.displayBeforeToneDb[static_cast<std::size_t>(i)] = bDb;
            backendState.displayAfterToneDb[static_cast<std::size_t>(i)]  = aDb;
            smoothedBefore[static_cast<std::size_t>(i)] = bDb;
            smoothedAfter[static_cast<std::size_t>(i)]  = aDb;

            if (resetSmoothing)
                backendState.deltaToneDb[static_cast<std::size_t>(i)] = dTarget;
            else
                backendState.deltaToneDb[static_cast<std::size_t>(i)] = smoothScalar(
                    backendState.deltaToneDb[static_cast<std::size_t>(i)], dTarget, deltaDisplaySeconds);

            const float displayTarget = std::abs(backendState.deltaToneDb[static_cast<std::size_t>(i)]) < 1.0f
                ? 0.0f : backendState.deltaToneDb[static_cast<std::size_t>(i)];
            if (resetSmoothing)
                backendState.displayDeltaToneDb[static_cast<std::size_t>(i)] = displayTarget;
            else
                backendState.displayDeltaToneDb[static_cast<std::size_t>(i)] = smoothScalar(
                    backendState.displayDeltaToneDb[static_cast<std::size_t>(i)], displayTarget, averageSeconds);
            smoothedDelta[static_cast<std::size_t>(i)] = backendState.displayDeltaToneDb[static_cast<std::size_t>(i)];

            if (hasMeaningfulBandEnergy(bLin, aLin)
                && std::abs(smoothedDelta[static_cast<std::size_t>(i)]) > std::abs(largestDeltaAcc)) {
                largestDeltaAcc = smoothedDelta[static_cast<std::size_t>(i)];
                largestBandAcc  = i;
            }
        }

        const auto sparseTone = classifySparseTone(backendState.beforeToneLinear, backendState.afterToneLinear, smoothedDelta);
        model.sparseTone = sparseTone.sparse;
        model.sparseToneBands = sparseTone.significantBands;

        if (model.sparseTone) {
            model.beforeToneDb = smoothedBefore;
            model.afterToneDb  = smoothedAfter;
            model.deltaToneDb  = smoothedDelta;
        } else {
            model.beforeToneDb = smoothNeighbourBins(smoothedBefore, smoothingRadius);
            model.afterToneDb  = smoothNeighbourBins(smoothedAfter,  smoothingRadius);
            model.deltaToneDb  = smoothNeighbourBins(smoothedDelta,  smoothingRadius);
        }

        model.largestToneBand = largestBandAcc;
        backendState.largestToneDeltaDb = resetSmoothing
            ? largestDeltaAcc
            : smoothScalar(backendState.largestToneDeltaDb, largestDeltaAcc, summarySmoothingSeconds);

        model.summaryLines = buildToneSummary(
            model.deltaToneDb, backendState.beforeToneLinear, backendState.afterToneLinear,
            model.largestToneBand, model.sparseTone, model.sparseToneBands,
            toDb(before.rms, -120.0f), toDb(after.rms, -120.0f));

        const juce::String trackReliability = trackUnavailable ? "unknown"
            : (!externalStages.empty() && diagMatchingTrack > 0) ? "confirmed"
            : !trackUnavailable ? "unconfirmed"
            : "unknown";
        model.diagnosticsText =
            "[Analyser]"
            + juce::String("\n  Domain: ") + juce::String(static_cast<juce::int64>(analyserDomain))
            + "\n  Track ID: " + (trackUnavailable ? "awaiting"
                                  : juce::String::toHexString(static_cast<juce::int64>(analyserTrack)).toUpperCase())
            + "\n  Track name: " + (processor.trackDisplayName().isEmpty() ? "(none)" : processor.trackDisplayName())
            + "\n  Track state: " + trackReliability
            + "\n[Filter]"
            + "\n  Total VX stages: "     + juce::String(diagPreFilter)
            + "\n  Unknown track: "       + juce::String(diagUnknownTrack)
            + "\n  Matching track: "      + juce::String(diagMatchingTrack)
            + "\n  Rejected (foreign): "  + juce::String(diagRejected)
            + "\n  Post-filter: "         + juce::String(diagPostFilter)
            + "\n  Domain match: "        + juce::String(diagDomainMatch)
            + "\n[Stages]"
            + "\n  Visible: "  + juce::String((int) externalStages.size())
            + "\n  Active: "   + juce::String((int) activeStageIndices.size())
            + "\n[Discovery]"
            + "\n  Total slots: "  + juce::String(diagTotal)
            + "\n  Active: "       + juce::String(diagActive)
            + "\n  VXSuite: "      + juce::String(diagVx)
            + "\n  Non-stale: "    + juce::String(diagNonStale)
            + "\n[Selection]"
            + "\n  Mode: " + (sel.analyserOnly ? "Analyser-only (no local VX stages)"
                               : fullChainSelectedValue ? "Full chain"
                               : selectedInstanceIds.size() == 1 ? "Single stage"
                               : "Multi-select (" + juce::String((int) selectedInstanceIds.size()) + ")")
            + "\n  Key: " + sel.scopeKey
            + "\n  Avg time: " + juce::String(averageSeconds, 2) + " s"
            + "\n  Smoothing: " + juce::String(kSmoothingOptions[static_cast<std::size_t>(
                  juce::jlimit(0, static_cast<int>(kSmoothingOptions.size()) - 1, smoothingIndex.load()))]);
    }

    currentRenderModel = std::move(model);
}

void VXStudioAnalyserEditor::applyPendingRenderModel() {
    recordingLabel.setText(signalQualityLabel(processor.getSignalQualitySnapshot()), juce::dontSendNotification);
    statusLabel.setText(currentRenderModel.statusText, juce::dontSendNotification);
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
    fullChainButton.setColour(juce::TextButton::buttonColourId,
                              fullChainSelectedValue ? colourFromRgb(processor.theme().accentRgb, 0.40f)
                                                     : juce::Colours::white.withAlpha(0.06f));
    std::uint64_t fp = 0;
    for (const auto id : currentRenderModel.chainRowStageInstanceIds)
        fp ^= id;
    if (fp != chainRowsFingerprint) {
        chainRowsFingerprint = fp;
        resized();
    }
}

void VXStudioAnalyserEditor::selectStage(const std::uint64_t instanceId, const bool addToSelection) {
    if (instanceId == 0) return;
    fullChainSelectedValue = false;
    if (addToSelection) {
        // Cmd/Ctrl-click: toggle this instance in the multi-select set.
        if (selectedInstanceIds.count(instanceId) > 0)
            selectedInstanceIds.erase(instanceId);
        else
            selectedInstanceIds.insert(instanceId);
        if (selectedInstanceIds.empty())
            fullChainSelectedValue = true;
    } else {
        selectedInstanceIds = { instanceId };
    }
    refreshRenderModel();
    applyPendingRenderModel();
}

void VXStudioAnalyserEditor::selectTrack(const std::uint64_t trackStableId) {
    // Track-header click: select all active stages on that trackStableId.
    fullChainSelectedValue = false;
    selectedInstanceIds.clear();
    for (std::size_t i = 0; i < currentRenderModel.chainRows.size(); ++i) {
        const auto& row = currentRenderModel.chainRows[i];
        if (!row.isTrackHeader && row.trackStableId == trackStableId && !row.inactive) {
            const auto id = currentRenderModel.chainRowStageInstanceIds[i];
            if (id != 0) selectedInstanceIds.insert(id);
        }
    }
    if (selectedInstanceIds.empty())
        fullChainSelectedValue = true;
    refreshRenderModel();
    applyPendingRenderModel();
}

void VXStudioAnalyserEditor::selectFullChain() {
    selectedInstanceIds.clear();
    fullChainSelectedValue = true;
    refreshRenderModel();
    applyPendingRenderModel();
}

void VXStudioAnalyserEditor::debugRefreshNow() {
    refreshRenderModel();
    applyPendingRenderModel();
}

int VXStudioAnalyserEditor::debugVisibleChainRowCount() const noexcept {
    int count = 0;
    for (const auto& row : currentRenderModel.chainRows)
        if (!row.isTrackHeader) ++count;
    return count;
}

juce::String VXStudioAnalyserEditor::debugChainRowStateText(const int index) const {
    int stageIndex = 0;
    for (const auto& row : currentRenderModel.chainRows) {
        if (row.isTrackHeader) continue;
        if (stageIndex == index)
            return row.stateText;
        ++stageIndex;
    }
    return {};
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

juce::Colour VXStudioAnalyserEditor::colourFromRgb(const std::array<float, 3>& rgb, const float alpha) const noexcept {
    return juce::Colour::fromFloatRGBA(rgb[0], rgb[1], rgb[2], alpha);
}
