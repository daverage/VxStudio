#pragma once

#include "../../framework/VxStudioHelpView.h"
#include "../../framework/VxStudioLookAndFeel.h"
#include "../../framework/VxStudioSpectrumTelemetry.h"
#include "../../framework/VxStudioUiHelpers.h"
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
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void visibilityChanged() override;

    void debugRefreshNow();
    [[nodiscard]] int debugVisibleChainRowCount() const noexcept;
    [[nodiscard]] juce::String debugChainRowStateText(int index) const;

private:
    struct ChainRow {
        juce::String stageName;
        juce::String stateText;
        juce::String impactText;
        juce::String typeLabel;
        juce::String freqHint;
        bool inactive = false;
        bool selected = false;
    };

    struct RenderModel {
        bool valid = false;
        bool bypassed = false;
        bool sparseTone = false;
        juce::String statusText;
        juce::String selectionTitle;
        std::array<juce::String, 3> summaryLines {};
        juce::String diagnosticsText;
        std::vector<ChainRow> chainRows;
        std::vector<int> chainRowStageIndices;
        std::vector<std::uint64_t> chainRowStageInstanceIds;
        std::vector<int> sparseToneBands;
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> beforeToneDb {};
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> afterToneDb {};
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> deltaToneDb {};
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
        float largestToneDeltaDb = 0.0f;
    };

    struct StageEntry {
        vxsuite::analysis::StageView view;
        juce::String stageId;
        juce::String stageName;
        juce::String stateText;
        juce::String impactText;
        juce::String typeLabel;
        juce::String freqHint;
        float spectralChange = 0.0f;
        float dynamicChange = 0.0f;
        float stereoChange = 0.0f;
        float impactScore = 0.0f;
        bool stale = false;
    };

    void timerCallback() override;
    void applyTextFit();
    void refreshRenderModel();
    void applyPendingRenderModel();
    void rebuildStageButtons();
    void selectStage(int index);
    void selectFullChain();
    [[nodiscard]] float currentAverageTimeSeconds() const noexcept;
    [[nodiscard]] int currentSpectrumSmoothingRadius() const noexcept;

    [[nodiscard]] juce::Path makeTonePath(const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& valuesDb,
                                          juce::Rectangle<float> bounds) const;
    [[nodiscard]] juce::Colour colourFromRgb(const std::array<float, 3>& rgb, float alpha = 1.0f) const noexcept;

    VXStudioAnalyserAudioProcessor& processor;
    vxsuite::SuiteLookAndFeel lookAndFeel;

    std::atomic<int> selectedStageIndex { -1 };
    std::atomic<std::uint64_t> selectedStageInstanceId { 0 };
    std::atomic<bool> fullChainSelected { true };
    std::atomic<int> averageTimeIndex { 3 };
    std::atomic<int> smoothingIndex { 3 };
    bool diagnosticsExpanded = false;
    bool chainCollapsed = false;
    std::size_t prevChainRowCount = 0;

    RenderModel currentRenderModel;
    BackendState backendState;

    juce::Label titleLabel;
    juce::Label suiteLabel;
    juce::Label subtitleLabel;
    juce::Label recordingLabel;
    juce::Label statusLabel;
    juce::Label selectionLabel;
    juce::Label summaryLabel;
    juce::Label averageTimeLabel;
    juce::Label smoothingLabel;
    vxsuite::HelpButton helpButton;
    juce::TextButton fullChainButton;
    juce::TextButton chainToggleButton;
    juce::TextButton diagnosticsToggleButton;
    juce::ComboBox averageTimeBox;
    juce::ComboBox smoothingBox;

    juce::Rectangle<int> chainBounds;
    juce::Rectangle<int> contentBounds;
    juce::Rectangle<int> summaryBounds;
    juce::Rectangle<int> plotBounds;
    juce::Rectangle<int> diagnosticsBounds;
    std::vector<juce::Rectangle<int>> stageRowBounds;
    std::vector<int> stageRowLogicalIndices;

    int chainScrollOffset = 0;
    int maxChainScroll = 0;
};
