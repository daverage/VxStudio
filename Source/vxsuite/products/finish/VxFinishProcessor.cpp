#include "VxFinishProcessor.h"

#include <cmath>

namespace {

constexpr std::string_view kSuiteName = "VX Suite";
constexpr std::string_view kProductName = "Finish";
constexpr std::string_view kShortTag = "FIN";
constexpr std::string_view kFinishParam = "finish";
constexpr std::string_view kBodyParam = "body";
constexpr std::string_view kGainParam = "gain";
constexpr std::string_view kModeParam = "mode";
constexpr std::string_view kListenParam = "listen";

} // namespace

VXFinishAudioProcessor::VXFinishAudioProcessor()
    : ProcessorBase(makeIdentity(), vxsuite::createSimpleParameterLayout(makeIdentity())) {}

vxsuite::ProductIdentity VXFinishAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.suiteName = kSuiteName;
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
    identity.tertiaryHint = "Smart gain. Lifts the right bands and makeup only when the signal is clear enough.";
    identity.theme.accentRgb = { 0.88f, 0.50f, 0.18f };
    identity.theme.accent2Rgb = { 0.16f, 0.10f, 0.07f };
    identity.theme.backgroundRgb = { 0.07f, 0.05f, 0.04f };
    identity.theme.panelRgb = { 0.12f, 0.09f, 0.07f };
    identity.theme.textRgb = { 0.98f, 0.94f, 0.88f };
    return identity;
}

const juce::String VXFinishAudioProcessor::getName() const {
    return "VX Finish";
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

juce::AudioProcessorEditor* VXFinishAudioProcessor::createEditor() {
    return new vxsuite::EditorBase(*this);
}

void VXFinishAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    polishChain.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());
    resetSuite();
}

void VXFinishAudioProcessor::resetSuite() {
    polishChain.reset();
    tonalAnalysis.reset();
    smoothedFinish = 0.0f;
    smoothedBody = 0.5f;
    smoothedGain = 0.5f;
    smoothedMakeupDb = 0.0f;
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

    vxsuite::polish::updateTonalAnalysis(tonalAnalysis, buffer, currentSampleRateHz, numSamples);

    const auto evidence = vxsuite::polish::deriveAnalysisEvidence(tonalAnalysis, analysis);
    const float finishDrive = finish * (0.72f + 0.28f * juce::jmax(evidence.mudExcess, evidence.highTrouble));
    const float recoveryNeed = juce::jlimit(0.0f, 1.0f,
                                            0.28f + 0.34f * analysis.protectVoice + 0.24f * evidence.mudExcess + 0.14f * evidence.highTrouble);
    const float protectBias = voiceMode ? modePolicy.sourceProtect : 0.45f * modePolicy.sourceProtect;

    vxsuite::finish::Dsp::Params params {};
    params.contentMode = voiceMode ? 0 : 1;
    params.deMud = 0.0f;
    params.deEss = 0.0f;
    params.breath = 0.0f;
    params.plosive = 0.0f;
    params.compress = finishDrive * juce::jlimit(0.0f, 1.0f,
                                                 0.28f + 0.30f * (1.0f - analysis.speechStability)
                                               + 0.10f * analysis.speechPresence);
    params.troubleSmooth = 0.0f;
    params.limit = juce::jlimit(0.16f, 0.92f,
                                0.24f + 0.42f * finishDrive + 0.18f * body + 0.12f * analysis.transientRisk);
    params.recovery = body * recoveryNeed * juce::jlimit(0.0f, 1.0f, 0.30f + 0.70f * finishDrive);
    params.smartGain = gain;
    params.voicePreserve = juce::jlimit(0.0f, 1.0f, 0.42f + 0.58f * protectBias);
    params.denoiseAmount = juce::jlimit(0.0f, 1.0f, 0.30f + 0.45f * finishDrive + 0.25f * evidence.highTrouble);
    params.artifactRisk = evidence.artifactRisk;
    params.compSidechainBoostDb = juce::jlimit(0.5f, 5.5f, 1.0f + 3.0f * analysis.speechPresence);
    params.speechLoudnessDb = evidence.speechLoudnessDb;
    params.proximityContext = evidence.proximityContext;
    params.speechPresence = evidence.speechConfidence;
    params.noiseFloorDb = evidence.noiseFloorDb;
    params.hpfOn = false;
    params.hiShelfOn = false;

    polishChain.setParams(params);

    auto bufferRms = [&]() {
        float sum = 0.0f;
        const int channels = buffer.getNumChannels();
        for (int ch = 0; ch < channels; ++ch) {
            const float* read = buffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                sum += read[i] * read[i];
        }
        return std::sqrt(sum / static_cast<float>(std::max(1, channels * numSamples)));
    };

    const float preRms = bufferRms();
    polishChain.processCorrective(buffer);
    const float postRms = bufferRms();

    {
        const float reductionDb = juce::Decibels::gainToDecibels(preRms + 1.0e-6f)
                                - juce::Decibels::gainToDecibels(postRms + 1.0e-6f);
        const float targetMakeupDb = juce::jlimit(0.0f, 10.0f, reductionDb);
        const float smartMakeup = juce::jlimit(0.0f, 1.0f, 0.22f + 0.78f * gain);
        const float weightedTargetDb = juce::jlimit(0.0f, 14.0f, targetMakeupDb * (0.70f + 0.80f * smartMakeup));
        const float alpha = targetMakeupDb > smoothedMakeupDb
            ? vxsuite::blockBlendAlpha(currentSampleRateHz, numSamples, 0.180f)
            : vxsuite::blockBlendAlpha(currentSampleRateHz, numSamples, 0.320f);
        smoothedMakeupDb += alpha * (weightedTargetDb - smoothedMakeupDb);

        const float noiseGuard = juce::jlimit(0.0f, 1.0f,
            (evidence.speechLoudnessDb - (evidence.noiseFloorDb + 7.0f)) / 13.0f);
        const float makeupGain = juce::Decibels::decibelsToGain(smoothedMakeupDb * noiseGuard);
        if (makeupGain > 1.001f)
            buffer.applyGain(makeupGain);
    }

    polishChain.processRecovery(buffer);
    polishChain.processLimiter(buffer);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXFinishAudioProcessor();
}
#endif
