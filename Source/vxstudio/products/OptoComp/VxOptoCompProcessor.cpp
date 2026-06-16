#include "VxOptoCompProcessor.h"
#include "../../framework/VxStudioHelpContent.h"
#include "../../framework/VxStudioOptoProductVoicing.h"
#include "VxStudioVersions.h"

namespace {

constexpr std::string_view kProductName = "VX Studio OptoComp";
constexpr std::string_view kShortTag = "OPC";
constexpr std::string_view kPeakReductionParam = "peak_reduction";
constexpr std::string_view kBodyParam = "body";
constexpr std::string_view kGainParam = "gain";
constexpr std::string_view kModeParam = "mode";
constexpr std::string_view kProParam = "pro_mode";
constexpr std::string_view kBehaviorParam = "behavior";
constexpr std::string_view kStereoLinkParam = "stereo_link";
constexpr std::string_view kListenParam = "listen";

} // namespace

VXOptoCompAudioProcessor::VXOptoCompAudioProcessor()
    : ProcessorBase(makeIdentity()) {}

vxsuite::ProductIdentity VXOptoCompAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.productName = kProductName;
    identity.shortTag = kShortTag;
    identity.primaryParamId = kPeakReductionParam;
    identity.secondaryParamId = kBodyParam;
    identity.tertiaryParamId = kGainParam;
    identity.quaternaryParamId = kStereoLinkParam;
    identity.modeParamId = kModeParam;
    identity.expertParamId = kProParam;
    identity.expertButtonLabel = "Pro";
    identity.expertDefaultValue = false;
    identity.auxSelectorParamId = kBehaviorParam;
    identity.auxSelectorLabel = "Behavior";
    identity.auxSelectorChoiceLabels = { "Auto", "Compress", "Limit" };
    identity.auxSelectorDefaultIndex = 0;
    identity.auxSelectorFollowsGeneralMode = false;
    identity.auxSelectorRequiresExpert = true;
    identity.quaternaryRequiresExpert = true;
    identity.listenParamId = kListenParam;
    identity.defaultMode = vxsuite::Mode::vocal;
    identity.primaryLabel = "Peak Red.";
    identity.secondaryLabel = "Body";
    identity.tertiaryLabel = "Gain";
    identity.quaternaryLabel = "Stereo Link";
    identity.primaryDefaultValue = 0.0f;
    identity.secondaryDefaultValue = 0.5f;
    identity.tertiaryDefaultValue = 0.5f;
    identity.quaternaryDefaultValue = 1.0f;
    identity.primaryHint = "Drive the professional LA-2A style gain reduction. Higher values level harder.";
    identity.secondaryHint = "Standalone body trim after compression. Middle stays neutral for classic opto voicing.";
    identity.tertiaryHint = "Standalone output gain trim. Middle is neutral, left reduces, right increases.";
    identity.quaternaryHint = "Right keeps channels linked for classic stereo tracking. Left allows dual-mono style channel response.";
    identity.stageId   = "vx.optocomp";
    identity.stageType = vxsuite::StageType::mixed;
    identity.dspVersion = vxsuite::versions::plugins::optocomp;
    identity.helpTitle = vxsuite::help::optoComp.title;
    identity.helpHtml = vxsuite::help::optoComp.html;
    identity.readmeSection = vxsuite::help::optoComp.readmeSection;
    identity.theme.accentRgb = { 0.94f, 0.76f, 0.28f };
    identity.theme.accent2Rgb = { 0.16f, 0.12f, 0.05f };
    identity.theme.backgroundRgb = { 0.08f, 0.06f, 0.03f };
    identity.theme.panelRgb = { 0.13f, 0.10f, 0.05f };
    identity.theme.textRgb = { 0.98f, 0.95f, 0.84f };
    return identity;
}

