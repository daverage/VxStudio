#include "VxFinishProcessor.h"
#include "../../framework/VxStudioHelpContent.h"
#include "../../framework/VxStudioOptoProductVoicing.h"
#include "VxStudioVersions.h"

#include <cmath>

namespace {

constexpr std::string_view kProductName = "VX Studio Finish";
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
    identity.primaryHint = "Intelligent peak reduction and levelling built on the same LA-2A style core as OptoComp.";
    identity.secondaryHint = "Light guided body enhancement only. Keep it subtle and post-cleanup.";
    identity.tertiaryHint = "Final guided output gain. Middle is neutral, left reduces, right increases.";
    identity.dspVersion = vxsuite::versions::plugins::finish;
    identity.helpTitle = vxsuite::help::finish.title;
    identity.helpHtml = vxsuite::help::finish.html;
    identity.readmeSection = vxsuite::help::finish.readmeSection;
    identity.theme.accentRgb = { 0.88f, 0.50f, 0.18f };
    identity.theme.accent2Rgb = { 0.16f, 0.10f, 0.07f };
    identity.theme.backgroundRgb = { 0.07f, 0.05f, 0.04f };
    identity.theme.panelRgb = { 0.12f, 0.09f, 0.07f };
    identity.theme.textRgb = { 0.98f, 0.94f, 0.88f };
    identity.showStereoGainMeter = true;
    return identity;
}

juce::String VXFinishAudioProcessor::getStatusText() const {
  if (isListenEnabled())
    return "Listen - finish delta";

  const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
  return isVoice ? "Vocal - intelligent LA-2A style finish levelling"
                 : "General - intelligent LA-2A style finish limiting";
}

int VXFinishAudioProcessor::getActivityLightCount() const noexcept { return 3; }

float VXFinishAudioProcessor::getActivityLight(int index) const noexcept {
  switch (index) {
    case 0: return finishChain.getCompActivity();
    case 1: return juce::jlimit(0.0f, 1.0f, finishChain.getGainReductionDb() / 20.0f);
    case 2: return finishChain.getLimiterActivity();
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
    currentNumChannels = getTotalNumOutputChannels();
    // 2x oversampling for the limiter stage (minimum phase, low latency)
    oversamplingWrapper.prepare(currentSampleRateHz, samplesPerBlock, currentNumChannels,
                                2, vxsuite::OversamplingWrapper::Mode::minPhase);
    // Prepare DSP at the oversampled rate so filter coefficients are correct
    finishChain.prepare(currentSampleRateHz * 2.0, samplesPerBlock * 2, currentNumChannels);
    setReportedLatencySamples(oversamplingWrapper.addedLatencySamples());
    resetSuite();
}

void VXFinishAudioProcessor::resetSuite() {
    finishChain.reset();
    oversamplingWrapper.reset();
    outputTrimmer.reset();
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

    if (smoothedFinish <= 1.0e-6f
        && std::abs(smoothedBody - 0.5f) <= 1.0e-6f
        && std::abs(smoothedGain - 0.5f) <= 1.0e-6f) {
        ensureLatencyAlignedListenDry(numSamples);
        const auto& alignedDry = getLatencyAlignedListenDryBuffer();
        const int channels = std::min(buffer.getNumChannels(), alignedDry.getNumChannels());
        const int samples = std::min(buffer.getNumSamples(), alignedDry.getNumSamples());
        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom(ch, 0, alignedDry, ch, 0, samples);
        finishChain.reset();
        outputTrimmer.reset();
        return;
    }

    // 1. DETECT MODE (Framework Pattern)
    const bool voiceMode = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const auto& policy = currentModePolicy();
    const auto voiceContext = getVoiceContextSnapshot();

    const auto renderConfig = vxsuite::opto::buildRenderConfig(
        vxsuite::opto::ProductVariant::intelligentFinish,
        voiceMode,
        smoothedFinish,
        smoothedBody,
        smoothedGain,
        vxsuite::opto::BehaviourMode::autoFollowProgram,
        true,
        policy,
        voiceContext);

    // 5. PROCESS — opto at native rate, limiter via 2× oversampling wrapper
    finishChain.setParams(renderConfig.dspParams);
    oversamplingWrapper.process(buffer, [&](juce::AudioBuffer<float>& upBuf) {
        finishChain.process(upBuf, renderConfig.options);
    });

    outputTrimmer.process(buffer, currentSampleRateHz);
}

void VXFinishAudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                                const juce::AudioBuffer<float>& inputBuffer) {
    renderAddedDeltaOutput(outputBuffer, inputBuffer);
}

vxsuite::MeteringSnapshot VXFinishAudioProcessor::getMeteringSnapshot() const noexcept {
    vxsuite::MeteringSnapshot s;
    s.gainReductionDb  = finishChain.getGainReductionDb();
    s.compActivity     = finishChain.getCompActivity();
    s.limiterActivity  = finishChain.getLimiterActivity();
    s.inputPeakL  = finishChain.getDryPeakL();
    s.inputPeakR  = finishChain.getDryPeakR();
    s.outputPeakL = finishChain.getWetPeakL();
    s.outputPeakR = finishChain.getWetPeakR();
    return s;
}

#if !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXFinishAudioProcessor();
}
#endif
