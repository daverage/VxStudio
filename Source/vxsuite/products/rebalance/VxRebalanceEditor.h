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
    bool diagnosticsExpanded = false;
    float uiScale = 1.0f;
};
