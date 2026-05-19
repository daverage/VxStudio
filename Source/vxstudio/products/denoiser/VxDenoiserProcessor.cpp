#include "VxDenoiserProcessor.h"
#include "../../framework/VxStudioAnalysisEvidence.h"
#include "../../framework/VxStudioHelpContent.h"
#include "../../framework/VxStudioLightAnalysis.h"
#include "../../framework/VxStudioReadabilityGuard.h"
#include "VxStudioVersions.h"

#include <cmath>

namespace {

constexpr std::string_view kProductName = "VX Studio Denoiser";
constexpr std::string_view kShortTag     = "DN";
constexpr std::string_view kCleanParam   = "clean";
constexpr std::string_view kGuardParam   = "guard";
constexpr std::string_view kModeParam    = "mode";
constexpr std::string_view kListenParam  = "listen";

} // namespace

VXDenoiserAudioProcessor::VXDenoiserAudioProcessor()
    : ProcessorBase(makeIdentity()) {}

vxsuite::ProductIdentity VXDenoiserAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity id {};
    id.productName      = kProductName;
    id.shortTag         = kShortTag;
    id.primaryParamId   = kCleanParam;
    id.secondaryParamId = kGuardParam;
    id.modeParamId      = kModeParam;
    id.listenParamId    = kListenParam;
    id.defaultMode      = vxsuite::Mode::vocal;
    id.primaryLabel     = "Clean";
    id.secondaryLabel   = "Guard";
    id.primaryHint      = "Spectral noise reduction - how much noise to remove.";
    id.secondaryHint    = "Artifact protection - guards harmonics and transients from over-processing.";
    id.dspVersion       = vxsuite::versions::plugins::denoiser;
    id.helpTitle        = vxsuite::help::denoiser.title;
    id.helpHtml         = vxsuite::help::denoiser.html;
    id.readmeSection    = vxsuite::help::denoiser.readmeSection;
    // Emerald green
    id.theme.accentRgb     = { 0.15f, 0.85f, 0.50f };
    id.theme.accent2Rgb    = { 0.04f, 0.10f, 0.06f };
    id.theme.backgroundRgb = { 0.04f, 0.06f, 0.05f };
    id.theme.panelRgb      = { 0.07f, 0.10f, 0.08f };
    id.theme.textRgb       = { 0.85f, 0.95f, 0.88f };
    id.primaryDefaultValue = 0.5f;
    id.secondaryDefaultValue = 0.5f;
    return id;
}

juce::String VXDenoiserAudioProcessor::getStatusText() const {
    if (isListenEnabled())
        return "Listen - removed noise only";
    const bool isVoice = vxsuite::readMode(parameters, productIdentity)
                      == vxsuite::Mode::vocal;
    return isVoice ? "Vocal - OM-LSA denoiser with harmonic guard"
                   : "General - broadband spectral noise reduction";
}

void VXDenoiserAudioProcessor::prepareSuite(const double sampleRate,
                                             const int    samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    denoiserDspMono.prepare(currentSampleRateHz, samplesPerBlock);
    denoiserDspLeft.prepare(currentSampleRateHz, samplesPerBlock);
    denoiserDspRight.prepare(currentSampleRateHz, samplesPerBlock);
    artifactCleanupStage.prepare(currentSampleRateHz, getTotalNumOutputChannels());
    // Pre-allocate scratch buffers to maximum typical block size.
    // This prevents heap allocation in the realtime audio thread (processProduct).
    const int maxBlockSize = std::max(samplesPerBlock, 4096);
    leftScratch.setSize(1, maxBlockSize, false, false, true);
    rightScratch.setSize(1, maxBlockSize, false, false, true);
    setReportedLatencySamples(denoiserDspMono.getLatencySamples());
    resetSuite();
}

void VXDenoiserAudioProcessor::resetSuite() {
    denoiserDspMono.reset();
    denoiserDspLeft.reset();
    denoiserDspRight.reset();
    artifactCleanupStage.reset();
    tonalAnalysis.reset();
    controls.reset(0.0f, 0.5f);
    smoothedMakeupGain = 1.0f;
    smoothedResidualTrim = 1.0f;
    smoothedArtifactCleanupBlend = 0.0f;
    smoothedStereoMakeupGain = { 1.0f, 1.0f };
    outputTrimmer.reset();
}

void VXDenoiserAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    const float cleanTarget = vxsuite::readNormalized(parameters, kCleanParam, 0.5f);
    const float guardTarget = vxsuite::readNormalized(parameters, kGuardParam, 0.5f);
    const float dryRms = vxsuite::analysis::rms(buffer);

    const auto [smoothedClean, smoothedGuard] = controls.process(
        cleanTarget, guardTarget, currentSampleRateHz, numSamples, 0.060f, 0.080f);

    const bool isVoice  = vxsuite::readMode(parameters, productIdentity)
                       == vxsuite::Mode::vocal;
    const auto& policy  = currentModePolicy();
    const auto analysis = getVoiceAnalysisSnapshot();
    const auto voiceContext = getVoiceContextSnapshot();
    const auto signalQuality = getSignalQualitySnapshot();
    const float vocalPriority = isVoice
        ? vxsuite::clamp01(0.40f * voiceContext.vocalDominance
                         + 0.30f * voiceContext.intelligibility
                         + 0.20f * voiceContext.phraseActivity
                         + 0.10f * voiceContext.speechPresence)
        : 0.0f;

    // Map user controls + ModePolicy onto ProcessOptions
    vxsuite::ProcessOptions opts;
    const float effectiveClean = smoothedClean <= 1.0e-4f
        ? 0.0f
        : vxsuite::clamp01(std::pow(smoothedClean, isVoice ? 0.84f : 0.68f));
    opts.isVoiceMode        = isVoice;
    opts.sourceProtect      = isVoice ? vxsuite::clamp01(0.48f
                                                       + 0.40f * smoothedGuard * policy.sourceProtect
                                                       + 0.16f * vocalPriority)
                                      : vxsuite::clamp01(0.28f + 0.52f * smoothedGuard * policy.sourceProtect);
    opts.lateTailAggression = policy.lateTailAggression;
    opts.guardStrictness    = isVoice ? vxsuite::clamp01(0.55f
                                                       + 0.45f * smoothedGuard * policy.guardStrictness
                                                       + 0.15f * vocalPriority)
                                      : vxsuite::clamp01(0.35f + 0.50f * smoothedGuard * policy.guardStrictness);
    opts.speechFocus        = isVoice ? juce::jmax(0.78f, juce::jlimit(0.0f, 1.0f, policy.speechFocus + 0.12f * vocalPriority))
                                      : juce::jmax(0.18f, policy.speechFocus);

    if (effectiveClean <= 1.0e-4f) {
        ensureLatencyAlignedListenDry(numSamples);
        const auto& alignedDry = getLatencyAlignedListenDryBuffer();
        const int channels = std::min(buffer.getNumChannels(), alignedDry.getNumChannels());
        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom(ch, 0, alignedDry, ch, 0, numSamples);
        return;
    }

    ensureLatencyAlignedListenDry(numSamples);
    const bool stereo = buffer.getNumChannels() >= 2;
    if (stereo) {
        // Use pre-allocated scratch buffers (no allocation on audio thread).
        leftScratch.copyFrom(0, 0, buffer, 0, 0, numSamples);
        rightScratch.copyFrom(0, 0, buffer, 1, 0, numSamples);
        denoiserDspLeft.processInPlace(leftScratch, effectiveClean, opts);
        denoiserDspRight.processInPlace(rightScratch, effectiveClean, opts);
        buffer.copyFrom(0, 0, leftScratch, 0, 0, numSamples);
        buffer.copyFrom(1, 0, rightScratch, 0, 0, numSamples);
    } else {
        denoiserDspMono.processInPlace(buffer, effectiveClean, opts);
    }

    const float speechEvidence = vxsuite::clamp01(0.70f * vocalPriority
                                                + 0.35f * voiceContext.speechPresence
                                                + 0.25f * voiceContext.phraseActivity
                                                + 0.20f * voiceContext.intelligibility);
    const float speechPreserveBlend = isVoice
        ? juce::jlimit(0.0f, 0.46f,
            effectiveClean
          * juce::jlimit(0.0f, 1.0f, (smoothedGuard - 0.36f) / 0.42f)
          * speechEvidence
          * (0.70f + 0.18f * smoothedGuard))
        : 0.0f;
    if (speechPreserveBlend > 1.0e-4f && dryRms > 1.0e-5f) {
        const auto& dryRef = getLatencyAlignedListenDryBuffer();
        const int dryChannels = std::min(buffer.getNumChannels(), dryRef.getNumChannels());
        const float wetKeep = 1.0f - speechPreserveBlend;
        for (int ch = 0; ch < dryChannels; ++ch) {
            buffer.applyGain(ch, 0, numSamples, wetKeep);
            buffer.addFrom(ch, 0, dryRef, ch, 0, numSamples, speechPreserveBlend);
        }
    }

    const int channels = buffer.getNumChannels();
    const float maxCompensation = juce::Decibels::decibelsToGain(isVoice ? 2.4f : 1.8f);
    if (channels >= 2) {
        for (int ch = 0; ch < 2; ++ch) {
            const float wetRms = vxsuite::analysis::rmsChannel(buffer, ch);
            const float channelDryRms = vxsuite::analysis::rmsChannel(getLatencyAlignedListenDryBuffer(), ch);
            const auto& dsp = (ch == 0) ? denoiserDspLeft : denoiserDspRight;
            const float speechPresence = juce::jlimit(0.0f, 1.0f, dsp.getSignalPresence());
            float compensationTarget = 1.0f;
            if (channelDryRms > 1.0e-5f && wetRms > 1.0e-5f && speechPresence > 0.35f) {
                const float speechWeight = juce::jlimit(0.0f, 1.0f, (speechPresence - 0.35f) / 0.45f);
                const float contextWeight = isVoice ? juce::jlimit(0.0f, 1.0f, 0.65f * vocalPriority + 0.35f * voiceContext.phraseActivity)
                                                    : 0.0f;
                const float retentionWeight = isVoice ? juce::jlimit(0.0f, 1.0f, 0.75f * speechWeight + 0.25f * contextWeight)
                                                      : speechWeight;
                const float retentionTarget = isVoice
                    ? juce::jlimit(0.64f, 0.84f, (0.68f + 0.04f * smoothedGuard + 0.02f * policy.sourceProtect) * retentionWeight)
                    : juce::jlimit(0.46f, 0.64f, (0.48f + 0.04f * smoothedGuard + 0.02f * policy.sourceProtect) * speechWeight);
                const float targetRms = channelDryRms * retentionTarget;
                compensationTarget = juce::jlimit(1.0f, maxCompensation, targetRms / std::max(wetRms, 1.0e-6f));
            }

            auto& smoothedGain = smoothedStereoMakeupGain[static_cast<size_t>(ch)];
            smoothedGain = vxsuite::smoothBlockValue(smoothedGain,
                                                     compensationTarget,
                                                     currentSampleRateHz,
                                                     numSamples,
                                                     compensationTarget > 1.0f ? 0.180f : 0.120f);
            if (std::abs(smoothedGain - 1.0f) > 1.0e-4f)
                buffer.applyGain(ch, 0, numSamples, smoothedGain);
        }
    } else {
        const float wetRms = vxsuite::analysis::rms(buffer);
        const float speechPresence = aggregatedSignalPresence(channels);
        if (dryRms > 1.0e-5f && wetRms > 1.0e-5f && speechPresence > 0.35f) {
            const float speechWeight = juce::jlimit(0.0f, 1.0f, (speechPresence - 0.35f) / 0.45f);
            const float contextWeight = isVoice ? juce::jlimit(0.0f, 1.0f, 0.65f * vocalPriority + 0.35f * voiceContext.phraseActivity)
                                                : 0.0f;
            const float retentionWeight = isVoice ? juce::jlimit(0.0f, 1.0f, 0.75f * speechWeight + 0.25f * contextWeight)
                                                  : speechWeight;
            const float retentionTarget = isVoice
                ? juce::jlimit(0.64f, 0.84f, (0.68f + 0.04f * smoothedGuard + 0.02f * policy.sourceProtect + 0.02f * vocalPriority) * retentionWeight)
                : juce::jlimit(0.46f, 0.64f, (0.48f + 0.04f * smoothedGuard + 0.02f * policy.sourceProtect) * speechWeight);
            const float targetRms = dryRms * retentionTarget;
            const float compensationTarget = juce::jlimit(1.0f,
                                                          maxCompensation,
                                                          targetRms / std::max(wetRms, 1.0e-6f));
            smoothedMakeupGain = vxsuite::smoothBlockValue(smoothedMakeupGain,
                                                           compensationTarget,
                                                           currentSampleRateHz,
                                                           numSamples,
                                                           0.180f);
            if (std::abs(smoothedMakeupGain - 1.0f) > 1.0e-4f)
                buffer.applyGain(smoothedMakeupGain);
        } else {
            smoothedMakeupGain = vxsuite::smoothBlockValue(smoothedMakeupGain,
                                                           1.0f,
                                                           currentSampleRateHz,
                                                           numSamples,
                                                           0.120f);
        }
    }

    vxsuite::corrective::updateTonalAnalysis(tonalAnalysis, buffer, currentSampleRateHz, numSamples);
    const auto evidence = vxsuite::corrective::deriveAnalysisEvidence(tonalAnalysis, analysis, voiceContext);
    const float speechPresence = aggregatedSignalPresence(channels);
    const float nonSpeechResidual = vxsuite::clamp01(1.0f - speechEvidence);
    const float artifactCleanupDrive = vxsuite::clamp01((effectiveClean - 0.30f) / 0.70f);
    const float qualityTrust = juce::jlimit(0.35f, 1.0f, 0.35f + 0.65f * signalQuality.separationConfidence);
    const float artifactEvidence = vxsuite::clamp01(
        0.36f * evidence.artifactRisk
      + 0.20f * evidence.highTrouble
      + 0.16f * nonSpeechResidual
      + 0.14f * (1.0f - speechPresence)
      + 0.14f * (1.0f - signalQuality.compressionScore));
    const float artifactOverlap = vxsuite::clamp01(
        0.34f * speechEvidence
      + 0.22f * voiceContext.intelligibility
      + 0.16f * voiceContext.speechPresence
      + 0.14f * smoothedGuard
      + 0.14f * policy.sourceProtect);
    const float artifactCleanupConfidence = vxsuite::clamp01(
        artifactCleanupDrive * qualityTrust * artifactEvidence * (0.55f + 0.45f * artifactEvidence)
        * (1.0f - 0.78f * artifactOverlap));

    if (artifactCleanupConfidence > 1.0e-4f) {
        vxsuite::corrective::SharedParams cleanupParams {};
        cleanupParams.contentMode = isVoice ? 0 : 1;
        cleanupParams.voicePreserve = juce::jlimit(0.0f, 1.0f,
            0.54f + 0.24f * policy.sourceProtect + 0.22f * speechEvidence);
        cleanupParams.artifactRisk = evidence.artifactRisk;
        cleanupParams.focusBias = juce::jlimit(0.0f, 1.0f,
            0.62f + 0.24f * evidence.highTrouble - 0.14f * evidence.mudExcess);
        cleanupParams.speechLoudnessDb = evidence.speechLoudnessDb;
        cleanupParams.proximityContext = evidence.proximityContext;
        cleanupParams.speechPresence = evidence.speechConfidence;
        cleanupParams.noiseFloorDb = evidence.noiseFloorDb;
        cleanupParams.deMud = 0.0f;
        cleanupParams.plosive = 0.0f;
        cleanupParams.compress = 0.0f;
        cleanupParams.limit = 0.0f;
        cleanupParams.recovery = 0.0f;
        cleanupParams.smartGain = 0.0f;
        cleanupParams.denoiseAmount = effectiveClean;
        cleanupParams.deEss = vxsuite::clamp01(
            artifactCleanupConfidence * (isVoice ? 0.30f : 0.56f)
          * (0.50f + 0.50f * evidence.highTrouble));
        cleanupParams.breath = vxsuite::clamp01(
            artifactCleanupConfidence * (isVoice ? 0.14f : 0.28f)
          * (0.46f + 0.54f * evidence.sizzleExcess)
          * (1.0f - 0.35f * speechEvidence));
        cleanupParams.troubleConfidence = vxsuite::clamp01(
            0.22f + 0.78f * artifactCleanupConfidence * (0.65f + 0.35f * evidence.highTrouble));
        cleanupParams.troubleSmooth = vxsuite::clamp01(
            artifactCleanupConfidence * (isVoice ? 0.22f : 0.48f)
          * (0.55f + 0.45f * evidence.highTrouble)
          * cleanupParams.troubleConfidence);
        cleanupParams.hpfOn = false;
        cleanupParams.hiShelfOn = false;
        artifactCleanupStage.setParams(cleanupParams);
        artifactCleanupStage.process(buffer);
    }

    smoothedArtifactCleanupBlend = vxsuite::smoothBlockValue(smoothedArtifactCleanupBlend,
                                                             artifactCleanupConfidence,
                                                             currentSampleRateHz,
                                                             numSamples,
                                                             artifactCleanupConfidence > smoothedArtifactCleanupBlend ? 0.050f : 0.140f);

    const float residualTrimDrive = vxsuite::clamp01((effectiveClean - 0.24f) / 0.76f);
    const float residualTrimDepth = isVoice ? 0.50f : 0.80f;
    const float residualTrimTarget = 1.0f - residualTrimDepth * residualTrimDrive * nonSpeechResidual
        * (0.78f + 0.22f * smoothedArtifactCleanupBlend);
    smoothedResidualTrim = vxsuite::smoothBlockValue(smoothedResidualTrim,
                                                     juce::jlimit(0.34f, 1.0f, residualTrimTarget),
                                                     currentSampleRateHz,
                                                     numSamples,
                                                     residualTrimTarget < smoothedResidualTrim ? 0.030f : 0.160f);
    if (std::abs(smoothedResidualTrim - 1.0f) > 1.0e-4f)
        buffer.applyGain(smoothedResidualTrim);

    outputTrimmer.process(buffer, currentSampleRateHz);
}

float VXDenoiserAudioProcessor::aggregatedSignalPresence(const int numChannels) const noexcept {
    if (numChannels >= 2) {
        const float left = juce::jlimit(0.0f, 1.0f, denoiserDspLeft.getSignalPresence());
        const float right = juce::jlimit(0.0f, 1.0f, denoiserDspRight.getSignalPresence());
        return juce::jlimit(0.0f, 1.0f, 0.5f * (left + right));
    }
    return juce::jlimit(0.0f, 1.0f, denoiserDspMono.getSignalPresence());
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT) && !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXDenoiserAudioProcessor();
}
#endif
