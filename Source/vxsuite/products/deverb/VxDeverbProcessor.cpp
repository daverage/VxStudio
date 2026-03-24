#include "VxDeverbProcessor.h"
#include "../../framework/VxSuiteHelpContent.h"
#include "VxSuiteVersions.h"

#include <cmath>

namespace {

constexpr std::string_view kProductName = "Deverb";
constexpr std::string_view kShortTag = "DVD";
constexpr std::string_view kReduceParam = "reduce";
constexpr std::string_view kBodyParam = "body";
constexpr std::string_view kModeParam = "mode";
constexpr std::string_view kListenParam = "listen";
constexpr int kMinPreparedBlockSize = 8192;

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

bool bufferIsStable(const juce::AudioBuffer<float>& buffer, const float maxPeak) {
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            const float s = data[i];
            if (!std::isfinite(s))
                return false;
            peak = std::max(peak, std::abs(s));
        }
    }
    return peak <= maxPeak;
}

float computeBufferRms(const juce::AudioBuffer<float>& buffer) {
    const int channels = buffer.getNumChannels();
    const int samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return 0.0f;

    double sumSquares = 0.0;
    int sampleCount = 0;
    for (int ch = 0; ch < channels; ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < samples; ++i) {
            const double sample = data[i];
            sumSquares += sample * sample;
        }
        sampleCount += samples;
    }

    if (sampleCount <= 0)
        return 0.0f;
    return static_cast<float>(std::sqrt(sumSquares / static_cast<double>(sampleCount)));
}

float computePeakAbs(const juce::AudioBuffer<float>& buffer) {
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            peak = std::max(peak, std::abs(data[i]));
    }
    return peak;
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
    identity.secondaryDefaultValue = 0.0f;
    return identity;
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
    if (!dryLowpassState.empty())
        std::fill(dryLowpassState.begin(), dryLowpassState.end(), 0.0f);
    if (!wetLowpassState.empty())
        std::fill(wetLowpassState.begin(), wetLowpassState.end(), 0.0f);
    if (!bodySpeechState.empty())
        std::fill(bodySpeechState.begin(), bodySpeechState.end(), 0.0f);
    smoothedReduce = 0.0f;
    smoothedBody = 0.0f;
    smoothedCompensationGain = 1.0f;
    controlsPrimed = false;
}

void VXDeverbAudioProcessor::setDebugRt60PresetSeconds(const float rt60Seconds) {
    deverbProcessor.setRt60PresetSeconds(rt60Seconds);
}

void VXDeverbAudioProcessor::clearDebugRt60Preset() {
    deverbProcessor.clearRt60Preset();
}

void VXDeverbAudioProcessor::setDebugDeterministicReset(const bool shouldUseDefaultRt60) {
    deverbProcessor.setDeterministicReset(shouldUseDefaultRt60);
}

float VXDeverbAudioProcessor::getDebugTrackedRt60Seconds(const int channel) const noexcept {
    return deverbProcessor.getTrackedRt60Seconds(channel);
}

void VXDeverbAudioProcessor::setDebugOverSubtract(const float newOverSubtract) {
    deverbProcessor.setOverSubtract(newOverSubtract);
}

float VXDeverbAudioProcessor::getDebugOverSubtract() const noexcept {
    return deverbProcessor.getOverSubtract();
}

void VXDeverbAudioProcessor::setDebugNoCepstral(const bool shouldBypass) {
    deverbProcessor.setDebugNoCepstral(shouldBypass);
}

