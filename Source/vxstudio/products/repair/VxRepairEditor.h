#pragma once

#include "VxRepairProcessor.h"
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

    // ── Per-tool rows (3 tools) ───────────────────────────────────────────────
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

    std::array<ToolRow, 3> rows;

    // ── Makeup gain ──────────────────────────────────────────────────────────
    juce::Slider makeupSlider;
    juce::Label  makeupLabel;
    juce::Label  makeupHint;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> makeupAttach;

    // ── Footer ────────────────────────────────────────────────────────────────
    juce::TextButton resetButton;
    juce::Label      deepFilterNote;

    VXRepairAudioProcessor& repairProcessor;
    vxsuite::repair::RepairAssessment lastAssessment;
};
