#include "VxProximity2Processor.h"
#include "../../framework/VxStudioHelpContent.h"
#include "VxStudioVersions.h"

#include <cmath>

namespace {

constexpr std::string_view kProductName = "VX Studio Proximity Classic";
constexpr std::string_view kShortTag     = "PC2";
constexpr std::string_view kCloserParam  = "closer";
constexpr std::string_view kAirParam     = "air";
constexpr std::string_view kModeParam    = "mode";
constexpr std::string_view kListenParam  = "listen";

} // namespace

VXProximity2AudioProcessor::VXProximity2AudioProcessor()
    : ProcessorBase(makeIdentity()) {}

vxsuite::ProductIdentity VXProximity2AudioProcessor::makeIdentity() {
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
    identity.stageId          = "vx.proximity2";
    identity.stageType        = vxsuite::StageType::mixed;
    identity.dspVersion       = vxsuite::versions::plugins::proximity2;
    identity.helpTitle        = vxsuite::help::proximity.title;
    identity.helpHtml         = vxsuite::help::proximity.html;
    identity.readmeSection    = vxsuite::help::proximity.readmeSection;
    identity.theme.accentRgb      = { 1.00f, 0.65f, 0.10f };
    identity.theme.accent2Rgb     = { 0.10f, 0.08f, 0.05f };
    identity.theme.backgroundRgb  = { 0.06f, 0.05f, 0.04f };
    identity.theme.panelRgb       = { 0.10f, 0.09f, 0.07f };
    identity.theme.textRgb        = { 0.95f, 0.90f, 0.80f };
    identity.primaryDefaultValue = 0.0f;
    identity.secondaryDefaultValue = 0.0f;
    return identity;
}

juce::String VXProximity2AudioProcessor::getStatusText() const {
    if (isListenEnabled())
        return "Listen - removed distance shaping only";

    const auto& modePolicy = currentModePolicy();
    return modePolicy.mode == vxsuite::Mode::vocal
        ? "Vocal - close-mic body with controlled low-mid cleanup and presence focus"
        : "General - broader close-mic weight with capsule air and restrained mud";
}

void VXProximity2AudioProcessor::prepareSuite(const double sampleRate,
                                              const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    proximityDsp.setChannelCount(getTotalNumOutputChannels());
    proximityDsp.prepare(currentSampleRateHz, samplesPerBlock);
    outputTrimmer.setCeiling(0.96f);
    outputTrimmer.setReleaseSeconds(0.16f);
    resetSuite();
}

void VXProximity2AudioProcessor::resetSuite() {
    proximityDsp.reset();
    outputTrimmer.reset();
    const float closer = vxsuite::readNormalized(parameters, productIdentity.primaryParamId,   0.0f);
    const float air    = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.0f);
    controls.reset(closer, air);
}

void VXProximity2AudioProcessor::processProduct(juce::AudioBuffer<float>& buffer,
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

    const auto& modePolicy = currentModePolicy();
    const bool isVoice = modePolicy.mode == vxsuite::Mode::vocal;
    const auto voiceContext = getVoiceContextSnapshot();
    const float vocalPriority = isVoice
        ? vxsuite::clamp01(0.35f * voiceContext.vocalDominance
                         + 0.25f * voiceContext.intelligibility
                         + 0.20f * voiceContext.phraseActivity
                         + 0.20f * voiceContext.transientRisk)
        : 0.0f;
    const float effectiveCloser = isVoice
        ? vxsuite::clamp01(smoothedCloser * (0.96f + 0.14f * voiceContext.buriedSpeech + 0.06f * modePolicy.bodyRecovery))
        : vxsuite::clamp01(smoothedCloser * (0.96f + 0.06f * modePolicy.bodyRecovery));
    const float effectiveAir = isVoice
        ? vxsuite::clamp01(smoothedAir * (0.94f - 0.10f * vocalPriority + 0.10f * voiceContext.intelligibility))
        : vxsuite::clamp01(smoothedAir * (0.92f + 0.08f * modePolicy.speechFocus));

    proximityDsp.processInPlace(buffer, numSamples,
                                effectiveCloser,
                                effectiveAir,
                                isVoice,
                                vocalPriority);

    outputTrimmer.process(buffer, currentSampleRateHz);
}

void VXProximity2AudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                                    const juce::AudioBuffer<float>& inputBuffer) {
    renderAddedDeltaOutput(outputBuffer, inputBuffer);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT) && !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXProximity2AudioProcessor();
}
#endif
