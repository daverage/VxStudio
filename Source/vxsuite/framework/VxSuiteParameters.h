#pragma once

#include "VxSuiteModePolicy.h"
#include "VxSuiteProduct.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace vxsuite {

inline juce::String toJuceString(std::string_view text) {
    return juce::String(text.data(), static_cast<int>(text.size()));
}

inline juce::AudioParameterChoiceAttributes makeModeAttributes() {
    juce::AudioParameterChoiceAttributes attrs;
    attrs = attrs.withLabel("Mode");
    return attrs;
}

inline juce::AudioParameterBoolAttributes makeListenAttributes() {
    juce::AudioParameterBoolAttributes attrs;
    attrs = attrs.withLabel("Listen");
    return attrs;
}

inline juce::StringArray makeModeChoiceLabels() {
    return juce::StringArray {
        toJuceString(kVocalModePolicy.label),
        toJuceString(kGeneralModePolicy.label)
    };
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createSimpleParameterLayout(const ProductIdentity& identity) {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    if (identity.supportsModeSwitch()) {
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { identity.modeParamId.data(), 1 },
            "Mode",
            makeModeChoiceLabels(),
            static_cast<int>(identity.defaultMode),
            makeModeAttributes()));
    }
    if (identity.supportsListenMode()) {
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { identity.listenParamId.data(), 1 },
            "Listen",
            false,
            makeListenAttributes()));
    }
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { identity.primaryParamId.data(), 1 },
        toJuceString(identity.primaryLabel),
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.5f,
        juce::AudioParameterFloatAttributes().withLabel(identity.primaryLabel.data())));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { identity.secondaryParamId.data(), 1 },
        toJuceString(identity.secondaryLabel),
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        0.5f,
        juce::AudioParameterFloatAttributes().withLabel(identity.secondaryLabel.data())));
    if (identity.supportsTertiaryControl()) {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { identity.tertiaryParamId.data(), 1 },
            toJuceString(identity.tertiaryLabel),
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
            0.5f,
            juce::AudioParameterFloatAttributes().withLabel(identity.tertiaryLabel.data())));
    }
    if (identity.supportsLowShelfToggle()) {
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { identity.lowShelfParamId.data(), 1 },
            "Low Shelf", true));
    }
    if (identity.supportsHighShelfToggle()) {
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { identity.highShelfParamId.data(), 1 },
            "High Shelf", true));
    }
    return layout;
}

inline Mode readMode(const juce::AudioProcessorValueTreeState& state, const ProductIdentity& identity) {
    if (!identity.supportsModeSwitch())
        return identity.defaultMode;
    if (const auto* raw = state.getRawParameterValue(identity.modeParamId.data()))
        return raw->load() < 0.5f ? Mode::vocal : Mode::general;
    return identity.defaultMode;
}

inline const ModePolicy& readModePolicy(const juce::AudioProcessorValueTreeState& state, const ProductIdentity& identity) {
    return policyForMode(readMode(state, identity));
}

inline float readNormalized(const juce::AudioProcessorValueTreeState& state, std::string_view paramId, float fallback = 0.0f) {
    if (const auto* raw = state.getRawParameterValue(paramId.data()))
        return juce::jlimit(0.0f, 1.0f, raw->load());
    return fallback;
}

inline bool readBool(const juce::AudioProcessorValueTreeState& state, std::string_view paramId, bool fallback = false) {
    if (const auto* raw = state.getRawParameterValue(paramId.data()))
        return raw->load() >= 0.5f;
    return fallback;
}

} // namespace vxsuite
