#pragma once

#include "VxStudioProduct.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace vxsuite {

// Scrolling pitch trace: detected pitch (dim) and rendered/corrected pitch
// (accent) in cents against a semitone grid with note names. Push-model like
// LevelTraceView's gain overlay: the editor feeds one sample per UI tick from
// the ProcessorBase pitch-trace virtuals; confidence <= 0 marks an unvoiced
// gap and breaks the lines.
class PitchTraceView final : public juce::Component {
public:
    explicit PitchTraceView(const ProductTheme& theme);

    void pushSample(float detectedCents, float correctedCents, float confidence);
    void setZoomSeconds(float seconds);
    void setZoomCents(float halfSpanCents);

    void paint(juce::Graphics&) override;

private:
    struct Sample {
        double timeMs = 0.0;
        float detectedCents = 0.0f;
        float correctedCents = 0.0f;
        float confidence = 0.0f;   // <= 0: unvoiced
    };

    static juce::String noteNameForCents(float cents);

    juce::Colour accent;
    juce::Colour background;
    juce::Colour text;

    static constexpr int kCapacity = 1024;
    std::array<Sample, kCapacity> samples {};
    int writeIndex = 0;
    int count = 0;
    float zoomSecondsValue = 6.0f;
    float halfSpanCentsValue = 350.0f;

    // Fixed-span window that follows the voice: melodies span octaves, and
    // fitting the whole visible range crushes cent-level detail to nothing.
    float viewCentreCents = 0.0f;
    bool viewCentreValid = false;
    double lastPaintMs = 0.0;
};

} // namespace vxsuite
