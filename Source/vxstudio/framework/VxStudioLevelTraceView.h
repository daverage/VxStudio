#pragma once

#include "VxStudioSpectrumTelemetry.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace vxsuite {

class LevelTraceView final : public juce::Component {
public:
    explicit LevelTraceView(const ProductTheme& theme);

    void setSnapshot(const spectrum::SnapshotView& snapshot);
    void setUnavailable();
    // Feed the optional gain-trace overlay (one sample per UI tick).
    void pushGainSample(float gainDb, float openness, bool enabled);
    // Feed the optional reference line (dBFS; enabled = draw it).
    void setReferenceDb(float referenceDb, bool enabled);
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

    struct GainSample {
        double timeMs = 0.0;
        float gainDb = 0.0f;
        float openness = 1.0f;
    };
    static constexpr int kGainTraceSamples = 512;
    bool gainTraceEnabled = false;
    int gainTraceWriteIndex = 0;
    int gainTraceCount = 0;
    std::array<GainSample, kGainTraceSamples> gainTrace {};
    bool referenceEnabled = false;
    float referenceDb = -100.0f;
};

} // namespace vxsuite
