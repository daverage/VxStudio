#include "VxDeverbProcessor.h"

#include <cmath>

namespace {

constexpr std::string_view kSuiteName = "VX Suite";
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

} // namespace

VXDeverbAudioProcessor::VXDeverbAudioProcessor()
    : ProcessorBase(makeIdentity(), vxsuite::createSimpleParameterLayout(makeIdentity())) {}

vxsuite::ProductIdentity VXDeverbAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.suiteName = kSuiteName;
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
    identity.theme.accentRgb = { 0.00f, 0.82f, 1.00f };
    identity.theme.accent2Rgb = { 0.07f, 0.08f, 0.10f };
    identity.theme.backgroundRgb = { 0.05f, 0.05f, 0.07f };
    identity.theme.panelRgb = { 0.09f, 0.09f, 0.12f };
    identity.theme.textRgb = { 0.86f, 0.91f, 1.00f };
    identity.primaryDefaultValue = 0.5f;
    identity.secondaryDefaultValue = 0.0f;
    return identity;
}

const juce::String VXDeverbAudioProcessor::getName() const {
    return "VX Deverb";
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
    setLatencySamples(deverbProcessor.getLatencySamples());
    ensureScratchCapacity(getTotalNumOutputChannels(), preparedBlockSize);
    latencyListen.prepare(getTotalNumOutputChannels(), preparedBlockSize, deverbProcessor.getLatencySamples());
    resetSuite();
}

void VXDeverbAudioProcessor::resetSuite() {
    deverbProcessor.reset();
    latencyListen.reset();
    wetScratch.clear();
    if (!dryLowpassState.empty())
        std::fill(dryLowpassState.begin(), dryLowpassState.end(), 0.0f);
    if (!wetLowpassState.empty())
        std::fill(wetLowpassState.begin(), wetLowpassState.end(), 0.0f);
    smoothedReduce = 0.0f;
    smoothedBody = 0.0f;
    controlsPrimed = false;
}

juce::AudioProcessorEditor* VXDeverbAudioProcessor::createEditor() {
    return new vxsuite::EditorBase(*this);
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
    if (!latencyListen.canStore(outputChannels, numSamples))
        return;
    if (wetScratch.getNumChannels() < outputChannels || wetScratch.getNumSamples() < numSamples)
        return;

    latencyListen.captureDry(buffer, numSamples);

    const float reduceTarget = vxsuite::readNormalized(parameters, productIdentity.primaryParamId, 0.0f);
    const float bodyTarget = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.0f);
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
        wetScratch.copyFrom(ch, 0, latencyListen.dryBuffer(), ch, 0, numSamples);

    vxsuite::ProcessOptions options {};
    options.isVoiceMode = false;
    options.voiceProtect = 0.0f;
    options.sourceProtect = 0.0f;
    options.lateTailAggression = 1.0f;
    options.stereoWidthProtect = 0.0f;
    options.guardStrictness = 0.0f;
    options.speechFocus = 0.0f;
    options.isPrimary = true;
    options.labRawMode = true;

    const bool voiceMode = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    deverbProcessor.voiceMode = voiceMode;

    // Pass reduce directly as the Wiener amount — at reduce=0, amount=0 so all
    // Wiener gains collapse to 1.0 (true bypass with no dry blend needed).
    // overSubtract still scales with reduce so depth ramps up with the knob.
    const float reduce = vxsuite::clamp01(smoothedReduce);
    deverbProcessor.setOverSubtract(1.0f + 1.5f * reduce);

    {
        juce::AudioBuffer<float> wetView (wetScratch.getArrayOfWritePointers(), outputChannels, numSamples);
        deverbProcessor.processInPlace(wetView, reduce, options);
    }
    latencyListen.buildAlignedDry(numSamples, deverbProcessor.getLatencySamples());

    for (int ch = 0; ch < outputChannels; ++ch)
        buffer.copyFrom(ch, 0, wetScratch, ch, 0, numSamples);

    if (smoothedBody > 1.0e-4f)
        applyBodyRestore(latencyListen.alignedDryBuffer(), buffer, smoothedBody, isFirstBlock);
}

void VXDeverbAudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                                const juce::AudioBuffer<float>& inputBuffer) {
    juce::ignoreUnused(inputBuffer);

    latencyListen.renderRemovedDelta(outputBuffer);
}

void VXDeverbAudioProcessor::ensureScratchCapacity(const int channels, const int samples) {
    const int safeChannels = std::max(1, channels);
    const int safeSamples = std::max(1, samples);
    wetScratch.setSize(safeChannels, safeSamples, false, false, true);
    dryLowpassState.assign(static_cast<size_t>(safeChannels), 0.0f);
    wetLowpassState.assign(static_cast<size_t>(safeChannels), 0.0f);
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
    const float restore = juce::jlimit(0.0f, 0.70f, 0.70f * std::pow(vxsuite::clamp01(bodyAmount), 0.9f));
    const float rampDuration = 2.0f * static_cast<float>(currentSampleRateHz) / 1000.0f;

    for (int ch = 0; ch < channels; ++ch) {
        auto* wet = wetBuffer.getWritePointer(ch);
        const auto* dry = dryBuffer.getReadPointer(ch);
        float dryLp = dryLowpassState[static_cast<size_t>(ch)];
        float wetLp = wetLowpassState[static_cast<size_t>(ch)];

        if (isFirstBlock) {
            dryLp = dry[0];
            wetLp = wet[0];
        }

        for (int i = 0; i < samples; ++i) {
            dryLp = alpha * dryLp + (1.0f - alpha) * dry[i];
            wetLp = alpha * wetLp + (1.0f - alpha) * wet[i];
            const float wetHigh = wet[i] - wetLp;
            const float ramp = isFirstBlock
                ? std::min(1.0f, static_cast<float>(i + 1) / std::max(1.0f, rampDuration))
                : 1.0f;
            const float blendedLow = wetLp + (restore * ramp) * (dryLp - wetLp);
            wet[i] = safeValue(wetHigh + blendedLow);
        }

        dryLowpassState[static_cast<size_t>(ch)] = dryLp;
        wetLowpassState[static_cast<size_t>(ch)] = wetLp;
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXDeverbAudioProcessor();
}
