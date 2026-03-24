#include "VxOptoCompProcessor.h"
#include "../../framework/VxSuiteHelpContent.h"
#include "VxSuiteVersions.h"

namespace {

constexpr std::string_view kProductName = "OptoComp";
constexpr std::string_view kShortTag = "OPC";
constexpr std::string_view kPeakReductionParam = "peak_reduction";
constexpr std::string_view kBodyParam = "body";
constexpr std::string_view kGainParam = "gain";
constexpr std::string_view kModeParam = "mode";
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
    identity.modeParamId = kModeParam;
    identity.listenParamId = kListenParam;
    identity.defaultMode = vxsuite::Mode::vocal;
    identity.primaryLabel = "Peak Red.";
    identity.secondaryLabel = "Body";
    identity.tertiaryLabel = "Gain";
    identity.primaryDefaultValue = 0.0f;
    identity.secondaryDefaultValue = 0.5f;
    identity.tertiaryDefaultValue = 0.5f;
    identity.primaryHint = "Drive the LA-2A style gain reduction. Higher values level harder.";
    identity.secondaryHint = "Light post-compressor body shaping only. Middle stays neutral.";
    identity.tertiaryHint = "Final output gain. Middle is neutral, left reduces, right increases.";
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
    return isVoice ? "Vocal - opto compression levelling"
                   : "General - opto limiting levelling";
}

int VXOptoCompAudioProcessor::getActivityLightCount() const noexcept { return 3; }

float VXOptoCompAudioProcessor::getActivityLight(int index) const noexcept {
    switch (index) {
        case 0: return optoDsp.getCompActivity();
        case 1: return juce::jlimit(0.0f, 1.0f, optoDsp.getGainReductionDb() / 20.0f);
        case 2: return optoDsp.getLimiterActivity();
        default: return 0.0f;
    }
}

std::string_view VXOptoCompAudioProcessor::getActivityLightLabel(int index) const noexcept {
    switch (index) {
        case 0: return "Opto";
        case 1: return "GR";
        case 2: return "Limit";
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
    smoothedPeakReduction = 0.0f;
    smoothedBody = 0.5f;
    smoothedGain = 0.5f;
    controlsPrimed = false;
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

    if (!controlsPrimed) {
        smoothedPeakReduction = peakReductionTarget;
        smoothedBody = bodyTarget;
        smoothedGain = gainTarget;
        controlsPrimed = true;
    } else {
        smoothedPeakReduction = vxsuite::smoothBlockValue(smoothedPeakReduction, peakReductionTarget, currentSampleRateHz, numSamples, 0.080f);
        smoothedBody = vxsuite::smoothBlockValue(smoothedBody, bodyTarget, currentSampleRateHz, numSamples, 0.100f);
        smoothedGain = vxsuite::smoothBlockValue(smoothedGain, gainTarget, currentSampleRateHz, numSamples, 0.080f);
    }

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
    dspParams.peakReduction = vxsuite::clamp01(smoothedPeakReduction
                            * (voiceMode
                                ? (1.0f - 0.10f * vocalPriority + 0.06f * voiceContext.buriedSpeech)
                                : 1.0f));
    dspParams.outputGainDb = outputGainDb;
    dspParams.body = 0.5f;

    optoDsp.setParams(dspParams);
    optoDsp.process(buffer);
}

void VXOptoCompAudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                                  const juce::AudioBuffer<float>& inputBuffer) {
    renderAddedDeltaOutput(outputBuffer, inputBuffer);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXOptoCompAudioProcessor();
}
#endif
