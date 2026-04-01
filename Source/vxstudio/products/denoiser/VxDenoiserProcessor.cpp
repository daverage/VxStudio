#include "VxDenoiserProcessor.h"
#include "../../framework/VxStudioHelpContent.h"
#include "../../framework/VxStudioLightAnalysis.h"
#include "VxStudioVersions.h"

#include <cmath>

namespace {

constexpr std::string_view kProductName  = "Denoiser";
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
    controls.reset(0.0f, 0.5f);
    smoothedMakeupGain = 1.0f;
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
    const auto voiceContext = getVoiceContextSnapshot();
    const float vocalPriority = isVoice
        ? vxsuite::clamp01(0.40f * voiceContext.vocalDominance
                         + 0.30f * voiceContext.intelligibility
                         + 0.20f * voiceContext.phraseActivity
                         + 0.10f * voiceContext.speechPresence)
        : 0.0f;

    // Map user controls + ModePolicy onto ProcessOptions
    vxsuite::ProcessOptions opts;
    const float effectiveClean = vxsuite::clamp01(smoothedClean);
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

    const int channels = buffer.getNumChannels();
    const float maxCompensation = juce::Decibels::decibelsToGain(isVoice ? 6.0f : 5.0f);
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
                    ? juce::jlimit(0.88f, 1.0f, (0.90f + 0.06f * smoothedGuard + 0.04f * policy.sourceProtect) * retentionWeight)
                    : juce::jlimit(0.85f, 1.0f, (0.88f + 0.07f * smoothedGuard + 0.05f * policy.sourceProtect) * speechWeight);
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
                ? juce::jlimit(0.72f, 0.88f, (0.72f + 0.06f * smoothedGuard + 0.04f * policy.sourceProtect + 0.03f * vocalPriority) * retentionWeight)
                : juce::jlimit(0.66f, 0.84f, (0.66f + 0.07f * smoothedGuard + 0.05f * policy.sourceProtect) * speechWeight);
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

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXDenoiserAudioProcessor();
}
#endif
