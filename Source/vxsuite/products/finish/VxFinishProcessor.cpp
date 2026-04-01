#include "VxFinishProcessor.h"
#include "../../framework/VxSuiteHelpContent.h"
#include "VxSuiteVersions.h"

namespace {

constexpr std::string_view kProductName = "Finish";
constexpr std::string_view kShortTag = "FIN";
constexpr std::string_view kFinishParam = "finish";
constexpr std::string_view kBodyParam = "body";
constexpr std::string_view kGainParam = "gain";
constexpr std::string_view kModeParam = "mode";
constexpr std::string_view kListenParam = "listen";
} // namespace

VXFinishAudioProcessor::VXFinishAudioProcessor()
    : ProcessorBase(makeIdentity()) {}

vxsuite::ProductIdentity VXFinishAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.productName = kProductName;
    identity.shortTag = kShortTag;
    identity.primaryParamId = kFinishParam;
    identity.secondaryParamId = kBodyParam;
    identity.tertiaryParamId = kGainParam;
    identity.modeParamId = kModeParam;
    identity.listenParamId = kListenParam;
    identity.defaultMode = vxsuite::Mode::vocal;
    identity.primaryLabel = "Finish";
    identity.secondaryLabel = "Body";
    identity.tertiaryLabel = "Gain";
    identity.primaryDefaultValue = 0.0f;
    identity.secondaryDefaultValue = 0.5f;
    identity.tertiaryDefaultValue = 0.5f;
    identity.primaryHint = "Peak reduction and levelling. Push higher for firmer opto control.";
    identity.secondaryHint = "Light body enhancement only. Keep it subtle and post-cleanup.";
    identity.tertiaryHint = "Final output gain. Middle is neutral, left reduces, right increases.";
    identity.dspVersion = vxsuite::versions::plugins::finish;
    identity.helpTitle = vxsuite::help::finish.title;
    identity.helpHtml = vxsuite::help::finish.html;
    identity.readmeSection = vxsuite::help::finish.readmeSection;
    identity.theme.accentRgb = { 0.88f, 0.50f, 0.18f };
    identity.theme.accent2Rgb = { 0.16f, 0.10f, 0.07f };
    identity.theme.backgroundRgb = { 0.07f, 0.05f, 0.04f };
    identity.theme.panelRgb = { 0.12f, 0.09f, 0.07f };
    identity.theme.textRgb = { 0.98f, 0.94f, 0.88f };
    return identity;
}

juce::String VXFinishAudioProcessor::getStatusText() const {
  if (isListenEnabled())
    return "Listen - finish delta";

  const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
  return isVoice ? "Vocal - LA-2A style compress levelling"
                 : "General - LA-2A style limit levelling";
}

int VXFinishAudioProcessor::getActivityLightCount() const noexcept { return 3; }

float VXFinishAudioProcessor::getActivityLight(int index) const noexcept {
  switch (index) {
    case 0: return polishChain.getCompActivity();
    case 1: return juce::jlimit(0.0f, 1.0f, polishChain.getGainReductionDb() / 20.0f);
    case 2: return polishChain.getLimiterActivity();
    default: return 0.0f;
  }
}

std::string_view VXFinishAudioProcessor::getActivityLightLabel(int index) const noexcept {
  switch (index) {
    case 0: return "Opto";
    case 1: return "GR";
    case 2: return "Limit";
    default: return {};
  }
}


void VXFinishAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    polishChain.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());
    resetSuite();
}

void VXFinishAudioProcessor::resetSuite() {
    polishChain.reset();
    controls.reset(0.0f, 0.5f, 0.5f);
}

void VXFinishAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const float finishTarget = vxsuite::readNormalized(parameters, productIdentity.primaryParamId, 0.0f);
    const float bodyTarget = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.5f);
    const float gainTarget = vxsuite::readNormalized(parameters, productIdentity.tertiaryParamId, 0.5f);

    const auto [smoothedFinish, smoothedBody, smoothedGain] = controls.process(
        finishTarget, bodyTarget, gainTarget, currentSampleRateHz, numSamples,
        0.080f, 0.100f, 0.080f);

    const bool voiceMode = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const auto voiceContext = getVoiceContextSnapshot();
    const float vocalPriority = voiceMode
        ? vxsuite::clamp01(0.38f * voiceContext.vocalDominance
                         + 0.26f * voiceContext.intelligibility
                         + 0.18f * voiceContext.phraseActivity
                         + 0.10f * voiceContext.speechPresence
                         + 0.08f * voiceContext.centerConfidence)
        : 0.0f;
    const float outputGainLinear = juce::jmap(smoothedGain, 0.0f, 1.0f, 0.5f, 1.5f);
    const float outputGainDb = juce::Decibels::gainToDecibels(outputGainLinear, -120.0f);

    vxsuite::finish::Dsp::Params dspParams {};
    dspParams.contentMode = voiceMode ? 0 : 1;
    dspParams.peakReduction = vxsuite::clamp01(smoothedFinish
                            * (voiceMode
                                ? (1.0f - 0.10f * vocalPriority + 0.06f * voiceContext.buriedSpeech)
                                : 1.0f));
    dspParams.outputGainDb = outputGainDb;
    dspParams.body = 0.5f;

    polishChain.setParams(dspParams);
    polishChain.process(buffer);
}

void VXFinishAudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                                const juce::AudioBuffer<float>& inputBuffer) {
    renderAddedDeltaOutput(outputBuffer, inputBuffer);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXFinishAudioProcessor();
}
#endif
