#include "VxDeverbProcessor.h"
#include "../../framework/VxStudioDspCommon.h"
#include "../../framework/VxStudioHelpContent.h"
#include "VxStudioVersions.h"

#include <cmath>

namespace {

constexpr std::string_view kProductName = "VX Studio Deverb";
constexpr std::string_view kShortTag = "DVD";
constexpr std::string_view kReduceParam = "reduce";
constexpr std::string_view kBodyParam = "body";
constexpr std::string_view kModeParam = "mode";
constexpr std::string_view kListenParam = "listen";
constexpr int kMinPreparedBlockSize = 2048;

float safeValue(const float value) {
    if (!std::isfinite(value))
        return 0.0f;
    if (std::fpclassify(value) == FP_SUBNORMAL)
        return 0.0f;
    return value;
}

float onePoleAlpha(const double sampleRate, const float cutoffHz) {
    if (sampleRate <= 0.0 || cutoffHz <= 0.0f)
        return 0.0f;
    return std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz / static_cast<float>(sampleRate));
}

struct BufferStats { float rms = 0.0f; float peak = 0.0f; bool stable = true; };

BufferStats computeBufferStats(const juce::AudioBuffer<float>& buffer, const float maxStablePeak = 1.0e30f) {
    const int channels = buffer.getNumChannels();
    const int samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return {};

    double sumSquares = 0.0;
    float peak = 0.0f;
    bool stable = true;
    for (int ch = 0; ch < channels; ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < samples; ++i) {
            const float s = data[i];
            if (!std::isfinite(s)) { stable = false; continue; }
            const float abs = std::abs(s);
            if (abs > maxStablePeak) stable = false;
            peak = std::max(peak, abs);
            sumSquares += static_cast<double>(s) * static_cast<double>(s);
        }
    }
    const int count = channels * samples;
    return { count > 0 ? static_cast<float>(std::sqrt(sumSquares / static_cast<double>(count))) : 0.0f,
             peak, stable };
}

float computeBufferRms(const juce::AudioBuffer<float>& buffer) {
    return computeBufferStats(buffer).rms;
}

} // namespace

VXDeverbAudioProcessor::VXDeverbAudioProcessor()
    : ProcessorBase(makeIdentity()) {}

vxsuite::ProductIdentity VXDeverbAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.productName = kProductName;
    identity.shortTag = kShortTag;
    identity.primaryParamId = kReduceParam;
    identity.secondaryParamId = kBodyParam;
    identity.modeParamId = kModeParam;
    identity.listenParamId = kListenParam;
    identity.defaultMode = vxsuite::Mode::vocal;
    identity.primaryLabel = "Reduce";
    identity.secondaryLabel = "Blend";
    identity.primaryHint = "Remove late room tail with a clean wet strength control.";
    identity.secondaryHint = "Restore low-end body after deverb if the source feels too thin.";
    identity.dspVersion = vxsuite::versions::plugins::deverb;
    identity.helpTitle = vxsuite::help::deverb.title;
    identity.helpHtml = vxsuite::help::deverb.html;
    identity.readmeSection = vxsuite::help::deverb.readmeSection;
    identity.theme.accentRgb = { 0.00f, 0.82f, 1.00f };
    identity.theme.accent2Rgb = { 0.07f, 0.08f, 0.10f };
    identity.theme.backgroundRgb = { 0.05f, 0.05f, 0.07f };
    identity.theme.panelRgb = { 0.09f, 0.09f, 0.12f };
    identity.theme.textRgb = { 0.86f, 0.91f, 1.00f };
    identity.primaryDefaultValue = 0.5f;
    identity.secondaryDefaultValue = 0.25f;  // Restore ~2.5 dB bass by default instead of 0 dB
    return identity;
}

float VXDeverbAudioProcessor::getActivityLight(int) const noexcept {
    return vxsuite::clamp01(deverbProcessor.getGainReductionDb() / -20.0f);
}

juce::String VXDeverbAudioProcessor::getStatusText() const {
    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    return isVoice ? "Vocal - LRSV dereverberation with body restore"
                   : "General - deeper tail removal across full range";
}

void VXDeverbAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    preparedBlockSize = std::max(kMinPreparedBlockSize, std::max(1, samplesPerBlock));
    deverbProcessor.setChannelCount(getTotalNumOutputChannels());
    deverbProcessor.prepare(currentSampleRateHz, preparedBlockSize);
    setReportedLatencyFromStages(deverbProcessor);
    ensureScratchCapacity(getTotalNumOutputChannels(), preparedBlockSize);
    resetSuite();
}

void VXDeverbAudioProcessor::resetSuite() {
    deverbProcessor.reset();
    wetScratch.clear();
    controls.reset(0.0f);
    smoothedCompensationGain = 1.0f;
    smoothedBody = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.0f);
    bodyShelfZ1.fill(0.0f);
    bodyShelfZ2.fill(0.0f);
    firstBlockProcessed = false;
}

void VXDeverbAudioProcessor::setTestRt60PresetSeconds(const float rt60Seconds) {
    deverbProcessor.setRt60PresetSeconds(rt60Seconds);
}

void VXDeverbAudioProcessor::clearTestRt60Preset() {
    deverbProcessor.clearRt60Preset();
}

void VXDeverbAudioProcessor::setTestDeterministicReset(const bool shouldUseDefaultRt60) {
    deverbProcessor.setDeterministicReset(shouldUseDefaultRt60);
}

float VXDeverbAudioProcessor::getTestTrackedRt60Seconds(const int channel) const noexcept {
    return deverbProcessor.getTrackedRt60Seconds(channel);
}

void VXDeverbAudioProcessor::setTestOverSubtract(const float newOverSubtract) {
    deverbProcessor.setOverSubtract(newOverSubtract);
}

float VXDeverbAudioProcessor::getTestOverSubtract() const noexcept {
    return deverbProcessor.getOverSubtract();
}

void VXDeverbAudioProcessor::setTestNoCepstral(const bool shouldBypass) {
    deverbProcessor.setDebugNoCepstral(shouldBypass);
}

bool VXDeverbAudioProcessor::isTestNoCepstral() const noexcept {
    return deverbProcessor.isDebugNoCepstral();
}

void VXDeverbAudioProcessor::setVoiceMode(const bool enabled) noexcept {
    deverbProcessor.voiceMode = enabled;
}

bool VXDeverbAudioProcessor::isVoiceMode() const noexcept {
    return deverbProcessor.voiceMode;
}

void VXDeverbAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int outputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;
    if (wetScratch.getNumChannels() < outputChannels || wetScratch.getNumSamples() < numSamples)
        return;

    const float reduceTarget = vxsuite::readNormalized(parameters, productIdentity.primaryParamId, 0.0f);
    const float bodyTarget   = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.0f);
    const float dryRms = computeBufferRms(buffer);
    const bool isFirstBlock = !firstBlockProcessed;
    const float smoothedReduce = controls.process(reduceTarget, currentSampleRateHz, numSamples, 0.060f);
    firstBlockProcessed = true;

    for (int ch = 0; ch < outputChannels; ++ch)
        wetScratch.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    // 1. DETECT MODE (Framework Pattern)
    const bool voiceMode = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const auto& policy = currentModePolicy();
    const auto analysis = getVoiceAnalysisSnapshot();
    const auto voiceContext = getVoiceContextSnapshot();

    // 2. CALCULATE VOCAL PRIORITY (Framework Pattern - Standard 5-factor for Deverb)
    const float vocalPriority = voiceMode
        ? vxsuite::clamp01(0.36f * voiceContext.vocalDominance
                         + 0.28f * voiceContext.intelligibility
                         + 0.18f * voiceContext.phraseActivity
                         + 0.10f * voiceContext.speechPresence
                         + 0.08f * voiceContext.centerConfidence)
        : 0.0f;

    // 3. BUILD ProcessOptions (Framework Pattern)
    vxsuite::ProcessOptions options {};
    options.isVoiceMode = voiceMode;
    options.sourceProtect = voiceMode
        ? vxsuite::clamp01(0.72f + 0.22f * vocalPriority)
        : vxsuite::clamp01(0.45f);
    options.guardStrictness = voiceMode
        ? vxsuite::clamp01(0.68f + 0.25f * vocalPriority)
        : vxsuite::clamp01(0.42f);
    options.speechFocus = voiceMode
        ? juce::jmax(0.82f, policy.speechFocus + 0.14f * vocalPriority)
        : juce::jmax(0.28f, policy.speechFocus);
    options.voiceProtect = voiceMode ? 0.90f : 0.60f;
    options.lateTailAggression = policy.lateTailAggression;
    options.labRawMode = true;
    const float tailEvidence = vxsuite::clamp01(
        0.52f * analysis.tailLikelihood
      + 0.20f * (1.0f - analysis.directness)
      + 0.16f * voiceContext.buriedSpeech
      + 0.12f * (1.0f - voiceContext.intelligibility));
    deverbProcessor.setVoiceMode(voiceMode);

    // Pass reduce directly as the Wiener amount — at reduce=0, amount=0 so all
    // Wiener gains collapse to 1.0 (true bypass with no dry blend needed).
    // overSubtract still scales with reduce so depth ramps up with the knob.
    const float reduce = vxsuite::clamp01(smoothedReduce);

    // Early return when deverb is effectively disabled
    if (reduce <= 1.0e-4f) {
        ensureLatencyAlignedListenDry(numSamples);
        const auto& alignedDry = getLatencyAlignedListenDryBuffer();
        const int channels = std::min(buffer.getNumChannels(), alignedDry.getNumChannels());
        for (int ch = 0; ch < channels; ++ch)
            buffer.copyFrom(ch, 0, alignedDry, ch, 0, numSamples);
        return;
    }

    const float reduceDrive = std::pow(reduce, voiceMode ? 0.76f : 0.72f);
    const float effectiveReduce = voiceMode
        ? vxsuite::clamp01(reduceDrive
                           * (1.0f - 0.02f * vocalPriority + 0.18f * voiceContext.buriedSpeech)
                           * (0.82f + 0.30f * tailEvidence))
        : vxsuite::clamp01(reduceDrive * (0.88f + 0.24f * tailEvidence));
    const float overSubtractTarget = voiceMode
        ? (1.0f + 5.8f * effectiveReduce * (0.84f + 0.32f * tailEvidence))
        : (1.0f + 6.6f * effectiveReduce * (0.86f + 0.28f * tailEvidence));
    deverbProcessor.setOverSubtract(overSubtractTarget);

    const auto renderWet = [&](const float amount) {
        for (int ch = 0; ch < outputChannels; ++ch)
            wetScratch.copyFrom(ch, 0, buffer, ch, 0, numSamples);
        juce::AudioBuffer<float> wetView (wetScratch.getArrayOfWritePointers(), outputChannels, numSamples);
        deverbProcessor.processInPlace(wetView, amount, options);
    };

    renderWet(effectiveReduce);
    {
        const auto wetStats = computeBufferStats(wetScratch, 4.0f);
        if (!wetStats.stable) {
            deverbProcessor.reset();
            for (int ch = 0; ch < outputChannels; ++ch)
                wetScratch.copyFrom(ch, 0, buffer, ch, 0, numSamples);
        }
    }
    ensureLatencyAlignedListenDry(numSamples);

    for (int ch = 0; ch < outputChannels; ++ch)
        buffer.copyFrom(ch, 0, wetScratch, ch, 0, numSamples);

    const auto finalWetStats = computeBufferStats(buffer);
    applyLoudnessCompensation(buffer, dryRms, effectiveReduce, isFirstBlock, finalWetStats.rms, finalWetStats.peak);

    // Body: post-deverb low shelf to restore low-end weight lost to subtraction.
    // 250 Hz shelf, +6 dB max at body=1.0, neutral at body=0.
    smoothedBody = vxsuite::smoothBlockValue(smoothedBody, bodyTarget,
                                             currentSampleRateHz, numSamples, 0.120f);
    if (smoothedBody > 1.0e-4f) {
        const float gainDb = 10.0f * smoothedBody;
        const float A   = std::pow(10.0f, gainDb / 40.0f);
        const float w0  = 2.0f * juce::MathConstants<float>::pi * 250.0f
                          / static_cast<float>(currentSampleRateHz);
        const float cosW = std::cos(w0);
        const float sinW = std::sin(w0);
        const float sqA  = std::sqrt(A);
        const float alph = sinW / (2.0f * 0.7071067811865476f);
        const float a0   = (A + 1.0f) + (A - 1.0f) * cosW + 2.0f * sqA * alph;
        const float invA0 = 1.0f / a0;
        const float cb0 =  A * ((A + 1.0f) - (A - 1.0f) * cosW + 2.0f * sqA * alph) * invA0;
        const float cb1 =  2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosW) * invA0;
        const float cb2 =  A * ((A + 1.0f) - (A - 1.0f) * cosW - 2.0f * sqA * alph) * invA0;
        const float ca1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cosW) * invA0;
        const float ca2 =        ((A + 1.0f) + (A - 1.0f) * cosW - 2.0f * sqA * alph) * invA0;

        const int numChannels = std::min(buffer.getNumChannels(),
                                         static_cast<int>(bodyShelfZ1.size()));
        for (int ch = 0; ch < numChannels; ++ch) {
            float z1 = bodyShelfZ1[static_cast<size_t>(ch)];
            float z2 = bodyShelfZ2[static_cast<size_t>(ch)];
            float* data = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i) {
                const float w = data[i] - ca1 * z1 - ca2 * z2;
                data[i] = safeValue(cb0 * w + cb1 * z1 + cb2 * z2);
                z2 = z1;
                z1 = w;
            }
            bodyShelfZ1[static_cast<size_t>(ch)] = z1;
            bodyShelfZ2[static_cast<size_t>(ch)] = z2;
        }
    }
}

