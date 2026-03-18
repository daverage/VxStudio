#include "VxFinishProcessor.h"

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
    identity.primaryHint = "Peak reduction and levelling. Push higher for firmer opto control.";
    identity.secondaryHint = "Light body enhancement only. Keep it subtle and post-cleanup.";
    identity.tertiaryHint = "Final output gain. Middle is neutral, left reduces, right increases.";
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
    outputTrimmer.setReleaseSeconds(0.22f);
    resetSuite();
}

void VXFinishAudioProcessor::resetSuite() {
    polishChain.reset();
    smoothedFinish = 0.0f;
    smoothedBody = 0.5f;
    smoothedGain = 0.5f;
    outputTrimmer.reset();
    controlsPrimed = false;
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

    if (!controlsPrimed) {
        smoothedFinish = finishTarget;
        smoothedBody = bodyTarget;
        smoothedGain = gainTarget;
        controlsPrimed = true;
    } else {
        smoothedFinish = vxsuite::smoothBlockValue(smoothedFinish, finishTarget, currentSampleRateHz, numSamples, 0.080f);
        smoothedBody = vxsuite::smoothBlockValue(smoothedBody, bodyTarget, currentSampleRateHz, numSamples, 0.100f);
        smoothedGain = vxsuite::smoothBlockValue(smoothedGain, gainTarget, currentSampleRateHz, numSamples, 0.080f);
    }

    const bool voiceMode = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const float gainSigned = juce::jlimit(-1.0f, 1.0f, (smoothedGain - 0.5f) / 0.5f);
    const float outputGainDb = juce::jmap(gainSigned, -6.0f, 6.0f);

    vxsuite::finish::Dsp::Params dspParams {};
    dspParams.contentMode = voiceMode ? 0 : 1;
    dspParams.peakReduction = vxsuite::clamp01(smoothedFinish);
    dspParams.outputGainDb = outputGainDb;
    dspParams.body = vxsuite::clamp01(smoothedBody);

    polishChain.setParams(dspParams);
    polishChain.process(buffer);
    outputTrimmer.process(buffer, currentSampleRateHz);
}

void VXFinishAudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                                const juce::AudioBuffer<float>& inputBuffer) {
    const int channels = std::min(outputBuffer.getNumChannels(), inputBuffer.getNumChannels());
    const int samples = std::min(outputBuffer.getNumSamples(), inputBuffer.getNumSamples());
    for (int ch = 0; ch < channels; ++ch) {
        auto* out = outputBuffer.getWritePointer(ch);
        const auto* in = inputBuffer.getReadPointer(ch);
        for (int i = 0; i < samples; ++i)
            out[i] = out[i] - in[i];
    }
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXFinishAudioProcessor();
}
#endif
