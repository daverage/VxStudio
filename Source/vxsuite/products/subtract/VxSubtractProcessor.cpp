#include "VxSubtractProcessor.h"

#include <cmath>

namespace {

constexpr std::string_view kSuiteName = "VX Suite";
constexpr std::string_view kProductName = "Subtract";
constexpr std::string_view kShortTag = "SUB";
constexpr std::string_view kSubtractParam = "subtract";
constexpr std::string_view kProtectParam = "protect";
constexpr std::string_view kModeParam = "mode";
constexpr std::string_view kListenParam = "listen";
constexpr std::string_view kLearnParam = "learn";
constexpr float kLearnMinSeconds = 0.45f;
constexpr float kLearnQuietStopSeconds = 0.24f;
constexpr float kLearnQuietThresholdDb = -48.0f;

float clamp01(const float value) {
    return juce::jlimit(0.0f, 1.0f, value);
}

float linearToDb(const float value) {
    return juce::Decibels::gainToDecibels(std::max(1.0e-6f, value), -120.0f);
}

} // namespace

VXSubtractAudioProcessor::VXSubtractAudioProcessor()
    : ProcessorBase(makeIdentity(), makeLayout(makeIdentity())) {}

vxsuite::ProductIdentity VXSubtractAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.suiteName = kSuiteName;
    identity.productName = kProductName;
    identity.shortTag = kShortTag;
    identity.primaryParamId = kSubtractParam;
    identity.secondaryParamId = kProtectParam;
    identity.modeParamId = kModeParam;
    identity.listenParamId = kListenParam;
    identity.learnParamId = kLearnParam;
    identity.learnButtonLabel = "Learn";
    identity.defaultMode = vxsuite::Mode::vocal;
    identity.primaryLabel = "Subtract";
    identity.secondaryLabel = "Protect";
    identity.primaryHint = "Smarter spectral subtraction than a raw profile notch, with adaptive tracking underneath.";
    identity.secondaryHint = "Keep consonants, harmonics, and detail when subtraction gets aggressive.";
    identity.theme.accentRgb = { 0.90f, 0.28f, 0.18f };
    identity.theme.accent2Rgb = { 0.13f, 0.07f, 0.06f };
    identity.theme.backgroundRgb = { 0.06f, 0.04f, 0.04f };
    identity.theme.panelRgb = { 0.10f, 0.08f, 0.08f };
    identity.theme.textRgb = { 0.97f, 0.92f, 0.89f };
    return identity;
}

juce::AudioProcessorValueTreeState::ParameterLayout
VXSubtractAudioProcessor::makeLayout(const vxsuite::ProductIdentity& identity) {
    return vxsuite::createSimpleParameterLayout(identity);
}

const juce::String VXSubtractAudioProcessor::getName() const {
    return "VX Subtract";
}

juce::String VXSubtractAudioProcessor::getStatusText() const {
    if (isListenEnabled())
        return "Listen - removed profile only";

    if (isLearnActive()) {
        const int progressPct = juce::roundToInt(100.0f * getLearnProgress());
        const int confidencePct = juce::roundToInt(100.0f * getLearnConfidence());
        return "Learning - " + juce::String(progressPct) + "% captured, "
             + juce::String(confidencePct) + "% confidence";
    }

    if (isLearnReady()) {
        const int confidencePct = juce::roundToInt(100.0f * getLearnConfidence());
        return "Locked - learned noise profile ready (" + juce::String(confidencePct) + "% confidence)";
    }

    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    return isVoice ? "Vocal - intelligent profile subtraction with speech protection"
                   : "General - broader spectral subtraction for mixed material";
}

juce::AudioProcessorEditor* VXSubtractAudioProcessor::createEditor() {
    return new vxsuite::EditorBase(*this);
}

void VXSubtractAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    subtractDsp.prepare(currentSampleRateHz, samplesPerBlock);
    setLatencySamples(subtractDsp.getLatencySamples());
    ensureScratchCapacity(getTotalNumOutputChannels(), samplesPerBlock);
    resetSuite();
}

