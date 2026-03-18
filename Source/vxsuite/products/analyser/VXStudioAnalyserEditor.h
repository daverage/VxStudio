#pragma once

#include "../../framework/VxSuiteSpectrumTelemetry.h"
#include "VXStudioAnalyserProcessor.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

class VXStudioAnalyserEditor final : public juce::AudioProcessorEditor,
                                     private juce::Timer,
                                     private juce::HighResolutionTimer {
public:
    explicit VXStudioAnalyserEditor(VXStudioAnalyserAudioProcessor&);
    ~VXStudioAnalyserEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    enum class Tab {
        tone = 0,
        dynamics = 1
    };

    struct ChainRow {
        juce::String stageName;
        juce::String stateText;
        juce::String impactText;
        juce::String classText;
        bool selected = false;
    };

    struct RenderModel {
        bool valid = false;
        std::uint64_t generation = 0;
        juce::String statusText;
        juce::String selectionTitle;
        std::array<juce::String, 4> summaryLines {};
        juce::String diagnosticsText;
        std::vector<ChainRow> chainRows;
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> beforeToneDb {};
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> afterToneDb {};
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> deltaToneDb {};
        std::array<float, vxsuite::analysis::kSummaryEnvelopeBins> beforeDynamicsDb {};
        std::array<float, vxsuite::analysis::kSummaryEnvelopeBins> afterDynamicsDb {};
        int largestToneBand = 0;
    };

    struct BackendState {
        bool initialized = false;
        juce::String selectionKey;
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> beforeToneLinear {};
        std::array<float, vxsuite::analysis::kSummarySpectrumBins> afterToneLinear {};
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
        juce::String classText;
        float spectralChange = 0.0f;
        float dynamicChange = 0.0f;
        float stereoChange = 0.0f;
        float impactScore = 0.0f;
    };

    void timerCallback() override;
    void hiResTimerCallback() override;
    void refreshRenderModel();
    void applyPendingRenderModel();
    void rebuildStageButtons();
    void selectStage(int index);
    void selectFullChain();
    void updateTabButtons();

    [[nodiscard]] juce::Path makeTonePath(const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& valuesDb,
                                          juce::Rectangle<float> bounds) const;
    [[nodiscard]] juce::Path makeDynamicsPath(const std::array<float, vxsuite::analysis::kSummaryEnvelopeBins>& valuesDb,
                                              juce::Rectangle<float> bounds) const;
    [[nodiscard]] juce::Colour colourFromRgb(const std::array<float, 3>& rgb, float alpha = 1.0f) const noexcept;

    VXStudioAnalyserAudioProcessor& processor;

    std::atomic<int> selectedStageIndex { -1 };
    std::atomic<bool> fullChainSelected { true };
    std::atomic<int> currentTabIndex { static_cast<int>(Tab::tone) };
    bool diagnosticsExpanded = false;

    juce::SpinLock renderModelLock;
    RenderModel pendingRenderModel;
    std::atomic<std::uint64_t> pendingGeneration { 0 };
    std::uint64_t appliedGeneration = 0;
    std::uint64_t renderGeneration = 0;
    RenderModel currentRenderModel;
    BackendState backendState;

    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label statusLabel;
    juce::Label selectionLabel;
    juce::Label summaryLabel;
    juce::TextButton fullChainButton;
    juce::TextButton toneTabButton;
    juce::TextButton dynamicsTabButton;
    juce::TextButton diagnosticsToggleButton;
    std::vector<std::unique_ptr<juce::TextButton>> stageButtons;

    juce::Rectangle<int> chainBounds;
    juce::Rectangle<int> contentBounds;
    juce::Rectangle<int> summaryBounds;
    juce::Rectangle<int> tabsBounds;
    juce::Rectangle<int> plotBounds;
    juce::Rectangle<int> diagnosticsBounds;
};
