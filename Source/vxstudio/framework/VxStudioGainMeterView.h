#pragma once

#include "VxStudioProduct.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace vxsuite {

class GainMeterView final : public juce::Component {
public:
    explicit GainMeterView(const ProductTheme& theme);

    // Call at 12 Hz with linear peak values (0–1+). Updates bar level
    // (fast decay) and peak hold (2 s hold then slow fall).
    void setLevels(float inL, float inR, float outL, float outR) noexcept;

    void paint(juce::Graphics&) override;

private:
    struct ChannelState {
        float dispLevel  = 0.0f;
        float holdLevel  = 0.0f;
        int   holdTicks  = 0;
        float peakDb     = -100.0f;
    };

    static void updateChannel(ChannelState& ch, float newLinear) noexcept;
    static float linearToNorm(float linear) noexcept;

    void drawBar(juce::Graphics& g,
                 juce::Rectangle<float> slot,
                 float normLevel, float normHold,
                 juce::Colour colour) const noexcept;

    void drawScale(juce::Graphics& g,
                   juce::Rectangle<float> scaleSlot,
                   juce::Rectangle<float> barSlot,
                   juce::Justification just) const noexcept;

    juce::Colour accentColour;
    juce::Colour inputColour;
    ChannelState chInL, chInR, chOutL, chOutR;

    static constexpr int   kSegments    = 60;
    static constexpr int   kYellowStart = 48;   // -12 dBFS
    static constexpr int   kRedStart    = 54;   // -6 dBFS
    static constexpr float kSegGap      = 1.0f;
};

} // namespace vxsuite