juce::String VXOptoCompAudioProcessor::getStatusText() const {
    if (isListenEnabled())
        return "Listen - opto delta";

    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const bool proEnabled = vxsuite::readBool(parameters, kProParam, false);
    const int behaviorIndex = proEnabled ? vxsuite::readChoiceIndex(parameters, kBehaviorParam, 0) : 0;
    const juce::String behaviorLabel = behaviorIndex == 1 ? "Compress"
        : (behaviorIndex == 2 ? "Limit" : "Auto");
    const bool stereoLinked = !proEnabled || vxsuite::readNormalized(parameters, kStereoLinkParam, 1.0f) >= 0.5f;
    const juce::String linkLabel = stereoLinked ? "Linked" : "Dual Mono";
    const juce::String modeLabel = proEnabled ? "Pro" : "Simple";
    return isVoice ? "Vocal - professional LA-2A style opto compression - " + modeLabel + " - " + behaviorLabel + " - " + linkLabel
                   : "General - professional LA-2A style opto limiting - " + modeLabel + " - " + behaviorLabel + " - " + linkLabel;
}

int VXOptoCompAudioProcessor::getActivityLightCount() const noexcept { return 4; }

float VXOptoCompAudioProcessor::getActivityLight(int index) const noexcept {
    switch (index) {
        case 0: return optoDsp.getCompActivity();
        case 1: return juce::jlimit(0.0f, 1.0f, optoDsp.getGainReductionDb() / 20.0f);
        case 2: return optoDsp.getLimiterActivity();
        case 3: return optoDsp.getGainReductionDb() > 0.1f
                    ? juce::jlimit(0.0f, 1.0f, (optoDsp.getEnvelopeDb() + 60.0f) / 60.0f)
                    : 0.0f;
        default: return 0.0f;
    }
}

std::string_view VXOptoCompAudioProcessor::getActivityLightLabel(int index) const noexcept {
    switch (index) {
        case 0: return "Opto";
        case 1: return "GR";
        case 2: return "Limit";
        case 3: return "Env";
        default: return {};
    }
}

void VXOptoCompAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    optoDsp.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());
    resetSuite();
}

void VXOptoCompAudioProcessor::resetSuite() {
    optoDsp.reset();
    outputTrimmer.reset();
    controls.reset(0.0f, 0.5f, 0.5f);
}

void VXOptoCompAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const float peakReductionTarget = vxsuite::readNormalized(parameters, productIdentity.primaryParamId, 0.0f);
    const float bodyTarget = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.5f);
    const float gainTarget = vxsuite::readNormalized(parameters, productIdentity.tertiaryParamId, 0.5f);

    const auto [smoothedPeakReduction, smoothedBody, smoothedGain] = controls.process(
        peakReductionTarget, bodyTarget, gainTarget,
        currentSampleRateHz, numSamples,
        0.080f, 0.100f, 0.080f);

    // 1. DETECT MODE (Framework Pattern)
    const bool voiceMode = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const auto& policy = currentModePolicy();
    const auto voiceContext = getVoiceContextSnapshot();
    const bool proEnabled = vxsuite::readBool(parameters, kProParam, false);
    const int behaviorIndex = proEnabled ? vxsuite::readChoiceIndex(parameters, kBehaviorParam, 0) : 0;
    const bool stereoLinked = !proEnabled || vxsuite::readNormalized(parameters, kStereoLinkParam, 1.0f) >= 0.5f;
    const auto behaviorMode = behaviorIndex == 1 ? vxsuite::opto::BehaviourMode::compress
        : (behaviorIndex == 2 ? vxsuite::opto::BehaviourMode::limit
                              : vxsuite::opto::BehaviourMode::autoFollowProgram);

    const auto renderConfig = vxsuite::opto::buildRenderConfig(
        vxsuite::opto::ProductVariant::standalone,
        voiceMode,
        smoothedPeakReduction,
        smoothedBody,
        smoothedGain,
        behaviorMode,
        stereoLinked,
        policy,
        voiceContext);

    // 5. PROCESS (Framework + Effect-specific)
    optoDsp.setParams(renderConfig.dspParams);
    optoDsp.process(buffer, renderConfig.options);

    outputTrimmer.process(buffer, currentSampleRateHz);
}

vxsuite::MeteringSnapshot VXOptoCompAudioProcessor::getMeteringSnapshot() const noexcept {
    vxsuite::MeteringSnapshot s;
    s.gainReductionDb = optoDsp.getGainReductionDb();
    s.compActivity    = optoDsp.getCompActivity();
    s.limiterActivity = optoDsp.getLimiterActivity();
    return s;
}

void VXOptoCompAudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                                  const juce::AudioBuffer<float>& inputBuffer) {
    renderAddedDeltaOutput(outputBuffer, inputBuffer);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT) && !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXOptoCompAudioProcessor();
}
#endif
