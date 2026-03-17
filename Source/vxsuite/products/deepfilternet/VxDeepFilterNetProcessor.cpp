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
    latencyListen.prepare(getTotalNumOutputChannels(), currentBlockSize, engine.getLatencySamples());
    resetSuite();
}

void VXDeepFilterNetAudioProcessor::resetSuite() {
    engine.resetRealtime();
    latencyListen.reset();
    smoothedClean = 0.0f;
    smoothedGuard = 0.5f;
    controlsPrimed = false;
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
        latencyListen.prepare(getTotalNumOutputChannels(), currentBlockSize, engine.getLatencySamples());
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
        smoothedClean = vxsuite::smoothBlockValue(smoothedClean, cleanTarget, currentSampleRateHz, numSamples, 0.050f);
        smoothedGuard = vxsuite::smoothBlockValue(smoothedGuard, guardTarget, currentSampleRateHz, numSamples, 0.080f);
    }

    if (latencyListen.canStore(numChannels, numSamples))
        latencyListen.captureDry(buffer, numSamples);

    const auto variant = selectedModelVariant();
    float effectiveClean = vxsuite::clamp01(0.10f + 0.90f * smoothedClean);
    if (variant == ModelVariant::dfn2) {
        // DFN2 reacts badly to post dry/wet recombination, so Guard should
        // mainly back off the model drive rather than reintroduce dry signal.
        effectiveClean = vxsuite::clamp01(effectiveClean * (1.0f - 0.42f * smoothedGuard));
    }
    const bool processed = engine.processRealtime(buffer, currentSampleRateHz, effectiveClean, 0);

    if (processed && latencyListen.canStore(numChannels, numSamples)) {
        latencyListen.buildAlignedDry(numSamples, engine.getLatencySamples());
        const float guardRecovery = variant == ModelVariant::dfn2
            ? 0.0f
            : vxsuite::clamp01(0.18f * smoothedGuard);
        const auto& alignedDryScratch = latencyListen.alignedDryBuffer();
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
    latencyListen.renderRemovedDelta(outputBuffer);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXDeepFilterNetAudioProcessor();
}
