#include "VxDeepFilterNetProcessor.h"
#include "../../framework/VxStudioHelpContent.h"
#include "VxStudioVersions.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr std::string_view kProductName = "VX Studio DeepFilterNet";
constexpr std::string_view kShortTag = "DF";
constexpr std::string_view kCleanParam = "clean";
constexpr std::string_view kGuardParam = "guard";
constexpr std::string_view kModelParam = "model";
constexpr std::string_view kListenParam = "listen";
constexpr double kLiveStartupPrerollSeconds = 0.0;

vxsuite::ModelPackage makeDeepFilterPackage(const vxsuite::deepfilternet::DeepFilterService::ModelVariant variant) {
    if (variant == vxsuite::deepfilternet::DeepFilterService::ModelVariant::rnnoise) {
        return {
            "rnnoise",
            "RNNoise Model",
            {},
            {}
        };
    }

    if (variant == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn2) {
        return {
            "deepfilternet2",
            "DeepFilterNet 2 Model",
            "VX DeepFilterNet uses an external ML denoise model. Downloading it enables realtime voice denoise without inflating the plugin bundle size.",
            {
                { "DeepFilterNet2_onnx_ll.tar.gz", "https://github.com/daverage/VxStudio/releases/download/models-v1/DeepFilterNet2_onnx_ll.tar.gz", 8628785 }
            }
        };
    }

    // DFN3 model is embedded in the plugin binary via include_bytes! — no download needed.
    return {
        "deepfilternet3",
        "DeepFilterNet 3 Model",
        {},
        {}
    };
}

juce::String describeBackend(const vxsuite::deepfilternet::DeepFilterService& engine) {
    switch (engine.realtimeBackend()) {
        case vxsuite::deepfilternet::DeepFilterService::RealtimeBackend::cpu: return "CPU";
        case vxsuite::deepfilternet::DeepFilterService::RealtimeBackend::none: break;
    }
    return "Unavailable";
}

juce::String describeVariant(const vxsuite::deepfilternet::DeepFilterService::ModelVariant variant) {
    switch (variant) {
        case vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn2: return "DeepFilterNet 2";
        case vxsuite::deepfilternet::DeepFilterService::ModelVariant::rnnoise: return "RNNoise";
        case vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn3: break;
    }
    return "DeepFilterNet 3";
}

} // namespace

VXDeepFilterNetAudioProcessor::VXDeepFilterNetAudioProcessor()
    : ProcessorBase(makeIdentity()) {
    startTimerHz(30);
}

VXDeepFilterNetAudioProcessor::~VXDeepFilterNetAudioProcessor() {
    stopTimer();
}

vxsuite::ProductIdentity VXDeepFilterNetAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.productName = kProductName;
    identity.shortTag = kShortTag;
    identity.primaryParamId = kCleanParam;
    identity.secondaryParamId = kGuardParam;
    identity.modeParamId = kModelParam;
    identity.listenParamId = kListenParam;
    identity.primaryLabel = "Clean";
    identity.secondaryLabel = "Guard";
    identity.secondaryDefaultValue = 0.0f;
    identity.primaryHint = "Voice-only ML denoise amount. Push higher for stronger DeepFilter cleanup.";
    identity.secondaryHint = "Speech protection. Backs the model off and, where safe, restores a little dry detail.";
    identity.dspVersion = vxsuite::versions::plugins::deepfilternet;
    identity.helpTitle = vxsuite::help::deepFilterNet.title;
    identity.helpHtml = vxsuite::help::deepFilterNet.html;
    identity.readmeSection = vxsuite::help::deepFilterNet.readmeSection;
    identity.selectorLabel = "Model";
    identity.selectorChoiceLabels = { "DeepFilterNet 3", "DeepFilterNet 2", "RNNoise" };
    identity.defaultMode = vxsuite::Mode::vocal;
    identity.theme.accentRgb = { 0.92f, 0.56f, 0.18f };
    identity.theme.accent2Rgb = { 0.14f, 0.11f, 0.08f };
    identity.theme.backgroundRgb = { 0.05f, 0.05f, 0.04f };
    identity.theme.panelRgb = { 0.10f, 0.09f, 0.08f };
    identity.theme.textRgb = { 0.97f, 0.94f, 0.88f };
    return identity;
}

