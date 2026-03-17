#include "VxFinishProcessor.h"

#include <cmath>

namespace {

constexpr std::string_view kProductName = "Finish";
constexpr std::string_view kShortTag = "FIN";
constexpr std::string_view kFinishParam = "finish";
constexpr std::string_view kBodyParam = "body";
constexpr std::string_view kGainParam = "gain";
constexpr std::string_view kModeParam = "mode";
constexpr std::string_view kListenParam = "listen";
constexpr float kNeutralGainPosition = 0.5f;

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
    identity.primaryHint = "Compression and final control. Push higher for a more produced voice.";
    identity.secondaryHint = "Smart body and presence recovery after cleanup without filling the noise back in.";
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
        return "Listen - finish delta only";

    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    return isVoice ? "Vocal - smart lift, compression, and final level"
                   : "General - broader finish with controlled enhancement";
}

int VXFinishAudioProcessor::getActivityLightCount() const noexcept { return 3; }

float VXFinishAudioProcessor::getActivityLight(int index) const noexcept {
    switch (index) {
        case 0: return polishChain.getRecoveryActivity();
        case 1: return polishChain.getCompActivity();
        case 2: return polishChain.getLimiterActivity();
        default: return 0.0f;
    }
}

std::string_view VXFinishAudioProcessor::getActivityLightLabel(int index) const noexcept {
    switch (index) {
        case 0: return "Recover";
        case 1: return "Comp";
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
    tonalAnalysis.reset();
    smoothedFinish = 0.0f;
    smoothedBody = 0.5f;
    smoothedGain = 0.5f;
    smoothedTargetGainDb = 0.0f;
    smoothedOutputGainDb = 0.0f;
    outputTrimmer.reset();
    controlsPrimed = false;
}

void VXFinishAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
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
        smoothedFinish = vxsuite::smoothBlockValue(smoothedFinish, finishTarget, currentSampleRateHz, numSamples, 0.050f);
        smoothedBody = vxsuite::smoothBlockValue(smoothedBody, bodyTarget, currentSampleRateHz, numSamples, 0.090f);
        smoothedGain = vxsuite::smoothBlockValue(smoothedGain, gainTarget, currentSampleRateHz, numSamples, 0.080f);
    }

    const bool voiceMode = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const auto& modePolicy = currentModePolicy();
    const auto analysis = getVoiceAnalysisSnapshot();
    const float finish = vxsuite::clamp01(smoothedFinish);
    const float body = vxsuite::clamp01(smoothedBody);
    const float gain = vxsuite::clamp01(smoothedGain);
    const float gainSigned = juce::jlimit(-1.0f, 1.0f, (gain - kNeutralGainPosition) / kNeutralGainPosition);

    vxsuite::polish::updateTonalAnalysis(tonalAnalysis, buffer, currentSampleRateHz, numSamples);

    const auto evidence = vxsuite::polish::deriveAnalysisEvidence(tonalAnalysis, analysis);
    const float speechClarity = juce::jlimit(0.0f, 1.0f,
        (evidence.speechLoudnessDb - (evidence.noiseFloorDb + 6.0f)) / 18.0f);
    const float densityIntent = juce::jlimit(0.0f, 1.0f,
        0.58f * finish
      + 0.16f * body
      + 0.14f * (1.0f - analysis.speechStability)
      + 0.12f * evidence.speechConfidence);
    const float finishDrive = densityIntent * juce::jlimit(0.0f, 1.0f,
        0.62f + 0.38f * evidence.speechConfidence);
    const float targetLowMidRatio = voiceMode ? 0.22f : 0.18f;
    const float recoveryNeed = juce::jlimit(0.0f, 1.0f,
        0.22f
      + 0.40f * juce::jlimit(0.0f, 1.0f, (targetLowMidRatio - evidence.lowMidRatio) / targetLowMidRatio)
      + 0.22f * analysis.protectVoice
      + 0.16f * speechClarity);
    const float protectBias = voiceMode ? modePolicy.sourceProtect : 0.45f * modePolicy.sourceProtect;

    vxsuite::finish::Dsp::Params params {};
    params.contentMode = voiceMode ? 0 : 1;
    params.deMud = 0.0f;
    params.deEss = 0.0f;
    params.breath = 0.0f;
    params.plosive = 0.0f;
    params.compress = finishDrive * juce::jlimit(0.0f, 1.0f,
                                                 0.30f + 0.34f * finish
                                               + 0.24f * (1.0f - analysis.speechStability)
                                               + 0.06f * body);
    params.troubleSmooth = 0.0f;
    params.limit = juce::jlimit(0.16f, 0.92f,
                                0.26f + 0.22f * finish + 0.28f * analysis.transientRisk);
    params.recovery = juce::jlimit(0.0f, 1.0f,
                                   body * recoveryNeed * (0.62f + 0.58f * finishDrive)
                                 + 0.18f * body + 0.08f * finish);
    params.smartGain = juce::jlimit(0.0f, 1.0f, 0.16f + 0.46f * finish + 0.38f * body);
    params.voicePreserve = juce::jlimit(0.0f, 1.0f, 0.42f + 0.58f * protectBias);
    params.denoiseAmount = juce::jlimit(0.0f, 1.0f, 0.18f + 0.82f * speechClarity);
    params.artifactRisk = evidence.artifactRisk;
    params.compSidechainBoostDb = juce::jlimit(0.5f, 5.5f, 1.0f + 3.0f * analysis.speechPresence);
    params.speechLoudnessDb = evidence.speechLoudnessDb;
    params.proximityContext = evidence.proximityContext;
    params.speechPresence = evidence.speechConfidence;
    params.noiseFloorDb = evidence.noiseFloorDb;
    params.hpfOn = false;
    params.hiShelfOn = false;

    polishChain.setParams(params);
    polishChain.processCorrective(buffer);
    polishChain.processRecovery(buffer);

    {
        const float baseTargetLoudnessDb = voiceMode ? -19.0f : -19.8f;
        const float densityLiftDb = 0.48f * finishDrive + 0.32f * finish + 0.18f * (1.0f - analysis.speechStability);
        const float targetMakeupDb = juce::jlimit(-2.5f, 4.0f,
            (baseTargetLoudnessDb - evidence.speechLoudnessDb + densityLiftDb)
          * juce::jlimit(0.0f, 1.0f, 0.24f + 0.50f * speechClarity));
        const float alpha = targetMakeupDb > smoothedTargetGainDb
            ? vxsuite::blockBlendAlpha(currentSampleRateHz, numSamples, 0.220f)
            : vxsuite::blockBlendAlpha(currentSampleRateHz, numSamples, 0.160f);
        smoothedTargetGainDb += alpha * (targetMakeupDb - smoothedTargetGainDb);

        const float makeupGain = juce::Decibels::decibelsToGain(smoothedTargetGainDb);
        if (std::abs(smoothedTargetGainDb) > 0.05f)
            buffer.applyGain(makeupGain);
    }

    polishChain.processLimiter(buffer);

    {
        const float targetOutputGainDb = voiceMode
            ? juce::jmap(gainSigned, -6.0f, 6.0f)
            : juce::jmap(gainSigned, -5.5f, 5.5f);
        const float alpha = targetOutputGainDb > smoothedOutputGainDb
            ? vxsuite::blockBlendAlpha(currentSampleRateHz, numSamples, 0.180f)
            : vxsuite::blockBlendAlpha(currentSampleRateHz, numSamples, 0.120f);
        const float previousOutputGainDb = smoothedOutputGainDb;
        smoothedOutputGainDb += alpha * (targetOutputGainDb - smoothedOutputGainDb);
        const float startGain = juce::Decibels::decibelsToGain(previousOutputGainDb);
        const float endGain   = juce::Decibels::decibelsToGain(smoothedOutputGainDb);
        if (std::abs(startGain - 1.0f) > 1.0e-5f || std::abs(endGain - 1.0f) > 1.0e-5f) {
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.applyGainRamp(ch, 0, numSamples, startGain, endGain);
        }
    }

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
