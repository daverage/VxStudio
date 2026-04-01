#include "VxProximityProcessor.h"
#include "../../framework/VxSuiteHelpContent.h"
#include "VxSuiteVersions.h"

#include <cmath>

namespace {

constexpr std::string_view kProductName  = "Proximity";
constexpr std::string_view kShortTag     = "PRX";
constexpr std::string_view kCloserParam  = "closer";
constexpr std::string_view kAirParam     = "air";
constexpr std::string_view kModeParam    = "mode";
constexpr std::string_view kListenParam  = "listen";

} // namespace

VXProximityAudioProcessor::VXProximityAudioProcessor()
    : ProcessorBase(makeIdentity()) {}

vxsuite::ProductIdentity VXProximityAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
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
    identity.dspVersion       = vxsuite::versions::plugins::proximity;
    identity.helpTitle        = vxsuite::help::proximity.title;
    identity.helpHtml         = vxsuite::help::proximity.html;
    identity.readmeSection    = vxsuite::help::proximity.readmeSection;
    identity.theme.accentRgb      = { 1.00f, 0.65f, 0.10f };
    identity.theme.accent2Rgb     = { 0.10f, 0.08f, 0.05f };
    identity.theme.backgroundRgb  = { 0.06f, 0.05f, 0.04f };
    identity.theme.panelRgb       = { 0.10f, 0.09f, 0.07f };
    identity.theme.textRgb        = { 0.95f, 0.90f, 0.80f };
    identity.primaryDefaultValue = 0.5f;
    identity.secondaryDefaultValue = 0.0f;
    return identity;
}

juce::String VXProximityAudioProcessor::getStatusText() const {
    if (isListenEnabled())
        return "Listen - removed distance shaping only";

    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    return isVoice ? "Vocal - low body + consonant presence"
                   : "General - full-range bass + upper air";
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
    controls.reset(0.0f, 0.0f);
}

void VXProximityAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer,
                                                juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const float closerTarget = vxsuite::readNormalized(parameters, productIdentity.primaryParamId,   0.f);
    const float airTarget    = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.f);

    const auto [smoothedCloser, smoothedAir] = controls.process(
        closerTarget, airTarget,
        currentSampleRateHz, numSamples,
        0.060f, 0.090f);

    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const auto voiceContext = getVoiceContextSnapshot();
    const float vocalPriority = isVoice
        ? vxsuite::clamp01(0.35f * voiceContext.vocalDominance
                         + 0.25f * voiceContext.intelligibility
                         + 0.20f * voiceContext.phraseActivity
                         + 0.20f * voiceContext.transientRisk)
        : 0.0f;
    const float effectiveCloser = isVoice
        ? vxsuite::clamp01(smoothedCloser * (1.0f + 0.12f * voiceContext.buriedSpeech + 0.04f * voiceContext.phraseActivity))
        : vxsuite::clamp01(smoothedCloser);
    const float effectiveAir = isVoice
        ? vxsuite::clamp01(smoothedAir * (1.0f - 0.12f * vocalPriority + 0.08f * voiceContext.intelligibility))
        : vxsuite::clamp01(smoothedAir);

    proximityDsp.processInPlace(buffer, numSamples,
                                effectiveCloser,
                                effectiveAir,
                                isVoice);
}

void VXProximityAudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                                    const juce::AudioBuffer<float>& inputBuffer) {
    renderAddedDeltaOutput(outputBuffer, inputBuffer);
}

 #if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXProximityAudioProcessor();
}
 #endif
