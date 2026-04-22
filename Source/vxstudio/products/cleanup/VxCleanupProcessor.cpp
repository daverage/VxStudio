#include "VxCleanupProcessor.h"
#include "../../framework/VxStudioHelpContent.h"
#include "VxStudioVersions.h"

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

float VXCleanupAudioProcessor::getLowShelfActivity() const noexcept { return correctiveChain.getDeMudActivity(); }
float VXCleanupAudioProcessor::getHighShelfActivity() const noexcept { return correctiveChain.getDeEssActivity(); }
int VXCleanupAudioProcessor::getActivityLightCount() const noexcept { return 4; }

float VXCleanupAudioProcessor::getActivityLight(int index) const noexcept {
    switch (index) {
        case 0: return correctiveChain.getBreathActivity();
        case 1: return correctiveChain.getDeEssActivity();
        case 2: return correctiveChain.getPlosiveActivity();
        case 3: return correctiveChain.getTroubleActivity();
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
    persistentCleanupStage.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());
    correctiveChain.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());
    resetSuite();
}

void VXCleanupAudioProcessor::resetSuite() {
    persistentCleanupStage.reset();
    correctiveChain.reset();
    tonalAnalysis.reset();
    controls.reset(0.0f, 0.5f, 0.5f);
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
    persistentLowMidDensity = 0.0f;
    shortLowMidDensity = 0.0f;
    persistentPresenceDensity = 0.0f;
    shortPresenceDensity = 0.0f;
    outputTrimmer.reset();
    smoothedMakeupGain = 1.0f;
    classifiersPrimed = false;
}

void VXCleanupAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    float dryPeak = 0.0f;
    double dryRmsSq = 0.0;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        dryPeak = std::max(dryPeak, buffer.getMagnitude(ch, 0, numSamples));
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            dryRmsSq += static_cast<double>(data[i]) * data[i];
    }
    const int dryCount = buffer.getNumChannels() * numSamples;
    const float dryRms = dryCount > 0 ? static_cast<float>(std::sqrt(dryRmsSq / static_cast<double>(dryCount))) : 0.0f;

    const float cleanupTarget = vxsuite::readNormalized(parameters, productIdentity.primaryParamId, 0.0f);
    const float bodyTarget = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.5f);
    const float focusTarget = vxsuite::readNormalized(parameters, productIdentity.tertiaryParamId, 0.5f);

    const auto [smoothedCleanup, smoothedBody, smoothedFocus] = controls.process(
        cleanupTarget, bodyTarget, focusTarget, currentSampleRateHz, numSamples,
        0.050f, 0.090f, 0.070f);

    const bool voiceMode = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const auto& modePolicy = currentModePolicy();
    const auto analysis = getVoiceAnalysisSnapshot();
    const auto voiceContext = getVoiceContextSnapshot();
    const auto signalQuality = getSignalQualitySnapshot();
    const float cleanup = vxsuite::clamp01(smoothedCleanup);
    const float cleanupDrive = vxsuite::clamp01(std::sqrt(cleanup));
    const float cleanupStrength = juce::jlimit(0.0f, 1.0f,
        (cleanupDrive - 0.46f) / 0.54f);
    const float body = vxsuite::clamp01(smoothedBody);
    const float focus = vxsuite::clamp01(smoothedFocus);
    const float lowBias = 1.0f - focus;
    const float highBias = focus;
    const float qualityTrust = juce::jlimit(0.35f, 1.0f,
        0.35f + 0.65f * signalQuality.separationConfidence);
    const float monoPenalty = signalQuality.monoScore;
    const float compressionPenalty = signalQuality.compressionScore;
    const float tiltPenalty = signalQuality.tiltScore;

    vxsuite::corrective::updateTonalAnalysis(tonalAnalysis, buffer, currentSampleRateHz, numSamples);

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

    const auto evidence = vxsuite::corrective::deriveAnalysisEvidence(tonalAnalysis, analysis, voiceContext);
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
    const float lowMidDensityTarget = juce::jlimit(0.0f, 1.0f,
        0.52f * evidence.lowMidRatio + 0.28f * evidence.mudExcess + 0.20f * spectral.spectralFlatness);
    const float presenceDensityTarget = juce::jlimit(0.0f, 1.0f,
        0.48f * evidence.presenceRatio + 0.32f * evidence.airRatio + 0.20f * spectral.highBandEnergy);
    const float densityPressure = juce::jlimit(0.0f, 1.0f,
        0.34f * evidence.mudExcess
      + 0.18f * evidence.highTrouble
      + 0.18f * shortLowMidDensity
      + 0.12f * persistentLowMidDensity
      + 0.10f * shortPresenceDensity
      + 0.08f * evidence.lowMidRatio);
    const float voicedMaterialGuard = juce::jlimit(0.58f, 1.0f,
        1.0f - 0.24f * evidence.speechConfidence - 0.18f * harmonicity
              - 0.10f * focus + 0.12f * densityPressure);
    const float highBandSpeechGuard = juce::jlimit(0.50f, 1.0f,
        1.0f - 0.28f * evidence.speechConfidence - 0.16f * harmonicity
              - 0.12f * highBias + 0.10f * densityPressure);
    const float cleanupRamp = juce::jlimit(0.0f, 1.0f,
        0.45f * cleanupStrength + 0.55f * cleanupStrength * cleanupStrength);
    const float cleanupIntensity = juce::jlimit(0.95f, 3.10f,
        1.00f + 0.84f * cleanupRamp
      + 1.72f * cleanupRamp * densityPressure * voicedMaterialGuard);

    if (cleanup > 1.0e-4f) {
        vxsuite::clarity::Params persistentParams {};
        persistentParams.clean = juce::jlimit(0.0f, 1.0f,
            (cleanupRamp * (0.46f + 0.32f * densityPressure)
            + 0.06f * cleanupStrength * cleanupStrength)
          * voicedMaterialGuard);
        persistentParams.focus = focus;
        persistentParams.voiceMode = voiceMode;
        persistentParams.sidechainPresent = false;
        persistentParams.monoScore = monoPenalty;
        persistentParams.compressionScore = compressionPenalty;
        persistentParams.tiltScore = tiltPenalty;
        persistentParams.separationConfidence = signalQuality.separationConfidence;
        persistentParams.speechFocus = modePolicy.speechFocus;
        persistentParams.bodyRecovery = juce::jlimit(0.0f, 1.0f,
            0.50f * modePolicy.bodyRecovery + 0.50f * preserveBody);
        persistentParams.guardStrictness = juce::jlimit(0.0f, 1.0f,
            modePolicy.guardStrictness * (0.84f + 0.16f * qualityTrust));
        persistentParams.sourceProtect = juce::jlimit(0.0f, 1.0f,
            0.55f * modePolicy.sourceProtect + 0.45f * preserveBody);
        persistentCleanupStage.process(buffer, nullptr, persistentParams);
    }

    if (!classifiersPrimed) {
        spectralFlatness = spectral.spectralFlatness;
        harmonicity = spectral.harmonicity;
        highFreqRatio = spectral.highFreqRatio;
        persistentLowMidDensity = lowMidDensityTarget;
        shortLowMidDensity = lowMidDensityTarget;
        persistentPresenceDensity = presenceDensityTarget;
        shortPresenceDensity = presenceDensityTarget;
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
        persistentLowMidDensity = vxsuite::smoothBlockToward(persistentLowMidDensity, lowMidDensityTarget,
                                     currentSampleRateHz, numSamples, 0.160f, 0.420f);
        shortLowMidDensity = vxsuite::smoothBlockToward(shortLowMidDensity, lowMidDensityTarget,
                                  currentSampleRateHz, numSamples, 0.030f, 0.120f);
        persistentPresenceDensity = vxsuite::smoothBlockToward(persistentPresenceDensity, presenceDensityTarget,
                                          currentSampleRateHz, numSamples, 0.150f, 0.380f);
        shortPresenceDensity = vxsuite::smoothBlockToward(shortPresenceDensity, presenceDensityTarget,
                                       currentSampleRateHz, numSamples, 0.025f, 0.100f);
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
    const auto readabilityGuard = vxsuite::corrective::deriveReadabilityGuard(
        evidence,
        analysis,
        voiceContext,
        focus,
        voiceMode,
        persistentLowMidDensity,
        shortLowMidDensity,
        persistentPresenceDensity,
        shortPresenceDensity,
        cleanupDrive,
        body,
        cleanupDrive * qualityTrust * correctiveLean * tonalMudWeight,
        cleanupDrive * qualityTrust * sibilanceWeight * (voiceMode ? 1.26f : 1.14f) * voicedHighBandGuard,
        cleanupDrive * qualityTrust * breathWeight * (voiceMode ? 0.90f : 0.56f) * voicedBreathGuard,
        cleanupDrive * qualityTrust * plosiveWeight * (voiceMode ? 1.08f : 0.86f) * (1.0f - 0.15f * preserveBody) * plosiveFocusGuard * plosiveVoicedGuard,
        cleanupDrive * qualityTrust * harshWeight * (0.56f + 0.94f * highBias) * (voiceMode ? 1.22f : 1.10f) * voicedHighBandGuard);

    const bool hpfOn = vxsuite::readBool(parameters, kHpfOnParam, false);
    const bool hiShelfOn = vxsuite::readBool(parameters, kHiShelfOnParam, false);

    vxsuite::cleanup::Dsp::Params params {};
    params.contentMode = voiceMode ? 0 : 1;
    params.deMud = vxsuite::clamp01(cleanupDrive * cleanupIntensity * qualityTrust * correctiveLean * tonalMudWeight
                           * (0.88f + 0.62f * lowBias)
                           * (1.0f - 0.18f * preserveBody)
                           * juce::jlimit(0.48f, 1.0f,
                               1.0f - 0.42f * readabilityGuard.bodyLossRisk
                                   - 0.18f * readabilityGuard.articulationRisk
                                   - 0.14f * readabilityGuard.cumulativeRisk
                                   + 0.10f * readabilityGuard.densityPersistence));
    params.deEss = vxsuite::clamp01(cleanupDrive * cleanupIntensity * qualityTrust * sibilanceWeight
                           * (voiceMode ? 1.26f : 1.14f)
                           * voicedHighBandGuard
                           * highBandSpeechGuard
                           * juce::jlimit(0.52f, 1.0f, 1.0f - 0.62f * readabilityGuard.articulationRisk - 0.18f * readabilityGuard.tonalDriftRisk));
    params.breath = vxsuite::clamp01(cleanupDrive * cleanupIntensity * qualityTrust * breathWeight
                            * (voiceMode ? 0.90f : 0.56f)
                            * voicedBreathGuard
                            * juce::jlimit(0.42f, 1.0f, 1.0f - 0.42f * readabilityGuard.articulationRisk - 0.18f * readabilityGuard.cumulativeRisk));
    params.plosive = vxsuite::clamp01(cleanupDrive * cleanupIntensity * qualityTrust * plosiveWeight
                             * (voiceMode ? 1.08f : 0.86f)
                             * (1.0f - 0.15f * preserveBody)
                             * plosiveFocusGuard
                             * plosiveVoicedGuard
                             * juce::jlimit(0.44f, 1.0f, 1.0f - 0.28f * readabilityGuard.bodyLossRisk - 0.18f * readabilityGuard.cumulativeRisk));
    params.compress = 0.0f;
    params.troubleSmooth = vxsuite::clamp01(cleanupDrive * cleanupIntensity * qualityTrust * harshWeight
                                   * (0.56f + 0.94f * highBias)
                                   * (voiceMode ? 1.22f : 1.10f)
                                   * voicedHighBandGuard
                                   * highBandSpeechGuard
                                   * juce::jlimit(0.46f, 1.0f, 1.0f - 0.48f * readabilityGuard.cumulativeRisk - 0.26f * readabilityGuard.tonalDriftRisk - 0.14f * readabilityGuard.articulationRisk));
    params.limit = 0.0f;
    params.recovery = 0.0f;
    params.smartGain = 0.0f;
    params.voicePreserve = juce::jlimit(0.0f, 1.0f,
        0.56f
      + 0.30f * (voiceMode ? modePolicy.sourceProtect : 0.55f * modePolicy.sourceProtect)
      + 0.14f * (voiceMode ? voiceContext.vocalDominance : 0.0f)
      + 0.08f * (1.0f - signalQuality.compressionScore));
    params.denoiseAmount = 0.0f;
    params.artifactRisk = evidence.artifactRisk;
    params.compSidechainBoostDb = 0.0f;
    params.focusBias = focus;
    params.persistentDensity = readabilityGuard.persistentDensity;
    params.shortDensity = readabilityGuard.shortDensity;
    params.densityPersistence = readabilityGuard.densityPersistence;
    params.selfMaskLowMid = readabilityGuard.selfMaskLowMid;
    params.selfMaskHigh = readabilityGuard.selfMaskHigh;
    params.articulationRisk = readabilityGuard.articulationRisk;
    params.bodyLossRisk = readabilityGuard.bodyLossRisk;
    params.cumulativeRisk = readabilityGuard.cumulativeRisk;
    params.tonalDriftRisk = readabilityGuard.tonalDriftRisk;
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
    params.focusBias = vxsuite::clamp01(params.focusBias);
    params.persistentDensity = vxsuite::clamp01(params.persistentDensity);
    params.shortDensity = vxsuite::clamp01(params.shortDensity);
    params.densityPersistence = vxsuite::clamp01(params.densityPersistence);
    params.selfMaskLowMid = vxsuite::clamp01(params.selfMaskLowMid);
    params.selfMaskHigh = vxsuite::clamp01(params.selfMaskHigh);
    params.articulationRisk = vxsuite::clamp01(params.articulationRisk);
    params.bodyLossRisk = vxsuite::clamp01(params.bodyLossRisk);
    params.cumulativeRisk = vxsuite::clamp01(params.cumulativeRisk);
    params.tonalDriftRisk = vxsuite::clamp01(params.tonalDriftRisk);
    params.proximityContext = vxsuite::clamp01(params.proximityContext);
    params.speechPresence = vxsuite::clamp01(params.speechPresence);

    const float strongCleanupFloor = juce::jlimit(0.0f, 1.0f,
        (cleanupDrive - 0.48f) / 0.52f) * cleanupRamp * (0.38f + 0.52f * densityPressure) * voicedMaterialGuard;
    if (strongCleanupFloor > 0.0f) {
        params.deMud = std::max(params.deMud,
            0.115f * strongCleanupFloor * (0.74f + 0.26f * lowBias)
            * (1.0f - 0.30f * readabilityGuard.bodyLossRisk));
        params.deEss = std::max(params.deEss,
            0.072f * strongCleanupFloor * (0.68f + 0.32f * highBias) * highBandSpeechGuard
            * (1.0f - 0.34f * readabilityGuard.articulationRisk));
        params.breath = std::max(params.breath,
            0.042f * strongCleanupFloor * (0.62f + 0.38f * highBias) * highBandSpeechGuard
            * (1.0f - 0.30f * readabilityGuard.articulationRisk));
        params.troubleSmooth = std::max(params.troubleSmooth,
            0.060f * strongCleanupFloor * (0.58f + 0.42f * highBias) * highBandSpeechGuard
            * (1.0f - 0.32f * readabilityGuard.cumulativeRisk));
    }

    correctiveChain.setParams(params);
    correctiveChain.processCorrective(buffer);

    // RMS makeup: restore level lost to subtractive EQ corrections.
    double wetRmsSq = 0.0;
    float wetPeak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        wetPeak = std::max(wetPeak, buffer.getMagnitude(ch, 0, numSamples));
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            wetRmsSq += static_cast<double>(data[i]) * data[i];
    }
    const float wetRms = dryCount > 0 ? static_cast<float>(std::sqrt(wetRmsSq / static_cast<double>(dryCount))) : 0.0f;

    float makeupTarget = 1.0f;
    if (dryRms > 1.0e-5f && wetRms > 1.0e-5f)
        makeupTarget = juce::jlimit(1.0f, juce::Decibels::decibelsToGain(8.0f),
            (dryRms / std::max(wetRms, 1.0e-6f))
          * juce::jlimit(0.86f, 1.0f, 1.0f - 0.08f * cleanupStrength - 0.06f * densityPressure));
    smoothedMakeupGain = vxsuite::smoothBlockValue(smoothedMakeupGain, makeupTarget, currentSampleRateHz, numSamples, 0.120f);

    // Safety: never push above the original dry peak.
    const float safeGain = wetPeak > 1.0e-6f ? std::min(smoothedMakeupGain, dryPeak / wetPeak) : smoothedMakeupGain;
    if (std::abs(safeGain - 1.0f) > 1.0e-4f)
        buffer.applyGain(safeGain);

    outputTrimmer.process(buffer, currentSampleRateHz);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT) && !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXCleanupAudioProcessor();
}
#endif
