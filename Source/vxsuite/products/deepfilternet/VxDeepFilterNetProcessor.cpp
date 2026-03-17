#include "VxDeepFilterNetProcessor.h"

#include <cmath>

namespace {

constexpr std::string_view kSuiteName = "VX Suite";
constexpr std::string_view kProductName = "DeepFilterNet";
constexpr std::string_view kShortTag = "DF";
constexpr std::string_view kCleanParam = "clean";
constexpr std::string_view kGuardParam = "guard";
constexpr std::string_view kModelParam = "model";
constexpr std::string_view kListenParam = "listen";

float clamp01(const float v) {
    return juce::jlimit(0.0f, 1.0f, v);
}

juce::String describeBackend(const vxsuite::deepfilternet::DeepFilterService& engine) {
    switch (engine.realtimeBackend()) {
        case vxsuite::deepfilternet::DeepFilterService::RealtimeBackend::cpu: return "CPU";
        case vxsuite::deepfilternet::DeepFilterService::RealtimeBackend::none: break;
    }
    return "Unavailable";
}

juce::String describeVariant(const vxsuite::deepfilternet::DeepFilterService::ModelVariant variant) {
    return variant == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn2
        ? "DeepFilterNet 2"
        : "DeepFilterNet 3";
}

} // namespace

VXDeepFilterNetAudioProcessor::VXDeepFilterNetAudioProcessor()
    : ProcessorBase(makeIdentity(), makeLayout(makeIdentity())) {
    startTimerHz(4);
}

VXDeepFilterNetAudioProcessor::~VXDeepFilterNetAudioProcessor() {
    stopTimer();
}

vxsuite::ProductIdentity VXDeepFilterNetAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.suiteName = kSuiteName;
    identity.productName = kProductName;
    identity.shortTag = kShortTag;
    identity.primaryParamId = kCleanParam;
    identity.secondaryParamId = kGuardParam;
    identity.modeParamId = kModelParam;
    identity.listenParamId = kListenParam;
    identity.primaryLabel = "Clean";
    identity.secondaryLabel = "Guard";
    identity.primaryHint = "Voice-only ML denoise amount. Push higher for stronger DeepFilter cleanup.";
    identity.secondaryHint = "Speech protection. Backs the model off and, where safe, restores a little dry detail.";
    identity.selectorLabel = "Model";
    identity.selectorChoiceLabels = { "DeepFilterNet 3", "DeepFilterNet 2" };
    identity.defaultMode = vxsuite::Mode::vocal;
    identity.theme.accentRgb = { 0.92f, 0.56f, 0.18f };
    identity.theme.accent2Rgb = { 0.14f, 0.11f, 0.08f };
    identity.theme.backgroundRgb = { 0.05f, 0.05f, 0.04f };
    identity.theme.panelRgb = { 0.10f, 0.09f, 0.08f };
    identity.theme.textRgb = { 0.97f, 0.94f, 0.88f };
    return identity;
}

juce::AudioProcessorValueTreeState::ParameterLayout
VXDeepFilterNetAudioProcessor::makeLayout(const vxsuite::ProductIdentity& identity) {
    return vxsuite::createSimpleParameterLayout(identity);
}

const juce::String VXDeepFilterNetAudioProcessor::getName() const {
    return "VX DeepFilterNet";
}

juce::String VXDeepFilterNetAudioProcessor::getStatusText() const {
    if (isListenEnabled())
        return "Listen - removed voice noise only";

    const auto variant = selectedModelVariant();
    const auto status = engine.lastStatus();
    if (status.startsWith("rt_missing_model"))
        return describeVariant(variant) + " selected - model not found";
    if (status == "rt_init_failed")
        return describeVariant(variant) + " - runtime init failed";
    if (status == "rt_process_failed")
        return describeVariant(variant) + " - processing fallback";
    if (engine.isRealtimeReady())
        return describeVariant(variant) + " - realtime " + describeBackend(engine) + " voice denoise";
    return describeVariant(variant) + " - preparing realtime backend";
}

juce::AudioProcessorEditor* VXDeepFilterNetAudioProcessor::createEditor() {
    return new vxsuite::EditorBase(*this);
}

void VXDeepFilterNetAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    currentBlockSize = std::max(1, samplesPerBlock);
    prepareEngineIfNeeded();
    ensureScratchCapacity(getTotalNumOutputChannels(), currentBlockSize);
    resetSuite();
}

