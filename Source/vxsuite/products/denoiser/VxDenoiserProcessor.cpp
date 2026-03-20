#include "VxDenoiserProcessor.h"

#include <cmath>

namespace {

constexpr std::string_view kProductName  = "Denoiser";
constexpr std::string_view kShortTag     = "DN";
constexpr std::string_view kCleanParam   = "clean";
constexpr std::string_view kGuardParam   = "guard";
constexpr std::string_view kModeParam    = "mode";
constexpr std::string_view kListenParam  = "listen";

float computeBufferRms(const juce::AudioBuffer<float>& buffer) {
    const int channels = buffer.getNumChannels();
    const int samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return 0.0f;

    double sumSquares = 0.0;
    int count = 0;
    for (int ch = 0; ch < channels; ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < samples; ++i) {
            const double sample = data[i];
            sumSquares += sample * sample;
        }
        count += samples;
    }

    return count > 0 ? static_cast<float>(std::sqrt(sumSquares / static_cast<double>(count))) : 0.0f;
}

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
    stageChain.prepare(currentSampleRateHz, samplesPerBlock);
    setReportedLatencySamples(stageChain.totalLatencySamples());
    resetSuite();
}

void VXDenoiserAudioProcessor::resetSuite() {
    stageChain.reset();
    smoothedClean  = 0.0f;
    smoothedGuard  = 0.5f;
    smoothedMakeupGain = 1.0f;
    outputTrimmer.reset();
    controlsPrimed = false;
}

void VXDenoiserAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    const float cleanTarget = vxsuite::readNormalized(parameters, kCleanParam, 0.5f);
    const float guardTarget = vxsuite::readNormalized(parameters, kGuardParam, 0.5f);
    const float dryRms = computeBufferRms(buffer);

    if (!controlsPrimed) {
        smoothedClean  = cleanTarget;
        smoothedGuard  = guardTarget;
        controlsPrimed = true;
    } else {
        smoothedClean = vxsuite::smoothBlockValue(smoothedClean, cleanTarget, currentSampleRateHz, numSamples, 0.060f);
        smoothedGuard = vxsuite::smoothBlockValue(smoothedGuard, guardTarget, currentSampleRateHz, numSamples, 0.080f);
    }

    const bool isVoice  = vxsuite::readMode(parameters, productIdentity)
                       == vxsuite::Mode::vocal;
    const auto& policy  = currentModePolicy();

    // Map user controls + ModePolicy onto ProcessOptions
    vxsuite::ProcessOptions opts;
    const float effectiveClean = isVoice
        ? vxsuite::clamp01(0.55f * smoothedClean)
        : vxsuite::clamp01(0.78f * smoothedClean);
    opts.isVoiceMode        = isVoice;
    opts.sourceProtect      = isVoice ? vxsuite::clamp01(0.55f + 0.45f * smoothedGuard * policy.sourceProtect)
                                      : vxsuite::clamp01(0.28f + 0.52f * smoothedGuard * policy.sourceProtect);
    opts.lateTailAggression = policy.lateTailAggression;
    opts.guardStrictness    = isVoice ? vxsuite::clamp01(0.55f + 0.45f * smoothedGuard * policy.guardStrictness)
                                      : vxsuite::clamp01(0.35f + 0.50f * smoothedGuard * policy.guardStrictness);
    opts.speechFocus        = isVoice ? juce::jmax(0.78f, policy.speechFocus) : juce::jmax(0.18f, policy.speechFocus);

    if (effectiveClean <= 1.0e-4f) {
        ensureLatencyAlignedListenDry(numSamples);
        const auto& alignedDry = getLatencyAlignedListenDryBuffer();
        const int channels = std::min(buffer.getNumChannels(), alignedDry.getNumChannels());
        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom(ch, 0, alignedDry, ch, 0, numSamples);
        return;
    }

    stageChain.processInPlace(buffer, { effectiveClean }, opts);
    ensureLatencyAlignedListenDry(numSamples);

    const float wetRms = computeBufferRms(buffer);
    const float speechPresence = juce::jlimit(0.0f, 1.0f, denoiserDsp.getSignalPresence());
    if (dryRms > 1.0e-5f && wetRms > 1.0e-5f && speechPresence > 0.35f) {
        const float speechWeight = juce::jlimit(0.0f, 1.0f, (speechPresence - 0.35f) / 0.45f);
        const float retentionTarget = isVoice
            ? juce::jlimit(0.76f, 0.90f, (0.76f + 0.07f * smoothedGuard + 0.05f * policy.sourceProtect) * speechWeight)
            : juce::jlimit(0.66f, 0.84f, (0.66f + 0.07f * smoothedGuard + 0.05f * policy.sourceProtect) * speechWeight);
        const float targetRms = dryRms * retentionTarget;
        const float maxCompensation = juce::Decibels::decibelsToGain(isVoice ? 2.5f : 2.0f);
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

    outputTrimmer.process(buffer, currentSampleRateHz);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXDenoiserAudioProcessor();
}
#endif
