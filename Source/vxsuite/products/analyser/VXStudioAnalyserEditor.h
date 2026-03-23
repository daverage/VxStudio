#pragma once

#include "../../framework/VxSuiteLookAndFeel.h"
#include "../../framework/VxSuiteSpectrumTelemetry.h"
#include "VXStudioAnalyserProcessor.h"

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

class VXStudioAnalyserEditor final : public juce::AudioProcessorEditor,
                                     private juce::Timer {
public:
    explicit VXStudioAnalyserEditor(VXStudioAnalyserAudioProcessor&);
    ~VXStudioAnalyserEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    enum class Tab {
        tone = 0,
        dynamics = 1
    };

    struct ChainRow {
        juce::String stageName;
        juce::String stateText;
        juce::String impactText;
        juce::String typeLabel;   // "Tone", "Dynamic", "Spatial", "Mixed", "Sparse"
        juce::String freqHint;    // "@1.2 kHz"
        bool selected = false;
    };

    struct RenderModel {
        bool valid = false;
        bool bypassed = false;
        bool sparseTone = false;
        bool snapshotFallback = false;
        juce::String statusText;
        juce::String selectionTitle;
        std::array<juce::String, 4> summaryLines {};
        juce::String diagnosticsText;
        std::vector<ChainRow> chainRows;
        std::vector<int> chainRowStageIndices;
        std::vector<int> sparseToneBands;
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> beforeToneDb {};
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> afterToneDb {};
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> deltaToneDb {};
        std::array<float, vxsuite::analysis::kSummaryEnvelopeBins> beforeDynamicsDb {};
        std::array<float, vxsuite::analysis::kSummaryEnvelopeBins> afterDynamicsDb {};
        int largestToneBand = 0;
    };

    struct BackendState {
        struct SpectrumHistoryFrame {
            std::uint64_t timestampMs = 0;
            std::array<float, vxsuite::analysis::kSummarySpectrumBins> beforeLinear {};
            std::array<float, vxsuite::analysis::kSummarySpectrumBins> afterLinear {};
        };

        bool initialized = false;
        juce::String selectionKey;
        std::deque<SpectrumHistoryFrame> spectrumHistory;
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> beforeToneLinearSum {};
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> afterToneLinearSum {};
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> beforeToneLinear {};
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> afterToneLinear {};
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> displayBeforeToneDb {};
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> displayAfterToneDb {};
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> displayDeltaToneDb {};
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> deltaToneDb {};
        std::array<float, vxsuite::analysis::kSummaryEnvelopeBins> beforeDynamicsLinear {};
        std::array<float, vxsuite::analysis::kSummaryEnvelopeBins> afterDynamicsLinear {};
        float peakDeltaDb = 0.0f;
        float rmsDeltaDb = 0.0f;
        float crestDeltaDb = 0.0f;
        float transientDelta = 0.0f;
        float largestToneDeltaDb = 0.0f;
        float lowToneDeltaDb = 0.0f;
        float midToneDeltaDb = 0.0f;
        float highToneDeltaDb = 0.0f;
    };

    struct StageEntry {
        vxsuite::analysis::StageView view;
        juce::String stageId;
        juce::String stageName;
        juce::String stateText;
        juce::String impactText;
        juce::String typeLabel;   // "Tone", "Dynamic", "Spatial", "Mixed", "Sparse"
        juce::String freqHint;    // "@1.2 kHz"
        float spectralChange = 0.0f;
        float dynamicChange = 0.0f;
        float stereoChange = 0.0f;
        float impactScore = 0.0f;
    };

    struct SidebarSnapshotCacheEntry {
        int order = 0;
        juce::String productName;
        juce::String displayName;
        juce::String canonicalKey;
        juce::String shortTag;
        bool silent = true;
        std::int64_t lastPublishMs = 0;
    };

    void timerCallback() override;
    void refreshRenderModel();
    void applyPendingRenderModel();
    void rebuildStageButtons();
    void selectStage(int index);
    void selectFullChain();
    void updateTabButtons();
    [[nodiscard]] float currentAverageTimeSeconds() const noexcept;
    [[nodiscard]] int currentSpectrumSmoothingRadius() const noexcept;

    [[nodiscard]] juce::Path makeTonePath(const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& valuesDb,
                                          juce::Rectangle<float> bounds) const;
    [[nodiscard]] juce::Path makeDynamicsPath(const std::array<float, vxsuite::analysis::kSummaryEnvelopeBins>& valuesDb,
                                              juce::Rectangle<float> bounds) const;
    [[nodiscard]] juce::Colour colourFromRgb(const std::array<float, 3>& rgb, float alpha = 1.0f) const noexcept;

    VXStudioAnalyserAudioProcessor& processor;
    vxsuite::SuiteLookAndFeel lookAndFeel;

    std::atomic<int> selectedStageIndex { -1 };
    std::atomic<bool> fullChainSelected { true };
    std::atomic<int> currentTabIndex { static_cast<int>(Tab::tone) };
    std::atomic<int> averageTimeIndex { 3 };
    std::atomic<int> smoothingIndex { 3 };
    bool diagnosticsExpanded = false;
    bool chainCollapsed = false;
    std::size_t prevChainRowCount = 0;

    RenderModel currentRenderModel;
    BackendState backendState;
    std::vector<SidebarSnapshotCacheEntry> sidebarSnapshotCache;

    juce::Label titleLabel;
    juce::Label suiteLabel;
    juce::Label subtitleLabel;
    juce::Label statusLabel;
    juce::Label selectionLabel;
    juce::Label summaryLabel;
    juce::Label averageTimeLabel;
    juce::Label smoothingLabel;
    juce::TextButton fullChainButton;
    juce::TextButton toneTabButton;
    juce::TextButton dynamicsTabButton;
    juce::TextButton chainToggleButton;
    juce::TextButton diagnosticsToggleButton;
    juce::ComboBox averageTimeBox;
    juce::ComboBox smoothingBox;

    juce::Rectangle<int> chainBounds;
    juce::Rectangle<int> contentBounds;
    juce::Rectangle<int> summaryBounds;
    juce::Rectangle<int> tabsBounds;
    juce::Rectangle<int> plotBounds;
    juce::Rectangle<int> diagnosticsBounds;
    std::vector<juce::Rectangle<int>> stageRowBounds;
};
