#pragma once

#include "VxSuiteModePolicy.h"
#include "VxSuiteProduct.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace vxsuite {

inline juce::String toJuceString(std::string_view text) {
    return juce::String(text.data(), static_cast<int>(text.size()));
}

inline juce::AudioParameterChoiceAttributes makeChoiceAttributes(std::string_view label = "Mode") {
    juce::AudioParameterChoiceAttributes attrs;
    attrs = attrs.withLabel(label.data());
    return attrs;
}

inline juce::AudioParameterBoolAttributes makeListenAttributes() {
    juce::AudioParameterBoolAttributes attrs;
    attrs = attrs.withLabel("Listen");
    return attrs;
}

inline juce::AudioParameterBoolAttributes makeBypassAttributes(std::string_view label) {
    juce::AudioParameterBoolAttributes attrs;
    attrs = attrs.withLabel(label.data());
    return attrs;
}

inline juce::AudioParameterBoolAttributes makeLearnAttributes(std::string_view label) {
    juce::AudioParameterBoolAttributes attrs;
    attrs = attrs.withLabel(label.data());
    return attrs;
}

inline juce::StringArray makeModeChoiceLabels() {
    return juce::StringArray {
        toJuceString(kVocalModePolicy.label),
        toJuceString(kGeneralModePolicy.label)
    };
}

inline juce::StringArray makeSelectorChoiceLabels(const ProductIdentity& identity) {
    const auto first = identity.selectorChoiceLabel(0);
    const auto second = identity.selectorChoiceLabel(1);
    if (!first.empty() && !second.empty()) {
        return juce::StringArray {
            toJuceString(first),
            toJuceString(second)
        };
    }
    return makeModeChoiceLabels();
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createSimpleParameterLayout(const ProductIdentity& identity) {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    if (identity.supportsModeSwitch()) {
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { identity.modeParamId.data(), 1 },
            toJuceString(identity.selectorLabel.empty() ? "Mode" : identity.selectorLabel),
            makeSelectorChoiceLabels(identity),
            static_cast<int>(identity.defaultMode),
            makeChoiceAttributes(identity.selectorLabel.empty() ? "Mode" : identity.selectorLabel)));
    }
    if (identity.supportsListenMode()) {
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { identity.listenParamId.data(), 1 },
            "Listen",
            false,
            makeListenAttributes()));
    }
    if (identity.supportsLearnButton()) {
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { identity.learnParamId.data(), 1 },
            toJuceString(identity.learnButtonLabel.empty() ? "Learn" : identity.learnButtonLabel),
            false,
            makeLearnAttributes(identity.learnButtonLabel.empty() ? "Learn" : identity.learnButtonLabel)));
    }
    if (!identity.lowShelfParamId.empty()) {
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { identity.lowShelfParamId.data(), 1 },
            "Low Shelf",
            identity.defaultLowShelf,
            makeBypassAttributes("Low Shelf")));
    }
    if (!identity.highShelfParamId.empty()) {
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { identity.highShelfParamId.data(), 1 },
            "High Shelf",
            identity.defaultHighShelf,
            makeBypassAttributes("High Shelf")));
    }
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { identity.primaryParamId.data(), 1 },
        toJuceString(identity.primaryLabel),
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        identity.primaryDefaultValue,
        juce::AudioParameterFloatAttributes().withLabel(identity.primaryLabel.data())));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { identity.secondaryParamId.data(), 1 },
        toJuceString(identity.secondaryLabel),
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        identity.secondaryDefaultValue,
        juce::AudioParameterFloatAttributes().withLabel(identity.secondaryLabel.data())));
    if (identity.supportsTertiaryControl()) {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { identity.tertiaryParamId.data(), 1 },
            toJuceString(identity.tertiaryLabel),
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
            identity.tertiaryDefaultValue,
            juce::AudioParameterFloatAttributes().withLabel(identity.tertiaryLabel.data())));
    }
    if (identity.supportsQuaternaryControl()) {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { identity.quaternaryParamId.data(), 1 },
            toJuceString(identity.quaternaryLabel),
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
            identity.quaternaryDefaultValue,
            juce::AudioParameterFloatAttributes().withLabel(identity.quaternaryLabel.data())));
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
