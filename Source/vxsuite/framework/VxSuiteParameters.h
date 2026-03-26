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

inline juce::AudioParameterFloatAttributes makePercentFloatAttributes() {
    juce::AudioParameterFloatAttributes attrs;
    attrs = attrs.withLabel("%")
                 .withStringFromValueFunction([](const float value, [[maybe_unused]] const int maximumStringLength) {
                     juce::ignoreUnused(maximumStringLength);
                     const float percent = juce::jlimit(0.0f, 100.0f, value * 100.0f);
                     return juce::String(percent, percent < 10.0f ? 1 : 0) + "%";
                 })
                 .withValueFromStringFunction([](const juce::String& text) {
                     auto trimmed = text.trim();
                     trimmed = trimmed.upToFirstOccurrenceOf("%", false, false).trim();
                     return juce::jlimit(0.0f, 1.0f, trimmed.getFloatValue() / 100.0f);
                 });
    return attrs;
}

inline juce::AudioParameterFloatAttributes makeUnityGainPercentAttributes() {
    juce::AudioParameterFloatAttributes attrs;
    attrs = attrs.withLabel("%")
                 .withStringFromValueFunction([](const float value, [[maybe_unused]] const int maximumStringLength) {
                     juce::ignoreUnused(maximumStringLength);
                     const float percent = juce::jlimit(50.0f, 150.0f, 50.0f + value * 100.0f);
                     return juce::String(percent, 0) + "%";
                 })
                 .withValueFromStringFunction([](const juce::String& text) {
                     auto trimmed = text.trim();
                     trimmed = trimmed.upToFirstOccurrenceOf("%", false, false).trim();
                     return juce::jlimit(0.0f, 1.0f, (trimmed.getFloatValue() - 50.0f) / 100.0f);
                 });
    return attrs;
}

inline juce::AudioParameterFloatAttributes makeCenteredPercentFloatAttributes() {
    juce::AudioParameterFloatAttributes attrs;
    attrs = attrs.withLabel("%")
                 .withStringFromValueFunction([](const float value, [[maybe_unused]] const int maximumStringLength) {
                     juce::ignoreUnused(maximumStringLength);
                     const float centered = juce::jlimit(-100.0f, 100.0f, (value - 0.5f) * 200.0f);
                     const juce::String prefix = centered > 0.05f ? "+" : "";
                     return prefix + juce::String(centered, std::abs(centered) < 10.0f ? 1 : 0) + "%";
                 })
                 .withValueFromStringFunction([](const juce::String& text) {
                     auto trimmed = text.trim();
                     trimmed = trimmed.upToFirstOccurrenceOf("%", false, false).trim();
                     const float centered = juce::jlimit(-100.0f, 100.0f, trimmed.getFloatValue());
                     return juce::jlimit(0.0f, 1.0f, centered / 200.0f + 0.5f);
                 });
    return attrs;
}

inline juce::AudioParameterFloatAttributes makeCenteredDecibelFloatAttributes(const float maxAbsDb) {
    juce::AudioParameterFloatAttributes attrs;
    attrs = attrs.withLabel("dB")
                 .withStringFromValueFunction([maxAbsDb](const float value, [[maybe_unused]] const int maximumStringLength) {
                     juce::ignoreUnused(maximumStringLength);
                     const float db = juce::jlimit(-maxAbsDb, maxAbsDb, (value - 0.5f) * 2.0f * maxAbsDb);
                     const juce::String prefix = db > 0.05f ? "+" : "";
                     return prefix + juce::String(db, std::abs(db) < 10.0f ? 1 : 0) + " dB";
                 })
                 .withValueFromStringFunction([maxAbsDb](const juce::String& text) {
                     auto trimmed = text.trim();
                     trimmed = trimmed.upToFirstOccurrenceOf("dB", false, false).trim();
                     const float db = juce::jlimit(-maxAbsDb, maxAbsDb, trimmed.getFloatValue());
                     return juce::jlimit(0.0f, 1.0f, db / (2.0f * maxAbsDb) + 0.5f);
                 });
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
    const auto third = identity.selectorChoiceLabel(2);
    if (!first.empty() && !second.empty()) {
        if (!third.empty())
            return juce::StringArray { toJuceString(first), toJuceString(second), toJuceString(third) };
        return juce::StringArray { toJuceString(first), toJuceString(second) };
    }
    return makeModeChoiceLabels();
}

inline juce::StringArray makeAuxSelectorChoiceLabels(const ProductIdentity& identity) {
    juce::StringArray labels;
    for (size_t i = 0; i < identity.auxSelectorChoiceLabels.size(); ++i) {
        const auto label = identity.auxSelectorChoiceLabel(i);
        if (!label.empty())
            labels.add(toJuceString(label));
    }
    return labels;
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
    if (identity.supportsAuxSelector()) {
        auto choices = makeAuxSelectorChoiceLabels(identity);
        if (choices.size() > 0) {
            layout.add(std::make_unique<juce::AudioParameterChoice>(
                juce::ParameterID { identity.auxSelectorParamId.data(), 1 },
                toJuceString(identity.auxSelectorLabel.empty() ? "Option" : identity.auxSelectorLabel),
                choices,
                juce::jlimit(0, choices.size() - 1, identity.auxSelectorDefaultIndex),
                makeChoiceAttributes(identity.auxSelectorLabel.empty() ? "Option" : identity.auxSelectorLabel)));
        }
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
        makePercentFloatAttributes()));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { identity.secondaryParamId.data(), 1 },
        toJuceString(identity.secondaryLabel),
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
        identity.secondaryDefaultValue,
        makePercentFloatAttributes()));
    if (identity.supportsTertiaryControl()) {
        const auto tertiaryAttrs = identity.tertiaryLabel == "Gain"
            ? makeUnityGainPercentAttributes()
            : makePercentFloatAttributes();
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { identity.tertiaryParamId.data(), 1 },
            toJuceString(identity.tertiaryLabel),
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
            identity.tertiaryDefaultValue,
            tertiaryAttrs));
    }
    if (identity.supportsQuaternaryControl()) {
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { identity.quaternaryParamId.data(), 1 },
            toJuceString(identity.quaternaryLabel),
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
            identity.quaternaryDefaultValue,
            makePercentFloatAttributes()));
    }
    return layout;
}

inline Mode readMode(const juce::AudioProcessorValueTreeState& state, const ProductIdentity& identity) {
    if (!identity.supportsModeSwitch())
        return identity.defaultMode;
    if (const auto* raw = state.getRawParameterValue(identity.modeParamId.data())) {
        const int index = static_cast<int>(raw->load() + 0.5f);
        if (index <= 0) return Mode::vocal;
        if (index == 1) return Mode::general;
        return Mode::extended;
    }
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

inline int readChoiceIndex(const juce::AudioProcessorValueTreeState& state,
                           std::string_view paramId,
                           int fallback = 0) {
    if (const auto* raw = state.getRawParameterValue(paramId.data()))
        return std::max(0, juce::roundToInt(raw->load()));
    return fallback;
}

} // namespace vxsuite