bool VXDeverbAudioProcessor::isDebugNoCepstral() const noexcept {
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
    const float bodyTarget = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.0f);
    const float dryRms = computeBufferRms(buffer);
    const bool isFirstBlock = !controlsPrimed;
    if (isFirstBlock) {
        smoothedReduce = reduceTarget;
        smoothedBody = bodyTarget;
        controlsPrimed = true;
    } else {
        smoothedReduce = vxsuite::smoothBlockValue(smoothedReduce, reduceTarget, currentSampleRateHz, numSamples, 0.060f);
        smoothedBody = vxsuite::smoothBlockValue(smoothedBody, bodyTarget, currentSampleRateHz, numSamples, 0.090f);
    }

    for (int ch = 0; ch < outputChannels; ++ch)
        wetScratch.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    vxsuite::ProcessOptions options {};
    options.labRawMode = true;

    const bool voiceMode = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const auto voiceContext = getVoiceContextSnapshot();
    const float vocalPriority = voiceMode
        ? vxsuite::clamp01(0.36f * voiceContext.vocalDominance
                         + 0.28f * voiceContext.intelligibility
                         + 0.18f * voiceContext.phraseActivity
                         + 0.10f * voiceContext.speechPresence
                         + 0.08f * voiceContext.centerConfidence)
        : 0.0f;
    deverbProcessor.voiceMode = voiceMode;

    // Pass reduce directly as the Wiener amount — at reduce=0, amount=0 so all
    // Wiener gains collapse to 1.0 (true bypass with no dry blend needed).
    // overSubtract still scales with reduce so depth ramps up with the knob.
    const float reduce = vxsuite::clamp01(smoothedReduce);
    const float effectiveReduce = voiceMode
        ? vxsuite::clamp01(reduce * (1.0f - 0.10f * vocalPriority + 0.06f * voiceContext.buriedSpeech))
        : reduce;
    deverbProcessor.setOverSubtract(1.0f + 1.5f * effectiveReduce);

    const auto renderWet = [&](const float amount) {
        for (int ch = 0; ch < outputChannels; ++ch)
            wetScratch.copyFrom(ch, 0, buffer, ch, 0, numSamples);
        juce::AudioBuffer<float> wetView (wetScratch.getArrayOfWritePointers(), outputChannels, numSamples);
        deverbProcessor.processInPlace(wetView, amount, options);
    };

    renderWet(effectiveReduce);
    if (!bufferIsStable(wetScratch, 4.0f)) {
        deverbProcessor.reset();
        for (int ch = 0; ch < outputChannels; ++ch)
            wetScratch.copyFrom(ch, 0, buffer, ch, 0, numSamples);
    }
    ensureLatencyAlignedListenDry(numSamples);

    for (int ch = 0; ch < outputChannels; ++ch)
        buffer.copyFrom(ch, 0, wetScratch, ch, 0, numSamples);

    const float effectiveBody = voiceMode
        ? vxsuite::clamp01(smoothedBody + 0.10f * vocalPriority)
        : smoothedBody;
    if (effectiveBody > 1.0e-4f)
        applyBodyRestore(getLatencyAlignedListenDryBuffer(), buffer, effectiveBody, isFirstBlock);
    applyLoudnessCompensation(buffer, dryRms, effectiveReduce, isFirstBlock);
}

void VXDeverbAudioProcessor::ensureScratchCapacity(const int channels, const int samples) {
    const int safeChannels = std::max(1, channels);
    const int safeSamples = std::max(1, samples);
    wetScratch.setSize(safeChannels, safeSamples, false, false, true);
    dryLowpassState.assign(static_cast<size_t>(safeChannels), 0.0f);
    wetLowpassState.assign(static_cast<size_t>(safeChannels), 0.0f);
    bodySpeechState.assign(static_cast<size_t>(safeChannels), 0.0f);
}

