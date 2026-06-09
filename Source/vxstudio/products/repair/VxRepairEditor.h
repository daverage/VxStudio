#pragma once

#include "VxRepairProcessor.h"
#include "../../framework/VxStudioHelpView.h"
#include "../../framework/VxStudioLookAndFeel.h"

#include <array>
#include <memory>

class VXRepairEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer {
public:
    explicit VXRepairEditor(VXRepairAudioProcessor&);
    ~VXRepairEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    void paintIdle(juce::Graphics&);
    void paintCollecting(juce::Graphics&);
    void paintRepair(juce::Graphics&);

    void buildRepairRows();
    void showRepairState();
    void showIdleState();

    int scaled(int v) const noexcept { return static_cast<int>(static_cast<float>(v) * uiScale); }

    // ── State enum ────────────────────────────────────────────────────────────
    enum class UIState { Idle, Collecting, Repair };
    UIState uiState = UIState::Idle;

    // ── Look-and-feel + scale ─────────────────────────────────────────────────
    vxsuite::SuiteLookAndFeel laf;
    float uiScale = 1.0f;

    // ── Header labels ─────────────────────────────────────────────────────────
    juce::Label suiteLabel;
    juce::Label productLabel;
    juce::Label statusLabel;

    // ── Idle / collecting widgets ─────────────────────────────────────────────
    juce::TextButton analyseButton;
    juce::Label      promptLabel;
    juce::Label      collectingLabel;
    juce::Label      phaseLabel;

    // ── Per-tool rows (4 tools) ───────────────────────────────────────────────
    struct ToolRow {
        juce::Label     nameLabel;
        juce::Label     confidenceLabel;
        juce::Slider    strengthSlider;
        juce::Label     strengthLabel;
        juce::TextButton listenButton;
        juce::TextButton bypassButton;

        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>  strengthAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>  listenAttach;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>  bypassAttach;
    };

    std::array<ToolRow, 4> rows;

    // ── Noise row DeepFilter toggle (row 0 only) ──────────────────────────────
    juce::ToggleButton deepFilterToggle;
    juce::Label        deepFilterStatus;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> deepFilterAttach;

    // ── Makeup gain ──────────────────────────────────────────────────────────
    juce::Slider makeupSlider;
    juce::Label  makeupLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> makeupAttach;

    // ── Footer ────────────────────────────────────────────────────────────────
    juce::TextButton  resetButton;
    vxsuite::HelpButton helpButton;

    VXRepairAudioProcessor& repairProcessor;
    vxsuite::repair::RepairAssessment lastAssessment;

    // Live display data updated each timer tick from the audio thread
    std::array<float, 24> smoothedBands {};
    float noiseActivityDisplay   = 0.0f;
    float clarityActivityDisplay = 0.0f;
    float clickActivityDisplay   = 0.0f;
    float reverbActivityDisplay  = 0.0f;
};
