#include "VxCleanupProcessor.h"
#include "../../framework/VxSuiteHelpContent.h"
#include "VxSuiteVersions.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr std::string_view kProductName = "Cleanup";
constexpr std::string_view kShortTag = "CLN";
constexpr std::string_view kCleanupParam = "cleanup";
constexpr std::string_view kBodyParam = "body";
constexpr std::string_view kFocusParam = "focus";
constexpr std::string_view kModeParam = "mode";
constexpr std::string_view kListenParam = "listen";
constexpr std::string_view kHpfOnParam = "hpf_on";
constexpr std::string_view kHiShelfOnParam = "hishelf_on";

int chooseSpectralOrder(const double sampleRate) {
    const double sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    constexpr double targetWindowSeconds = 1024.0 / 48000.0;
    int order = 9;
    while ((1 << order) < static_cast<int>(std::ceil(sr * targetWindowSeconds)) && order < 12)
        ++order;
    return order;
}

struct SpectralFeatures {
    float spectralFlatness = 0.0f;
    float harmonicity = 0.0f;
    float highFreqRatio = 0.0f;
    float lowBurstRatio = 0.0f;
    float highBandEnergy = 0.0f;
};

inline float lerp(const float a, const float b, const float t) {
    return a + (b - a) * t;
}

} // namespace

VXCleanupAudioProcessor::VXCleanupAudioProcessor()
    : ProcessorBase(makeIdentity()) {}

vxsuite::ProductIdentity VXCleanupAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.productName = kProductName;
    identity.shortTag = kShortTag;
    identity.primaryParamId = kCleanupParam;
    identity.secondaryParamId = kBodyParam;
    identity.tertiaryParamId = kFocusParam;
    identity.modeParamId = kModeParam;
    identity.listenParamId = kListenParam;
    identity.defaultMode = vxsuite::Mode::vocal;
    identity.primaryLabel = "Cleanup";
    identity.secondaryLabel = "Body";
    identity.tertiaryLabel = "Focus";
    identity.primaryHint = "Remove mud, breaths, plosives, harshness, and stray top-end trouble.";
    identity.secondaryHint = "Keep useful chest and low-mid weight while cleaning the voice up.";
    identity.tertiaryHint = "Steer cleanup from low-mid control toward presence and air control.";
    identity.dspVersion = vxsuite::versions::plugins::cleanup;
    identity.helpTitle = vxsuite::help::cleanup.title;
    identity.helpHtml = vxsuite::help::cleanup.html;
    identity.readmeSection = vxsuite::help::cleanup.readmeSection;
    identity.showLowShelfIcon = true;
    identity.showHighShelfIcon = true;
    identity.lowShelfParamId = kHpfOnParam;
    identity.highShelfParamId = kHiShelfOnParam;
    identity.defaultLowShelf = false;
    identity.defaultHighShelf = false;
    identity.theme.accentRgb = { 0.30f, 0.82f, 0.58f };
    identity.theme.accent2Rgb = { 0.08f, 0.14f, 0.11f };
    identity.theme.backgroundRgb = { 0.05f, 0.07f, 0.06f };
    identity.theme.panelRgb = { 0.09f, 0.12f, 0.10f };
    identity.theme.textRgb = { 0.90f, 0.97f, 0.93f };
    return identity;
}

juce::String VXCleanupAudioProcessor::getStatusText() const {
    if (isListenEnabled())
        return "Listen - removed trouble only";

    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    return isVoice ? "Vocal - speech-aware corrective cleanup"
                   : "General - broader subtractive cleanup";
}

float VXCleanupAudioProcessor::getLowShelfActivity() const noexcept { return polishChain.getDeMudActivity(); }
float VXCleanupAudioProcessor::getHighShelfActivity() const noexcept { return polishChain.getDeEssActivity(); }
int VXCleanupAudioProcessor::getActivityLightCount() const noexcept { return 4; }

float VXCleanupAudioProcessor::getActivityLight(int index) const noexcept {
    switch (index) {
        case 0: return polishChain.getBreathActivity();
        case 1: return polishChain.getDeEssActivity();
        case 2: return polishChain.getPlosiveActivity();
        case 3: return polishChain.getTroubleActivity();
        default: return 0.0f;
    }
}

std::string_view VXCleanupAudioProcessor::getActivityLightLabel(int index) const noexcept {
    switch (index) {
        case 0: return "De-breath";
        case 1: return "De-ess";
        case 2: return "Plosive";
        case 3: return "Smooth";
        default: return {};
    }
}

void VXCleanupAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    spectralOrder = chooseSpectralOrder(currentSampleRateHz);
    spectralFft.prepare(spectralOrder);
    spectralSize = spectralFft.size();
    spectralFifo.assign(static_cast<size_t>(spectralSize), 0.0f);
    spectralWindow.assign(static_cast<size_t>(spectralSize), 0.0f);
    spectralFrame.assign(static_cast<size_t>(spectralSize * 2), 0.0f);
    vxsuite::spectral::prepareSqrtHannWindow(spectralWindow, spectralSize);
    polishChain.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());
    resetSuite();
}

void VXCleanupAudioProcessor::resetSuite() {
    polishChain.reset();
    tonalAnalysis.reset();
    smoothedCleanup = 0.0f;
    smoothedBody = 0.5f;
    smoothedFocus = 0.5f;
    std::fill(spectralFifo.begin(), spectralFifo.end(), 0.0f);
    std::fill(spectralFrame.begin(), spectralFrame.end(), 0.0f);
    spectralWritePos = 0;
    spectralSamplesReady = 0;
    spectralFlatness = 0.0f;
    harmonicity = 0.0f;
    highFreqRatio = 0.0f;
    breathEnv = 0.0f;
    sibilanceEnv = 0.0f;
    plosiveEnv = 0.0f;
    tonalMudEnv = 0.0f;
    harshnessEnv = 0.0f;
    outputTrimmer.reset();
    classifiersPrimed = false;
    controlsPrimed = false;
}

void VXCleanupAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    float dryPeak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        dryPeak = std::max(dryPeak, buffer.getMagnitude(ch, 0, numSamples));

    const float cleanupTarget = vxsuite::readNormalized(parameters, productIdentity.primaryParamId, 0.0f);
    const float bodyTarget = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.5f);
    const float focusTarget = vxsuite::readNormalized(parameters, productIdentity.tertiaryParamId, 0.5f);

    if (!controlsPrimed) {
        smoothedCleanup = cleanupTarget;
        smoothedBody = bodyTarget;
        smoothedFocus = focusTarget;
        controlsPrimed = true;
    } else {
        smoothedCleanup = vxsuite::smoothBlockValue(smoothedCleanup, cleanupTarget, currentSampleRateHz, numSamples, 0.050f);
        smoothedBody = vxsuite::smoothBlockValue(smoothedBody, bodyTarget, currentSampleRateHz, numSamples, 0.090f);
        smoothedFocus = vxsuite::smoothBlockValue(smoothedFocus, focusTarget, currentSampleRateHz, numSamples, 0.070f);
    }

    const bool voiceMode = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const auto& modePolicy = currentModePolicy();
    const auto analysis = getVoiceAnalysisSnapshot();
    const auto voiceContext = getVoiceContextSnapshot();
    const auto signalQuality = getSignalQualitySnapshot();
    const float cleanup = vxsuite::clamp01(smoothedCleanup);
    const float body = vxsuite::clamp01(smoothedBody);
    const float focus = vxsuite::clamp01(smoothedFocus);
    const float lowBias = 1.0f - focus;
    const float highBias = focus;
    const float qualityTrust = juce::jlimit(0.35f, 1.0f,
        0.35f + 0.65f * signalQuality.separationConfidence);
    const float monoPenalty = signalQuality.monoScore;
    const float compressionPenalty = signalQuality.compressionScore;
    const float tiltPenalty = signalQuality.tiltScore;

    vxsuite::polish::updateTonalAnalysis(tonalAnalysis, buffer, currentSampleRateHz, numSamples);

    const int numChannels = buffer.getNumChannels();
    float blockPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        float mono = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            mono += buffer.getReadPointer(ch)[i];
        mono /= static_cast<float>(std::max(1, numChannels));
        blockPeak = std::max(blockPeak, std::abs(mono));
        if (spectralSize > 0) {
            spectralFifo[static_cast<size_t>(spectralWritePos)] = mono;
            spectralWritePos = (spectralWritePos + 1) % spectralSize;
            spectralSamplesReady = std::min(spectralSamplesReady + 1, spectralSize);
        }
    }

    SpectralFeatures spectral {};
    if (spectralFft.isReady() && spectralSize > 0) {
        std::fill(spectralFrame.begin(), spectralFrame.end(), 0.0f);
        const int available = std::min(spectralSamplesReady, spectralSize);
        const int pad = spectralSize - available;
        for (int i = 0; i < available; ++i) {
            const int ringIndex = (spectralWritePos + spectralSize - available + i) % spectralSize;
            spectralFrame[static_cast<size_t>(pad + i)] =
                spectralFifo[static_cast<size_t>(ringIndex)] * spectralWindow[static_cast<size_t>(pad + i)];
        }
        spectralFft.performForward(spectralFrame.data());

        const int bins = spectralSize / 2 + 1;
        const float binHz = static_cast<float>(currentSampleRateHz) / static_cast<float>(spectralSize);
        float totalPower = 1.0e-12f;
        float logPowerSum = 0.0f;
        float highPower = 0.0f;
        float highBandPower = 0.0f;
        float lowBurstPower = 0.0f;
        float harmonicPeakPower = 0.0f;
        float voicedBandPower = 1.0e-12f;
        for (int k = 0; k < bins; ++k) {
            const float re = spectralFrame[static_cast<size_t>(2 * k)];
            const float im = (k == 0 || k == bins - 1) ? 0.0f : spectralFrame[static_cast<size_t>(2 * k + 1)];
            const float power = std::max(1.0e-12f, re * re + im * im);
            const float hz = static_cast<float>(k) * binHz;
            totalPower += power;
            logPowerSum += std::log(power);
            if (hz >= 3000.0f)
                highPower += power;
            if (hz >= 4500.0f)
                highBandPower += power;
            if (hz >= 20.0f && hz <= 160.0f)
                lowBurstPower += power;
            if (hz >= 120.0f && hz <= 5000.0f) {
                voicedBandPower += power;
                const float left = (k > 0) ? std::max(1.0e-12f, std::pow(spectralFrame[static_cast<size_t>(2 * (k - 1))], 2.0f)
                                                              + std::pow((k - 1 == 0 || k - 1 == bins - 1) ? 0.0f : spectralFrame[static_cast<size_t>(2 * (k - 1) + 1)], 2.0f))
                                           : power;
                const float right = (k + 1 < bins) ? std::max(1.0e-12f, std::pow(spectralFrame[static_cast<size_t>(2 * (k + 1))], 2.0f)
                                                                        + std::pow((k + 1 == 0 || k + 1 == bins - 1) ? 0.0f : spectralFrame[static_cast<size_t>(2 * (k + 1) + 1)], 2.0f))
                                                 : power;
                if (power > left && power >= right)
                    harmonicPeakPower += power;
            }
        }
        spectral.spectralFlatness = vxsuite::clamp01(std::exp(logPowerSum / static_cast<float>(bins))
                                            / std::max(1.0e-12f, totalPower / static_cast<float>(bins))
                                            * 4.0f);
        spectral.highFreqRatio = vxsuite::clamp01(highPower / totalPower);
        spectral.lowBurstRatio = vxsuite::clamp01(lowBurstPower / totalPower * 6.0f);
        spectral.highBandEnergy = vxsuite::clamp01(highBandPower / totalPower * 5.0f);
        spectral.harmonicity = vxsuite::clamp01(harmonicPeakPower / voicedBandPower * 1.6f);
    }

    const auto evidence = vxsuite::polish::deriveAnalysisEvidence(tonalAnalysis, analysis, voiceContext);
    const float preserveBody = juce::jlimit(0.0f, 1.0f,
                                            0.30f + 0.70f * body + 0.10f * analysis.protectVoice);
    const float correctiveLean = juce::jlimit(0.65f, 1.15f,
                                              1.0f + (voiceMode ? -0.12f : 0.08f) * body);
    const float spectralPeakiness = vxsuite::clamp01(1.0f - spectral.spectralFlatness);
    const float plosiveSpike = vxsuite::clamp01((blockPeak / evidence.inputEnv - 1.35f) / 1.65f);
    const float speechGate = juce::jlimit(0.05f, 1.0f,
                                          1.0f - 0.82f * evidence.speechConfidence + 0.10f * (1.0f - analysis.directness));
    const float tonalMudTarget = juce::jlimit(0.0f, 1.0f,
                                              0.70f * evidence.mudExcess
                                            + 0.20f * (1.0f - analysis.directness)
                                            + 0.10f * spectral.spectralFlatness);
    const float harshnessTarget = evidence.highTrouble;
    const float breathNoiseLike = vxsuite::clamp01((spectral.spectralFlatness - 0.42f) / 0.42f);
    const float breathAir = vxsuite::clamp01((spectral.highFreqRatio - 0.10f) / 0.32f);
    const float breathTarget = juce::jlimit(0.0f, 1.0f,
                                            (0.56f * breathNoiseLike
                                           + 0.28f * vxsuite::clamp01(1.0f - spectral.harmonicity)
                                           + 0.16f * breathAir)
                                          * (0.55f + 0.45f * breathAir));
    const float sibilanceTarget = juce::jlimit(0.0f, 1.0f,
                                               spectral.highBandEnergy * spectralPeakiness
                                             * (0.55f + 0.45f * harshnessTarget));
    const float plosiveTarget = juce::jlimit(0.0f, 1.0f,
                                             0.45f * analysis.transientRisk
                                           + 0.35f * spectral.lowBurstRatio
                                           + 0.20f * plosiveSpike);
    const float qualitySpeechGate = juce::jlimit(0.08f, 1.0f,
        speechGate + 0.12f * compressionPenalty + 0.08f * monoPenalty);
    const float qualityMudTarget = juce::jlimit(0.0f, 1.0f,
        tonalMudTarget * lerp(1.0f, 0.72f, tiltPenalty));
    const float qualityHarshnessTarget = juce::jlimit(0.0f, 1.0f,
        harshnessTarget * lerp(1.0f, 0.78f, compressionPenalty));
    const float qualityBreathTarget = juce::jlimit(0.0f, 1.0f,
        breathTarget * lerp(1.0f, 0.74f, compressionPenalty));
    const float qualitySibilanceTarget = juce::jlimit(0.0f, 1.0f,
        sibilanceTarget * lerp(1.0f, 0.76f, monoPenalty + 0.35f * compressionPenalty));
    const float qualityPlosiveTarget = juce::jlimit(0.0f, 1.0f,
        plosiveTarget * lerp(1.0f, 0.72f, tiltPenalty + 0.25f * compressionPenalty));

    if (!classifiersPrimed) {
        spectralFlatness = spectral.spectralFlatness;
        harmonicity = spectral.harmonicity;
        highFreqRatio = spectral.highFreqRatio;
        breathEnv = qualityBreathTarget;
        sibilanceEnv = qualitySibilanceTarget;
        plosiveEnv = qualityPlosiveTarget;
        tonalMudEnv = qualityMudTarget;
        harshnessEnv = qualityHarshnessTarget;
        classifiersPrimed = true;
    } else {
        spectralFlatness = vxsuite::smoothBlockToward(spectralFlatness, spectral.spectralFlatness,
                                        currentSampleRateHz, numSamples, 0.030f, 0.180f);
        harmonicity = vxsuite::smoothBlockToward(harmonicity, spectral.harmonicity,
                                   currentSampleRateHz, numSamples, 0.025f, 0.160f);
        highFreqRatio = vxsuite::smoothBlockToward(highFreqRatio, spectral.highFreqRatio,
                                     currentSampleRateHz, numSamples, 0.020f, 0.120f);
        breathEnv = vxsuite::smoothBlockToward(breathEnv, qualityBreathTarget,
                                 currentSampleRateHz, numSamples, 0.025f, 0.170f);
        sibilanceEnv = vxsuite::smoothBlockToward(sibilanceEnv, qualitySibilanceTarget,
                                    currentSampleRateHz, numSamples, 0.012f, 0.090f);
        plosiveEnv = vxsuite::smoothBlockToward(plosiveEnv, qualityPlosiveTarget,
                                  currentSampleRateHz, numSamples, 0.008f, 0.120f);
        tonalMudEnv = vxsuite::smoothBlockToward(tonalMudEnv, qualityMudTarget,
                                   currentSampleRateHz, numSamples, 0.060f, 0.260f);
        harshnessEnv = vxsuite::smoothBlockToward(harshnessEnv, qualityHarshnessTarget,
                                    currentSampleRateHz, numSamples, 0.030f, 0.160f);
    }

    const float tonalMudWeight = juce::jlimit(0.0f, 1.0f,
                                              tonalMudEnv * (0.70f + 0.30f * lowBias));
    const float sibilanceWeight = juce::jlimit(0.0f, 1.0f,
                                               sibilanceEnv * (0.20f + 0.80f * highBias));
    const float breathWeight = juce::jlimit(0.0f, 1.0f,
                                           breathEnv
                                          * qualitySpeechGate
                                          * (1.0f - 0.55f * sibilanceEnv)
                                          * (0.45f + 0.55f * breathAir));
    const float plosiveWeight = juce::jlimit(0.0f, 1.0f,
                                             plosiveEnv
                                           * (0.65f + 0.35f * lowBias)
                                           * (1.0f - 0.15f * evidence.speechConfidence));
    const float harshWeight = juce::jlimit(0.0f, 1.0f,
                                           harshnessEnv * (0.30f + 0.70f * highBias));
    const float voicedIntegrity = juce::jlimit(0.0f, 1.0f,
        0.60f * evidence.speechConfidence + 0.40f * harmonicity);
    const float plosiveFocusGuard = juce::jlimit(0.08f, 1.0f,
        0.08f + 0.92f * lowBias * lowBias);
    const float plosiveVoicedGuard = juce::jlimit(0.05f, 1.0f,
        1.0f - 0.92f * voicedIntegrity * std::pow(1.0f - plosiveTarget, 2.0f));
    const float voicedHighBandGuard = juce::jlimit(0.58f, 1.0f,
        1.0f - 0.34f * voicedIntegrity * (0.45f + 0.55f * highBias));
    const float voicedBreathGuard = juce::jlimit(0.55f, 1.0f,
        1.0f - 0.38f * voicedIntegrity * (0.55f + 0.45f * breathAir));

    const bool hpfOn = vxsuite::readBool(parameters, kHpfOnParam, false);
    const bool hiShelfOn = vxsuite::readBool(parameters, kHiShelfOnParam, false);

    vxsuite::cleanup::Dsp::Params params {};
    params.contentMode = voiceMode ? 0 : 1;
    params.deMud = vxsuite::clamp01(cleanup * qualityTrust * correctiveLean * tonalMudWeight
                           * (0.88f + 0.62f * lowBias)
                           * (1.0f - 0.18f * preserveBody));
    params.deEss = vxsuite::clamp01(cleanup * qualityTrust * sibilanceWeight
                           * (voiceMode ? 1.26f : 1.14f)
                           * voicedHighBandGuard);
    params.breath = vxsuite::clamp01(cleanup * qualityTrust * breathWeight
                            * (voiceMode ? 0.90f : 0.56f)
                            * voicedBreathGuard);
    params.plosive = vxsuite::clamp01(cleanup * qualityTrust * plosiveWeight
                             * (voiceMode ? 1.08f : 0.86f)
                             * (1.0f - 0.15f * preserveBody)
                             * plosiveFocusGuard
                             * plosiveVoicedGuard);
    params.compress = 0.0f;
    params.troubleSmooth = vxsuite::clamp01(cleanup * qualityTrust * harshWeight
                                   * (0.56f + 0.94f * highBias)
                                   * (voiceMode ? 1.22f : 1.10f)
                                   * voicedHighBandGuard);
    params.limit = 0.0f;
    params.recovery = 0.0f;
    params.smartGain = 0.0f;
    params.voicePreserve = juce::jlimit(0.0f, 1.0f,
        0.56f
      + 0.30f * (voiceMode ? modePolicy.sourceProtect : 0.55f * modePolicy.sourceProtect)
      + 0.14f * (voiceMode ? voiceContext.vocalDominance : 0.0f)
      + 0.08f * (1.0f - signalQuality.compressionScore));
    params.denoiseAmount = vxsuite::clamp01(cleanup * qualityTrust
        * juce::jmax(tonalMudWeight, juce::jmax(sibilanceWeight, breathWeight)));
    params.artifactRisk = evidence.artifactRisk;
    params.compSidechainBoostDb = 0.0f;
    params.speechLoudnessDb = evidence.speechLoudnessDb;
    params.proximityContext = juce::jlimit(0.0f, 1.0f,
        0.55f * lowBias + 0.45f * evidence.proximityContext);
    params.speechPresence = juce::jlimit(0.0f, 1.0f,
        0.75f * evidence.speechConfidence + 0.25f * voiceContext.intelligibility);
    params.noiseFloorDb = evidence.noiseFloorDb;
    params.hpfOn = hpfOn;
    params.hiShelfOn = hiShelfOn;

    params.deMud = vxsuite::clamp01(params.deMud);
    params.deEss = vxsuite::clamp01(params.deEss);
    params.breath = vxsuite::clamp01(params.breath);
    params.plosive = vxsuite::clamp01(params.plosive);
    params.troubleSmooth = vxsuite::clamp01(params.troubleSmooth);
    params.voicePreserve = vxsuite::clamp01(params.voicePreserve);
    params.denoiseAmount = vxsuite::clamp01(params.denoiseAmount);
    params.artifactRisk = vxsuite::clamp01(params.artifactRisk);
    params.proximityContext = vxsuite::clamp01(params.proximityContext);
    params.speechPresence = vxsuite::clamp01(params.speechPresence);

    polishChain.setParams(params);
    polishChain.processCorrective(buffer);

    float wetPeak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        wetPeak = std::max(wetPeak, buffer.getMagnitude(ch, 0, numSamples));

    const float allowedPeak = juce::jmin(0.985f, juce::jmax(1.0e-4f, dryPeak * 1.02f + 1.0e-4f));
    if (wetPeak > allowedPeak)
        buffer.applyGain(allowedPeak / std::max(wetPeak, 1.0e-6f));

    outputTrimmer.process(buffer, currentSampleRateHz);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXCleanupAudioProcessor();
}
#endif
