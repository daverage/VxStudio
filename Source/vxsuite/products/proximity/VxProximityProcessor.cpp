#include "VxProximityProcessor.h"

#include <cmath>

namespace {

constexpr std::string_view kSuiteName    = "VX Suite";
constexpr std::string_view kProductName  = "Proximity";
constexpr std::string_view kShortTag     = "PRX";
constexpr std::string_view kCloserParam  = "closer";
constexpr std::string_view kAirParam     = "air";
constexpr std::string_view kModeParam    = "mode";
constexpr std::string_view kListenParam  = "listen";

float clamp01(const float v) { return juce::jlimit(0.f, 1.f, v); }

} // namespace

VXProximityAudioProcessor::VXProximityAudioProcessor()
    : ProcessorBase(makeIdentity(), makeLayout(makeIdentity())) {}

vxsuite::ProductIdentity VXProximityAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.suiteName        = kSuiteName;
    identity.productName      = kProductName;
    identity.shortTag         = kShortTag;
    identity.primaryParamId   = kCloserParam;
    identity.secondaryParamId = kAirParam;
    identity.modeParamId      = kModeParam;
    identity.listenParamId    = kListenParam;
    identity.defaultMode      = vxsuite::Mode::vocal;
    identity.primaryLabel     = "Closer";
    identity.secondaryLabel   = "Air";
    identity.primaryHint      = "Simulate moving the mic closer for natural bass body and warmth.";
    identity.secondaryHint    = "Add upper presence and clarity that characterises a close placement.";
    identity.theme.accentRgb      = { 1.00f, 0.65f, 0.10f };
    identity.theme.accent2Rgb     = { 0.10f, 0.08f, 0.05f };
    identity.theme.backgroundRgb  = { 0.06f, 0.05f, 0.04f };
    identity.theme.panelRgb       = { 0.10f, 0.09f, 0.07f };
    identity.theme.textRgb        = { 0.95f, 0.90f, 0.80f };
    return identity;
}

juce::AudioProcessorValueTreeState::ParameterLayout VXProximityAudioProcessor::makeLayout(
    const vxsuite::ProductIdentity& identity) {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { identity.primaryParamId.data(), 1 },
        vxsuite::toJuceString(identity.primaryLabel),
        juce::NormalisableRange<float> { 0.f, 1.f, 0.001f },
        0.5f,
        juce::AudioParameterFloatAttributes().withLabel(identity.primaryLabel.data())));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { identity.secondaryParamId.data(), 1 },
        vxsuite::toJuceString(identity.secondaryLabel),
        juce::NormalisableRange<float> { 0.f, 1.f, 0.001f },
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel(identity.secondaryLabel.data())));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { kModeParam.data(), 1 },
        "Mode",
        vxsuite::makeModeChoiceLabels(),
        static_cast<int>(identity.defaultMode),
        vxsuite::makeChoiceAttributes("Mode")));
    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { kListenParam.data(), 1 },
        "Listen",
        false,
        vxsuite::makeListenAttributes()));
    return layout;
}

const juce::String VXProximityAudioProcessor::getName() const {
    return "VX Proximity";
}

juce::String VXProximityAudioProcessor::getStatusText() const {
    if (isListenEnabled())
        return "Listen - removed distance shaping only";

    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    return isVoice ? "Vocal - low body + consonant presence"
                   : "General - full-range bass + upper air";
}

juce::AudioProcessorEditor* VXProximityAudioProcessor::createEditor() {
    return new vxsuite::EditorBase(*this);
}

void VXProximityAudioProcessor::prepareSuite(const double sampleRate,
                                              const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    proximityDsp.setChannelCount(getTotalNumOutputChannels());
    proximityDsp.prepare(currentSampleRateHz, samplesPerBlock);
    resetSuite();
}

void VXProximityAudioProcessor::resetSuite() {
    proximityDsp.reset();
    smoothedCloser = 0.f;
    smoothedAir    = 0.f;
    controlsPrimed = false;
}

void VXProximityAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const float closerTarget = vxsuite::readNormalized(parameters, productIdentity.primaryParamId,   0.f);
    const float airTarget    = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.f);

    if (!controlsPrimed) {
        smoothedCloser = closerTarget;
        smoothedAir    = airTarget;
        controlsPrimed = true;
    } else {
        const float sr       = static_cast<float>(currentSampleRateHz);
        const float blendC   = 1.f - std::exp(-static_cast<float>(numSamples) / (0.060f * sr));
        const float blendA   = 1.f - std::exp(-static_cast<float>(numSamples) / (0.090f * sr));
        smoothedCloser += blendC * (closerTarget - smoothedCloser);
        smoothedAir    += blendA * (airTarget    - smoothedAir);
    }

    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;

    proximityDsp.processInPlace(buffer, numSamples,
                                clamp01(smoothedCloser),
                                clamp01(smoothedAir),
                                isVoice);
}

void VXProximityAudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                                    const juce::AudioBuffer<float>& inputBuffer) {
    // Proximity is additive: wet = dry + effect. Listen plays wet - dry (the added effect).
    const int channels = std::min(outputBuffer.getNumChannels(), inputBuffer.getNumChannels());
    const int samples  = std::min(outputBuffer.getNumSamples(),  inputBuffer.getNumSamples());
    for (int ch = 0; ch < channels; ++ch) {
        auto*       out = outputBuffer.getWritePointer(ch);
        const auto* in  = inputBuffer.getReadPointer(ch);
        for (int i = 0; i < samples; ++i)
            out[i] = out[i] - in[i];
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXProximityAudioProcessor();
}
