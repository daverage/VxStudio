#pragma once

#include "../../framework/VxStudioHelpView.h"
#include "../../framework/VxStudioLookAndFeel.h"
#include "../../framework/VxStudioSpectrumTelemetry.h"
#include "../../framework/VxStudioUiHelpers.h"
#include "VXStudioAnalyserProcessor.h"
#include "VXStudioAnalyserController.h"

#include <array>
#include <atomic>
#include <memory>

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
    void timerCallback() override;
    void applyTextFit();
    void refreshFromController();
    void applyPendingRenderModel();
    void rebuildStageButtons();

    [[nodiscard]] float currentAverageTimeSeconds() const noexcept;
    [[nodiscard]] int   currentSpectrumSmoothingRadius() const noexcept;

    [[nodiscard]] juce::Path makeTonePath(
        const std::array<float, vxsuite::analysis::kSummarySpectrumBins>& valuesDb,
        juce::Rectangle<float> bounds) const;
    [[nodiscard]] juce::Colour colourFromRgb(const std::array<float, 3>& rgb,
                                              float alpha = 1.0f) const noexcept;

    VXStudioAnalyserAudioProcessor& processor;
    vxsuite::SuiteLookAndFeel       lookAndFeel;

    // Controller: owns all business logic, produces RenderModel.
    vxanalyser::AnalyserController controller;

    std::atomic<int> averageTimeIndex { 3 };
    std::atomic<int> smoothingIndex   { 3 };
    bool diagnosticsExpanded = false;
    bool chainCollapsed      = false;

    std::uint64_t chainRowsFingerprint = 0;

    // Local copy of the model produced by the controller.
    vxanalyser::RenderModel currentRenderModel;

    juce::Label     titleLabel;
    juce::Label     suiteLabel;
    juce::Label     subtitleLabel;
    juce::Label     recordingLabel;
    juce::Label     statusLabel;
    juce::Label     selectionLabel;
    juce::Label     summaryLabel;
    juce::Label     averageTimeLabel;
    juce::Label     smoothingLabel;
    vxsuite::HelpButton helpButton;
    juce::TextButton fullChainButton;
    juce::TextButton chainToggleButton;
    juce::TextButton refreshButton;
    juce::TextButton diagnosticsToggleButton;
    juce::TextButton copyDiagnosticsButton;
    juce::ComboBox  averageTimeBox;
    juce::ComboBox  smoothingBox;
    juce::TextEditor diagnosticsEditor;

    juce::Rectangle<int> chainBounds;
    juce::Rectangle<int> contentBounds;
    juce::Rectangle<int> summaryBounds;
    juce::Rectangle<int> plotBounds;
    juce::Rectangle<int> diagnosticsBounds;
    std::vector<juce::Rectangle<int>> stageRowBounds;
    std::vector<int>                  stageRowLogicalIndices;

    int chainScrollOffset = 0;
    int maxChainScroll    = 0;
    std::uint64_t lastObservedChainFingerprint = 0;
};