void VXSubtractAudioProcessor::resetSuite() {
    subtractDsp.reset();
    dryScratch.clear();
    alignedDryScratch.clear();
    for (auto& line : dryDelayLines)
        std::fill(line.begin(), line.end(), 0.0f);
    std::fill(dryDelayWritePos.begin(), dryDelayWritePos.end(), 0);
    smoothedSubtract = 0.0f;
    smoothedProtect = 0.5f;
    controlsPrimed = false;
    learnToggleLatched = true;  // prevents false start-edge if Learn was left on
    learnSilentSeconds = 0.0f;
    learnActive.store(false, std::memory_order_relaxed);
    // learnReady / learnProgress / learnConfidence / learnObservedSeconds are
    // intentionally preserved — the learned noise profile survives playback stops.
}

void VXSubtractAudioProcessor::ensureScratchCapacity(const int channels, const int samples) {
    const int safeChannels = std::max(1, channels);
    const int safeSamples = std::max(1, samples);
    dryScratch.setSize(safeChannels, safeSamples, false, false, true);
    alignedDryScratch.setSize(safeChannels, safeSamples, false, false, true);
    const int latency = std::max(0, subtractDsp.getLatencySamples());
    const int delayCapacity = std::max(1, latency + safeSamples + 1);
    dryDelayLines.assign(static_cast<size_t>(safeChannels),
                         std::vector<float>(static_cast<size_t>(delayCapacity), 0.0f));
    dryDelayWritePos.assign(static_cast<size_t>(safeChannels), 0);
}

void VXSubtractAudioProcessor::fillAlignedDryScratch(const juce::AudioBuffer<float>& dryBuffer,
                                                     const int numSamples) {
    const int latency = std::max(0, subtractDsp.getLatencySamples());
    for (int ch = 0; ch < dryBuffer.getNumChannels(); ++ch) {
        const auto* dry = dryBuffer.getReadPointer(ch);
        auto* delayed = alignedDryScratch.getWritePointer(ch);
        auto& line = dryDelayLines[static_cast<size_t>(ch)];
        const int size = static_cast<int>(line.size());
        int writePos = dryDelayWritePos[static_cast<size_t>(ch)];
        for (int i = 0; i < numSamples; ++i) {
            line[static_cast<size_t>(writePos)] = dry[i];
            const int readPos = (writePos + size - latency) % size;
            delayed[i] = line[static_cast<size_t>(readPos)];
            writePos = (writePos + 1) % size;
        }
        dryDelayWritePos[static_cast<size_t>(ch)] = writePos;
    }
}

void VXSubtractAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    const float subtractTarget = vxsuite::readNormalized(parameters, productIdentity.primaryParamId, 0.0f);
    const float protectTarget = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.5f);

    if (!controlsPrimed) {
        smoothedSubtract = subtractTarget;
        smoothedProtect = protectTarget;
        controlsPrimed = true;
    } else {
        const float sr = static_cast<float>(currentSampleRateHz);
        const float subtractAlpha = 1.0f - std::exp(-static_cast<float>(numSamples) / (0.045f * sr));
        const float protectAlpha = 1.0f - std::exp(-static_cast<float>(numSamples) / (0.080f * sr));
        smoothedSubtract += subtractAlpha * (subtractTarget - smoothedSubtract);
        smoothedProtect += protectAlpha * (protectTarget - smoothedProtect);
    }

    if (dryScratch.getNumChannels() >= numChannels && dryScratch.getNumSamples() >= numSamples)
        for (int ch = 0; ch < numChannels; ++ch)
            dryScratch.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    float sumSquares = 0.0f;
    if (dryScratch.getNumChannels() >= numChannels && dryScratch.getNumSamples() >= numSamples) {
        for (int ch = 0; ch < numChannels; ++ch) {
            const auto* dry = dryScratch.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                sumSquares += dry[i] * dry[i];
        }
    } else {
        for (int ch = 0; ch < numChannels; ++ch) {
            const auto* input = buffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                sumSquares += input[i] * input[i];
        }
    }
    const float sampleCount = static_cast<float>(std::max(1, numChannels * numSamples));
    const float blockRms = std::sqrt(sumSquares / sampleCount);
    const float blockDb = linearToDb(blockRms);

    const bool isVoice = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    const bool learnRequested = vxsuite::readBool(parameters, productIdentity.learnParamId, false);
    const bool learnStartEdge = learnRequested && !learnToggleLatched;
    const bool learnStopEdge = !learnRequested && learnToggleLatched;
    if (learnStartEdge) {
        subtractDsp.clearLearnedProfile();
        subtractDsp.setLearning(true);
        learnSilentSeconds = 0.0f;
    }
    if (learnStopEdge && subtractDsp.isLearning()) {
        subtractDsp.setLearning(false);
        learnSilentSeconds = 0.0f;
    }

    bool learnRunning = subtractDsp.isLearning();
    if (learnRunning) {
        const float observedSeconds = subtractDsp.getLearnObservedSeconds();
        if (observedSeconds >= kLearnMinSeconds && blockDb <= kLearnQuietThresholdDb)
            learnSilentSeconds += static_cast<float>(numSamples) / static_cast<float>(currentSampleRateHz);
        else
            learnSilentSeconds = 0.0f;

        if (observedSeconds >= kLearnMinSeconds && learnSilentSeconds >= kLearnQuietStopSeconds) {
            subtractDsp.setLearning(false);
            learnRunning = false;
            learnSilentSeconds = 0.0f;
        }
    } else {
        learnSilentSeconds = 0.0f;
    }

    const bool learnedReady = subtractDsp.hasLearnedProfile();

    vxcleaner::dsp::ProcessOptions options {};
    options.isVoiceMode = isVoice;
    options.sourceProtect = clamp01(0.45f + 0.55f * smoothedProtect);
    options.guardStrictness = clamp01(0.40f + 0.60f * smoothedProtect);
    options.speechFocus = isVoice ? clamp01(0.65f + 0.35f * smoothedProtect) : 0.42f;
    options.learningActive = subtractDsp.isLearning();
    options.subtract = 7.0f * clamp01(smoothedSubtract);
    options.sensitivity = 1.1f * clamp01(1.0f - smoothedProtect);
    options.labRawMode = false;

    const float blindAmount = learnedReady ? 0.0f
                                           : clamp01(0.55f * smoothedSubtract);
    subtractDsp.processInPlace(buffer, blindAmount, options);

    learnProgress.store(subtractDsp.getLearnProgress(), std::memory_order_relaxed);
    learnConfidence.store(subtractDsp.getLearnConfidence(), std::memory_order_relaxed);
    learnObservedSeconds.store(subtractDsp.getLearnObservedSeconds(), std::memory_order_relaxed);
    learnActive.store(subtractDsp.isLearning(), std::memory_order_relaxed);
    learnReady.store(subtractDsp.hasLearnedProfile(), std::memory_order_relaxed);
    learnToggleLatched = learnRequested;

    if (alignedDryScratch.getNumChannels() >= numChannels
        && alignedDryScratch.getNumSamples() >= numSamples)
        fillAlignedDryScratch(dryScratch, numSamples);
}

void VXSubtractAudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
                                                  const juce::AudioBuffer<float>& inputBuffer) {
    juce::ignoreUnused(inputBuffer);

    const int channels = std::min(outputBuffer.getNumChannels(), alignedDryScratch.getNumChannels());
    const int samples = std::min(outputBuffer.getNumSamples(), alignedDryScratch.getNumSamples());
    for (int ch = 0; ch < channels; ++ch) {
        auto* out = outputBuffer.getWritePointer(ch);
        const auto* dry = alignedDryScratch.getReadPointer(ch);
        for (int i = 0; i < samples; ++i)
            out[i] = dry[i] - out[i];
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXSubtractAudioProcessor();
}
