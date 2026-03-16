#pragma once

namespace vxcleaner::dsp {

/**
 * Common realtime processing policy shared by corrective DSP stages.
 * Individual processors consume only the fields relevant to their job.
 */
struct ProcessOptions {
    bool isVoiceMode = true;
    float voiceProtect = 0.75f;
    float sourceProtect = 0.75f;
    float lateTailAggression = 0.55f;
    float stereoWidthProtect = 0.75f;
    float guardStrictness = 0.75f;
    float speechFocus = 0.75f;
    bool learningActive = false;
    float subtract = 0.0f;
    float sensitivity = 0.0f;
    bool isPrimary = true;
    bool labRawMode = false;
};

} // namespace vxcleaner::dsp
