#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace vxsuite {
class EditorBase;
}

class VXRebalanceAudioProcessor;

class VXRebalanceEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer {
public:
    explicit VXRebalanceEditor(VXRebalanceAudioProcessor&);
    ~VXRebalanceEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void setScaleFactor(float newScale) override;

private:
    class DebugPanel;

    void timerCallback() override;
    void updateLayout();

    VXRebalanceAudioProcessor& processor;
    std::unique_ptr<vxsuite::EditorBase> mainEditor;
    std::unique_ptr<DebugPanel> debugPanel;
    juce::TextButton diagnosticsToggleButton;
#if VXSTUDIO_REBALANCE_AI_VARIANT
    juce::Label aiModeLabel;
    juce::ComboBox aiModeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aiModeAttachment;
#endif
    bool diagnosticsExpanded = false;
    float uiScale = 1.0f;
};
