#pragma once

#include "VxSuiteSpectrumTelemetry.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace vxsuite {

class LevelTraceView final : public juce::Component {
public:
    explicit LevelTraceView(const ProductTheme& theme);

    void setSnapshot(const spectrum::SnapshotView& snapshot);
    void setUnavailable();
    void setZoomSeconds(float seconds);
    [[nodiscard]] float zoomSeconds() const noexcept { return zoomSecondsValue; }

    void paint(juce::Graphics&) override;

private:
    static juce::Colour accentFromTheme(const ProductTheme& theme) noexcept;
    static float levelToDb(float linear) noexcept;
    float sampleAt(const std::array<float, spectrum::kLevelTraceSamples>& values,
                   int index) const noexcept;

    juce::Colour accent;
    bool available = false;
    float totalSeconds = 0.0f;
    int traceCount = 0;
    float dryRms = 0.0f;
    float wetRms = 0.0f;
    float zoomSecondsValue = 6.0f;
    std::array<float, spectrum::kLevelTraceSamples> dryTrace {};
    std::array<float, spectrum::kLevelTraceSamples> wetTrace {};
};

} // namespace vxsuite