void VXDeepFilterNetAudioProcessor::resetSuite() {
    engine.resetRealtime();
    dryScratch.clear();
    alignedDryScratch.clear();
    for (auto& line : dryDelayLines)
        std::fill(line.begin(), line.end(), 0.0f);
    std::fill(dryDelayWritePos.begin(), dryDelayWritePos.end(), 0);
    smoothedClean = 0.0f;
    smoothedGuard = 0.5f;
    controlsPrimed = false;
}

void VXDeepFilterNetAudioProcessor::ensureScratchCapacity(const int channels, const int samples) {
    const int safeChannels = std::max(1, channels);
    const int safeSamples = std::max(1, samples);
    dryScratch.setSize(safeChannels, safeSamples, false, false, true);
    alignedDryScratch.setSize(safeChannels, safeSamples, false, false, true);
    const int latency = std::max(0, engine.getLatencySamples());
    const int delayCapacity = std::max(1, latency + safeSamples + 1);
    dryDelayLines.assign(static_cast<size_t>(safeChannels),
                         std::vector<float>(static_cast<size_t>(delayCapacity), 0.0f));
    dryDelayWritePos.assign(static_cast<size_t>(safeChannels), 0);
}

void VXDeepFilterNetAudioProcessor::fillAlignedDryScratch(const juce::AudioBuffer<float>& dryBuffer,
                                                          const int numSamples) {
    const int latency = std::max(0, engine.getLatencySamples());
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

VXDeepFilterNetAudioProcessor::ModelVariant VXDeepFilterNetAudioProcessor::selectedModelVariant() const noexcept {
    if (const auto* raw = parameters.getRawParameterValue(kModelParam.data()))
        return raw->load() < 0.5f ? ModelVariant::dfn3 : ModelVariant::dfn2;
    return ModelVariant::dfn3;
}

void VXDeepFilterNetAudioProcessor::prepareEngineIfNeeded() {
    if (currentSampleRateHz <= 1000.0 || currentBlockSize <= 0)
        return;
    engine.setModelVariant(selectedModelVariant());
    if (engine.needsRealtimePrepare(currentSampleRateHz, currentBlockSize)) {
        engine.prepareRealtime(currentSampleRateHz, currentBlockSize);
        setLatencySamples(engine.getLatencySamples());
        ensureScratchCapacity(getTotalNumOutputChannels(), currentBlockSize);
    }
}

void VXDeepFilterNetAudioProcessor::timerCallback() {
    prepareEngineIfNeeded();
}

void VXDeepFilterNetAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    const float cleanTarget = vxsuite::readNormalized(parameters, productIdentity.primaryParamId, 0.5f);
    const float guardTarget = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.5f);

    if (!controlsPrimed) {
        smoothedClean = cleanTarget;
        smoothedGuard = guardTarget;
        controlsPrimed = true;
    } else {
        const float sr = static_cast<float>(currentSampleRateHz);
        const float cleanAlpha = 1.0f - std::exp(-static_cast<float>(numSamples) / (0.050f * sr));
        const float guardAlpha = 1.0f - std::exp(-static_cast<float>(numSamples) / (0.080f * sr));
        smoothedClean += cleanAlpha * (cleanTarget - smoothedClean);
        smoothedGuard += guardAlpha * (guardTarget - smoothedGuard);
    }

    if (dryScratch.getNumChannels() >= numChannels && dryScratch.getNumSamples() >= numSamples)
        for (int ch = 0; ch < numChannels; ++ch)
            dryScratch.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    const auto variant = selectedModelVariant();
    float effectiveClean = clamp01(0.10f + 0.90f * smoothedClean);
    if (variant == ModelVariant::dfn2) {
        // DFN2 reacts badly to post dry/wet recombination, so Guard should
        // mainly back off the model drive rather than reintroduce dry signal.
        effectiveClean = clamp01(effectiveClean * (1.0f - 0.42f * smoothedGuard));
    }
    const bool processed = engine.processRealtime(buffer, currentSampleRateHz, effectiveClean, 0);

    if (processed && alignedDryScratch.getNumChannels() >= numChannels
        && alignedDryScratch.getNumSamples() >= numSamples) {
        fillAlignedDryScratch(dryScratch, numSamples);
        const float guardRecovery = variant == ModelVariant::dfn2
            ? 0.0f
            : clamp01(0.18f * smoothedGuard);
        for (int ch = 0; ch < numChannels; ++ch) {
            auto* wet = buffer.getWritePointer(ch);
            const auto* dry = alignedDryScratch.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                wet[i] = wet[i] + (dry[i] - wet[i]) * guardRecovery;
        }
    }
}

void VXDeepFilterNetAudioProcessor::renderListenOutput(juce::AudioBuffer<float>& outputBuffer,
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
    return new VXDeepFilterNetAudioProcessor();
}