void VXDeverbAudioProcessor::applyBodyRestore(const juce::AudioBuffer<float>& dryBuffer,
                                              juce::AudioBuffer<float>& wetBuffer,
                                              const float bodyAmount,
                                              const bool isFirstBlock) {
    const int channels = wetBuffer.getNumChannels();
    const int samples = wetBuffer.getNumSamples();
    if (channels <= 0 || samples <= 0 || bodyAmount < 1.0e-4f)
        return;

    const float alpha = onePoleAlpha(currentSampleRateHz, 180.0f);
    const float restore = juce::jlimit(0.0f, 1.05f, 1.05f * std::pow(vxsuite::clamp01(bodyAmount), 0.72f));
    const float support = 0.24f * vxsuite::clamp01(bodyAmount);
    const float rampDuration = 2.0f * static_cast<float>(currentSampleRateHz) / 1000.0f;
    const float speechAttack = std::exp(-1.0f / (0.004f * static_cast<float>(currentSampleRateHz)));
    const float speechRelease = std::exp(-1.0f / (0.160f * static_cast<float>(currentSampleRateHz)));

    for (int ch = 0; ch < channels; ++ch) {
        auto* wet = wetBuffer.getWritePointer(ch);
        const auto* dry = dryBuffer.getReadPointer(ch);
        float dryLp = dryLowpassState[static_cast<size_t>(ch)];
        float wetLp = wetLowpassState[static_cast<size_t>(ch)];
        float speechState = bodySpeechState[static_cast<size_t>(ch)];

        if (isFirstBlock) {
            dryLp = dry[0];
            wetLp = wet[0];
            speechState = 0.0f;
        }

        for (int i = 0; i < samples; ++i) {
            dryLp = alpha * dryLp + (1.0f - alpha) * dry[i];
            wetLp = alpha * wetLp + (1.0f - alpha) * wet[i];
            const float wetHigh = wet[i] - wetLp;
            const float dryHigh = dry[i] - dryLp;
            const float ramp = isFirstBlock
                ? std::min(1.0f, static_cast<float>(i + 1) / std::max(1.0f, rampDuration))
                : 1.0f;
            const float speechDriver = std::abs(dryHigh) + 0.18f * std::abs(dry[i]);
            const float speechIndicator = juce::jlimit(0.0f, 1.0f, (speechDriver - 0.0010f) / 0.012f);
            const float speechCoeff = speechIndicator > speechState ? speechAttack : speechRelease;
            speechState = speechCoeff * speechState + (1.0f - speechCoeff) * speechIndicator;
            const float gatedRamp = ramp * speechState;
            const float restoreDelta = std::max(0.0f, dryLp - wetLp);
            const float blendedLow = wetLp
                + (restore * gatedRamp) * restoreDelta
                + (support * gatedRamp) * dryLp;
            wet[i] = safeValue(wetHigh + blendedLow);
        }

        dryLowpassState[static_cast<size_t>(ch)] = dryLp;
        wetLowpassState[static_cast<size_t>(ch)] = wetLp;
        bodySpeechState[static_cast<size_t>(ch)] = speechState;
    }
}

void VXDeverbAudioProcessor::applyLoudnessCompensation(juce::AudioBuffer<float>& wetBuffer,
                                                       const float dryRms,
                                                       const float reduceAmount,
                                                       const bool isFirstBlock) noexcept {
    const float wetRms = computeBufferRms(wetBuffer);
    if (dryRms <= 1.0e-6f || wetRms <= 1.0e-6f || reduceAmount <= 1.0e-4f) {
        smoothedCompensationGain = 1.0f;
        return;
    }

    const float dryDb = juce::Decibels::gainToDecibels(dryRms, -120.0f);
    const float wetDb = juce::Decibels::gainToDecibels(wetRms, -120.0f);
    const float lostDb = std::max(0.0f, dryDb - wetDb);
    const float compensationDb = std::min(9.0f, lostDb * (0.72f + 0.18f * reduceAmount));
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

    const float wetPeak = computePeakAbs(wetBuffer);
    const float peakSafeGain = wetPeak > 1.0e-6f ? std::min(1.0f / wetPeak, 1.8f) : 1.0f;
    const float appliedGain = std::min(smoothedCompensationGain, peakSafeGain);
    wetBuffer.applyGain(appliedGain);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXDeverbAudioProcessor();
}