void VXDeverbAudioProcessor::ensureScratchCapacity(const int channels, const int samples) {
    const int safeChannels = std::max(1, channels);
    const int safeSamples = std::max(1, samples);
    wetScratch.setSize(safeChannels, safeSamples, false, false, true);
}

void VXDeverbAudioProcessor::applyLoudnessCompensation(juce::AudioBuffer<float>& wetBuffer,
                                                       const float dryRms,
                                                       const float reduceAmount,
                                                       const bool isFirstBlock,
                                                       const float wetRms,
                                                       const float wetPeak) noexcept {
    if (dryRms <= 1.0e-6f || wetRms <= 1.0e-6f || reduceAmount <= 1.0e-4f) {
        smoothedCompensationGain = 1.0f;
        return;
    }

    const float dryDb = juce::Decibels::gainToDecibels(dryRms, -120.0f);
    const float wetDb = juce::Decibels::gainToDecibels(wetRms, -120.0f);
    const float lostDb = std::max(0.0f, dryDb - wetDb);
    const float compensationScale = juce::jmap(juce::jlimit(0.0f, 1.0f, reduceAmount),
                                               0.0f, 1.0f,
                                               0.68f, 0.24f);
    const float compensationDb = std::min(8.0f, lostDb * compensationScale);
    const float targetGain = juce::Decibels::decibelsToGain(compensationDb);

    if (isFirstBlock) {
        smoothedCompensationGain = targetGain;
    } else {
        smoothedCompensationGain = vxsuite::smoothBlockValue(smoothedCompensationGain,
                                                             targetGain,
                                                             currentSampleRateHz,
                                                             wetBuffer.getNumSamples(),
                                                             0.180f);
    }

    const float peakSafeGain = wetPeak > 1.0e-6f ? std::min(1.0f / wetPeak, 1.55f) : 1.0f;
    const float appliedGain = std::min(smoothedCompensationGain, peakSafeGain);
    wetBuffer.applyGain(appliedGain);
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT) && !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXDeverbAudioProcessor();
}
#endif
