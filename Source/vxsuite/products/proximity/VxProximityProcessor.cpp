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

} // namespace

VXProximityAudioProcessor::VXProximityAudioProcessor()
    : ProcessorBase(makeIdentity(), vxsuite::createSimpleParameterLayout(makeIdentity())) {}

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
    identity.primaryDefaultValue = 0.5f;
    identity.secondaryDefaultValue = 0.0f;
    return identity;
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
        smoothedCloser = vxsuite::smoothBlockValue(smoothedCloser, closerTarget, currentSampleRateHz, numSamples, 0.060f);
        smoothedAir = vxsuite::smoothBlockValue(smoothedAir, airTarget, currentSampleRateHz, numSamples, 0.090f);
    }

    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;

    proximityDsp.processInPlace(buffer, numSamples,
                                vxsuite::clamp01(smoothedCloser),
                                vxsuite::clamp01(smoothedAir),
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