juce::String VXDeepFilterNetAudioProcessor::getStatusText() const {
    if (isListenEnabled())
        return "Listen - removed voice noise only";

    const auto variant = selectedModelVariant();
    if (isModelDownloadInProgress())
        return describeVariant(variant) + " - downloading model";
    if (!isModelReadyForUi())
        return describeVariant(variant) + " selected - model not installed";
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

vxsuite::ModelPackage VXDeepFilterNetAudioProcessor::currentModelPackage() const {
    return makeDeepFilterPackage(selectedModelVariant());
}

bool VXDeepFilterNetAudioProcessor::isModelReadyForUi() const noexcept {
    const auto variant = selectedModelVariant();
    if (variant == ModelVariant::rnnoise || variant == ModelVariant::dfn3)
        return true;
    return vxsuite::ModelAssetService::instance().isReady(makeDeepFilterPackage(variant))
        || engine.isRealtimeReady();
}

bool VXDeepFilterNetAudioProcessor::isModelDownloadInProgress() const noexcept {
    if (selectedModelVariant() == ModelVariant::rnnoise)
        return false;
    return vxsuite::ModelAssetService::instance().isDownloading(makeDeepFilterPackage(selectedModelVariant()));
}

float VXDeepFilterNetAudioProcessor::getModelDownloadProgress() const noexcept {
    return vxsuite::ModelAssetService::instance().progress(makeDeepFilterPackage(selectedModelVariant()));
}

bool VXDeepFilterNetAudioProcessor::shouldPromptForModelDownload() const noexcept {
    if (selectedModelVariant() == ModelVariant::rnnoise)
        return false;
    return !isModelReadyForUi()
        && vxsuite::ModelAssetService::instance().shouldPrompt(makeDeepFilterPackage(selectedModelVariant()));
}

juce::String VXDeepFilterNetAudioProcessor::getModelDownloadButtonText() const {
    if (isModelDownloadInProgress())
        return "Downloading Model...";
    return "Download Model";
}

juce::String VXDeepFilterNetAudioProcessor::getModelDownloadPromptTitle() const {
    return "Download " + describeVariant(selectedModelVariant()) + "?";
}

juce::String VXDeepFilterNetAudioProcessor::getModelDownloadPromptBody() const {
    return currentModelPackage().reason
        + "\n\nThe model will be stored in your user model cache so the VST stays smaller. If you skip this, ML denoise stays unavailable until you download it later.";
}

void VXDeepFilterNetAudioProcessor::requestModelDownload() {
    vxsuite::ModelAssetService::instance().requestDownload(currentModelPackage());
}

void VXDeepFilterNetAudioProcessor::declineModelDownloadPrompt() {
    vxsuite::ModelAssetService::instance().declinePrompt(currentModelPackage());
}

void VXDeepFilterNetAudioProcessor::setNonRealtime(const bool shouldProcessOffline) noexcept {
    const bool modeChanged = shouldProcessOffline != isNonRealtime();
    juce::AudioProcessor::setNonRealtime(shouldProcessOffline);

    if (!modeChanged)
        return;

    ProcessorBase::reset();
    prepareEngineIfNeeded();
}

void VXDeepFilterNetAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    currentBlockSize = std::max(1, samplesPerBlock);
    silenceGuard.prepare();
    resetSuite();
    prepareEngineIfNeeded();
}

void VXDeepFilterNetAudioProcessor::resetSuite() {
    if (isNonRealtime())
        engine.resetRealtime();
    silenceGuard.reset();
    smoothedClean = 0.0f;
    smoothedGuard = 0.5f;
    prevWetMix = 0.0f;
    controlsPrimed = false;
    holdbackActive = !engine.isRealtimeReady() || engine.isInStartupBypass();
    tonalAnalysis.reset();
}


