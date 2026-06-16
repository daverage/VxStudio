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

float VXDenoiserAudioProcessor::getActivityLight(int) const noexcept {
    return smoothedNrActivity;
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
    denoiserDsp.prepare(currentSampleRateHz, samplesPerBlock);
    setReportedLatencySamples(denoiserDsp.getLatencySamples());
    silenceGuard.prepare();
    resetSuite();
}

void VXDenoiserAudioProcessor::resetSuite() {
    denoiserDsp.reset();
    controls.reset(0.0f, 0.5f);
    silenceGuard.reset();
    smoothedMakeupGain = 1.0f;
    smoothedNrActivity = 0.0f;
    prevPhraseActive = true;
}

void VXDenoiserAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    const bool inputIsSilent = silenceGuard.update(buffer);

    const float cleanTarget = vxsuite::readNormalized(parameters, kCleanParam, 0.5f);
    const float guardTarget = vxsuite::readNormalized(parameters, kGuardParam, 0.5f);

    const auto voiceContext = getVoiceContextSnapshot();
    const bool currentPhraseActive = voiceContext.phraseActivity > 0.15f;
    prevPhraseActive = currentPhraseActive;

    const auto [smoothedClean, smoothedGuard] = controls.process(
        cleanTarget, guardTarget, currentSampleRateHz, numSamples, 0.060f, 0.080f);

    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const auto& policy = currentModePolicy();
    const float vocalPriority = isVoice
        ? vxsuite::clamp01(0.40f * voiceContext.vocalDominance
                         + 0.30f * voiceContext.intelligibility
                         + 0.20f * voiceContext.phraseActivity
                         + 0.10f * voiceContext.speechPresence)
        : 0.0f;

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
        // Keep STFT accumulator warm; cold OLA on the first active frame creates a pop.
        // processInPlace output is discarded; aligned dry replaces it.
        denoiserDsp.processInPlace(buffer, 0.0f, opts);
        const int channels = std::min(buffer.getNumChannels(), alignedDry.getNumChannels());
        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom(ch, 0, alignedDry, ch, 0, numSamples);
        smoothedNrActivity *= 0.85f;
        return;
    }

    ensureLatencyAlignedListenDry(numSamples);
    const float dryRms = vxsuite::analysis::rms(buffer);
    denoiserDsp.processInPlace(buffer, effectiveClean, opts);

    // Wet level is tracked for activity display only. Avoid automatic make-up gain.
    const float wetRms = vxsuite::analysis::rms(buffer);

    // NR activity: fraction of energy removed, smoothed for display.
    // Zero when clean (dry ≈ wet), bright when the denoiser is suppressing heavily.
    {
        const float removed = dryRms > 1.0e-5f
            ? vxsuite::clamp01((dryRms - wetRms) / dryRms)
            : 0.0f;
        smoothedNrActivity = 0.85f * smoothedNrActivity + 0.15f * removed;
        if (inputIsSilent)
            smoothedNrActivity *= 0.75f;
    }
    // Do not apply automatic make-up gain after noise reduction. The changing
    // speech-presence estimate can otherwise turn suppression changes into
    // low-frequency thumps, and denoising should not make transient faults louder.
    const float compensationTarget = 1.0f;
    const float prevMakeupGain = smoothedMakeupGain;
    smoothedMakeupGain = vxsuite::smoothBlockValue(smoothedMakeupGain,
                                                   compensationTarget,
                                                   currentSampleRateHz,
                                                   numSamples,
                                                   compensationTarget > 1.0f ? 0.180f : 0.120f);
    if (std::abs(smoothedMakeupGain - 1.0f) > 1.0e-4f || std::abs(prevMakeupGain - 1.0f) > 1.0e-4f)
        buffer.applyGainRamp(0, numSamples, prevMakeupGain, smoothedMakeupGain);
}


void VXDenoiserAudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                                   const juce::AudioBuffer<float>&) {
    // Output removed content (noise) using latency-aligned dry.
    // ensureLatencyAlignedListenDry() was already called inside processProduct().
    const auto& alignedDry = getLatencyAlignedListenDryBuffer();
    const int channels = std::min(outputBuffer.getNumChannels(), alignedDry.getNumChannels());
    const int samples  = std::min(outputBuffer.getNumSamples(),  alignedDry.getNumSamples());
    double deltaEnergy = 0.0;
    int count = 0;
    for (int ch = 0; ch < channels; ++ch) {
        auto* out = outputBuffer.getWritePointer(ch);
        const auto* dry = alignedDry.getReadPointer(ch);
        for (int i = 0; i < samples; ++i) {
            out[i] = dry[i] - out[i];  // removed noise = dry - wet
            deltaEnergy += static_cast<double>(out[i]) * out[i];
            ++count;
        }
    }
    const float deltaRms = count > 0
        ? static_cast<float>(std::sqrt(deltaEnergy / static_cast<double>(count)))
        : 0.0f;
    if (deltaRms < 1.0e-5f) {
        outputBuffer.clear();
        return;
    }
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT) && !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXDenoiserAudioProcessor();
}
#endif