VXDeepFilterNetAudioProcessor::ModelVariant VXDeepFilterNetAudioProcessor::selectedModelVariant() const noexcept {
    if (const auto* raw = parameters.getRawParameterValue(kModelParam.data())) {
        const int choice = juce::roundToInt(raw->load());
        if (choice == 1)
            return ModelVariant::dfn2;
        if (choice == 2)
            return ModelVariant::rnnoise;
    }
    return ModelVariant::dfn3;
}

void VXDeepFilterNetAudioProcessor::prepareEngineIfNeeded() {
    std::lock_guard lock(enginePrepareMutex);
    if (currentSampleRateHz <= 1000.0 || currentBlockSize <= 0)
        return;
    engine.setModelVariant(selectedModelVariant());
    engine.setStartupPrerollSeconds(isNonRealtime() ? 0.0 : kLiveStartupPrerollSeconds);
    if (engine.needsRealtimePrepare(currentSampleRateHz, currentBlockSize)) {
        engine.prepareRealtime(currentSampleRateHz, currentBlockSize);
        setReportedLatencySamples(engine.getLatencySamples());
        holdbackActive = engine.isInStartupBypass();
    }
}

void VXDeepFilterNetAudioProcessor::timerCallback() {
    if (!isNonRealtime())
        prepareEngineIfNeeded();
}

void VXDeepFilterNetAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    const bool muteSilentOutput = vxsuite::blockRmsLinear(buffer) < 0.002f;
    silenceGuard.update(buffer);

    const float cleanTarget = vxsuite::readNormalized(parameters, productIdentity.primaryParamId, 0.5f);
    const float guardTarget = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.5f);

    const bool firstControlBlock = !controlsPrimed;
    if (!controlsPrimed) {
        smoothedClean = cleanTarget;
        smoothedGuard = guardTarget;
        controlsPrimed = true;
    } else {
        smoothedClean = vxsuite::smoothBlockValue(smoothedClean, cleanTarget, currentSampleRateHz, numSamples, 0.050f);
        smoothedGuard = vxsuite::smoothBlockValue(smoothedGuard, guardTarget, currentSampleRateHz, numSamples, 0.080f);
    }

    const float effectiveClean = vxsuite::clamp01(smoothedClean);

    ensureLatencyAlignedListenDry(numSamples);
    const bool wasHolding = holdbackActive;
    engine.processRealtime(buffer, currentSampleRateHz, effectiveClean, 0);
    holdbackActive = engine.isInStartupBypass();

    if (isNonRealtime() && wasHolding) {
        buffer.clear();
        prevWetMix = 1.0f - vxsuite::clamp01(smoothedGuard);
        return;
    }

    if (muteSilentOutput) {
        buffer.clear();
        prevWetMix = 1.0f - vxsuite::clamp01(smoothedGuard);
        return;
    }

    const float targetWetMix = 1.0f - vxsuite::clamp01(smoothedGuard);
    const bool liveDryGuardActive = !isNonRealtime() && wasHolding;
    const float wetMix = liveDryGuardActive ? 0.0f : targetWetMix;
    if (firstControlBlock)
        prevWetMix = liveDryGuardActive ? 0.0f : wetMix;

    blendProcessedWithDry(buffer, prevWetMix, wetMix);
    prevWetMix = wetMix;
}

void VXDeepFilterNetAudioProcessor::blendProcessedWithDry(juce::AudioBuffer<float>& buffer, const float prevWetMix, const float wetMix) {
    const auto& alignedDryScratch = getLatencyAlignedListenDryBuffer();
    const int channels = std::min(buffer.getNumChannels(), alignedDryScratch.getNumChannels());
    const int samples = std::min(buffer.getNumSamples(), alignedDryScratch.getNumSamples());
    for (int ch = 0; ch < channels; ++ch) {
        auto* processed = buffer.getWritePointer(ch);
        const auto* dry = alignedDryScratch.getReadPointer(ch);
        for (int i = 0; i < samples; ++i) {
            const float t = samples > 1 ? static_cast<float>(i) / static_cast<float>(samples - 1) : 1.0f;
            const float wet = vxsuite::clamp01(prevWetMix + (wetMix - prevWetMix) * t);
            processed[i] = dry[i] + (processed[i] - dry[i]) * wet;
        }
    }
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT) && !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXDeepFilterNetAudioProcessor();
}
#endif
