#include "VxDeepFilterNetProcessor.h"
#include "../../framework/VxStudioHelpContent.h"
#include "VxStudioVersions.h"

#include <cmath>

namespace {

constexpr std::string_view kProductName = "VX Studio DeepFilterNet";
constexpr std::string_view kShortTag = "DF";
constexpr std::string_view kCleanParam = "clean";
constexpr std::string_view kGuardParam = "guard";
constexpr std::string_view kModelParam = "model";
constexpr std::string_view kListenParam = "listen";

vxsuite::ModelPackage makeDeepFilterPackage(const vxsuite::deepfilternet::DeepFilterService::ModelVariant variant) {
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

    return {
        "deepfilternet3",
        "DeepFilterNet 3 Model",
        "VX DeepFilterNet uses an external ML denoise model. Downloading it enables realtime voice denoise without inflating the plugin bundle size.",
        {
            { "DeepFilterNet3_onnx.tar.gz", "https://github.com/daverage/VxStudio/releases/download/models-v1/DeepFilterNet3_onnx.tar.gz", 7983136 }
        }
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
    return variant == vxsuite::deepfilternet::DeepFilterService::ModelVariant::dfn2
        ? "DeepFilterNet 2"
        : "DeepFilterNet 3";
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
    identity.primaryHint = "Voice-only ML denoise amount. Push higher for stronger DeepFilter cleanup.";
    identity.secondaryHint = "Speech protection. Backs the model off and, where safe, restores a little dry detail.";
    identity.dspVersion = vxsuite::versions::plugins::deepfilternet;
    identity.helpTitle = vxsuite::help::deepFilterNet.title;
    identity.helpHtml = vxsuite::help::deepFilterNet.html;
    identity.readmeSection = vxsuite::help::deepFilterNet.readmeSection;
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
    return vxsuite::ModelAssetService::instance().isReady(makeDeepFilterPackage(selectedModelVariant()))
        || engine.isRealtimeReady();
}

bool VXDeepFilterNetAudioProcessor::isModelDownloadInProgress() const noexcept {
    return vxsuite::ModelAssetService::instance().isDownloading(makeDeepFilterPackage(selectedModelVariant()));
}

float VXDeepFilterNetAudioProcessor::getModelDownloadProgress() const noexcept {
    return vxsuite::ModelAssetService::instance().progress(makeDeepFilterPackage(selectedModelVariant()));
}

bool VXDeepFilterNetAudioProcessor::shouldPromptForModelDownload() const noexcept {
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
    analysisScratch.setSize(std::max(1, getTotalNumOutputChannels()), currentBlockSize, false, false, true);
    resetSuite();
    prepareEngineIfNeeded();
}

void VXDeepFilterNetAudioProcessor::resetSuite() {
    engine.resetRealtime();
    smoothedClean = 0.0f;
    smoothedGuard = 0.5f;
    startupWetRamp = 0.0f;
    lastArtifactRisk = 0.0f;
    controlsPrimed = false;
    tonalAnalysis.reset();
}

void VXDeepFilterNetAudioProcessor::ensureAnalysisScratch(const int channels, const int samples) {
    if (analysisScratch.getNumChannels() < channels || analysisScratch.getNumSamples() < samples)
        analysisScratch.setSize(channels, samples, false, false, true);
}

float VXDeepFilterNetAudioProcessor::estimateArtifactRisk(const juce::AudioBuffer<float>& dry,
                                                          const juce::AudioBuffer<float>& wet,
                                                          const int channels,
                                                          const int samples) const noexcept {
    // Post-processing artifact detection: compares dry/wet difference to identify
    // potential DeepFilterNet suppression artifacts. Used to scale Guard knob adaptively.
    //
    // Three metrics:
    // 1. Suppression: how much signal was removed (dryRms - wetRms) / dryRms
    // 2. DeltaDominance: is the delta (removed signal) dominant relative to output?
    // 3. BrightBias: high-frequency artifacts detected via delta derivative
    //
    // Weighting: 34% suppression + 36% dominance + 30% bright bias (empirically tuned)
    // This is a reactive detector — artifacts are measured after the fact.
    // Better approach: predict artifacts using speech analysis (see framework snapshots).

    double drySquares = 0.0;
    double wetSquares = 0.0;
    double deltaSquares = 0.0;
    double brightDeltaSquares = 0.0;
    int count = 0;

    for (int ch = 0; ch < channels; ++ch) {
        const float* dryData = dry.getReadPointer(ch);
        const float* wetData = wet.getReadPointer(ch);
        float previousDelta = 0.0f;
        for (int i = 0; i < samples; ++i) {
            const double drySample = static_cast<double>(dryData[i]);
            const double wetSample = static_cast<double>(wetData[i]);
            const float delta = wetData[i] - dryData[i];  // Removed signal
            drySquares += drySample * drySample;
            wetSquares += wetSample * wetSample;
            deltaSquares += static_cast<double>(delta) * static_cast<double>(delta);
            const float brightDelta = delta - previousDelta;  // Derivative (HF artifact indicator)
            brightDeltaSquares += static_cast<double>(brightDelta) * static_cast<double>(brightDelta);
            previousDelta = delta;
        }
        count += samples;
    }

    if (count <= 0)
        return 0.0f;

    const float dryRms = std::sqrt(static_cast<float>(drySquares / static_cast<double>(count)));
    const float wetRms = std::sqrt(static_cast<float>(wetSquares / static_cast<double>(count)));
    const float deltaRms = std::sqrt(static_cast<float>(deltaSquares / static_cast<double>(count)));
    const float brightDeltaRms = std::sqrt(static_cast<float>(brightDeltaSquares / static_cast<double>(count)));

    // Metric 1: How aggressively did the model suppress?
    const float suppression = vxsuite::clamp01((dryRms - wetRms) / std::max(dryRms, 1.0e-6f));

    // Metric 2: Is the removed signal dominant (signs of over-suppression)?
    const float deltaDominance = vxsuite::clamp01(deltaRms / std::max(wetRms + 0.35f * dryRms, 1.0e-6f));

    // Metric 3: High-frequency artifacts (ringing, phasiness)?
    const float brightBias = vxsuite::clamp01(brightDeltaRms / std::max(deltaRms * 2.4f, 1.0e-6f));

    return vxsuite::clamp01(0.34f * suppression + 0.36f * deltaDominance + 0.30f * brightBias);
}

VXDeepFilterNetAudioProcessor::ModelVariant VXDeepFilterNetAudioProcessor::selectedModelVariant() const noexcept {
    if (const auto* raw = parameters.getRawParameterValue(kModelParam.data()))
        return raw->load() < 0.5f ? ModelVariant::dfn3 : ModelVariant::dfn2;
    return ModelVariant::dfn3;
}

void VXDeepFilterNetAudioProcessor::prepareEngineIfNeeded() {
    std::lock_guard lock(enginePrepareMutex);
    if (currentSampleRateHz <= 1000.0 || currentBlockSize <= 0)
        return;
    engine.setModelVariant(selectedModelVariant());
    if (engine.needsRealtimePrepare(currentSampleRateHz, currentBlockSize)) {
        engine.prepareRealtime(currentSampleRateHz, currentBlockSize);
        setReportedLatencySamples(engine.getLatencySamples());
    }
}

void VXDeepFilterNetAudioProcessor::timerCallback() {
    if (!isNonRealtime())
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

    // Cache analysis snapshots for reuse throughout processing
    const auto analysis = getVoiceAnalysisSnapshot();
    const auto voiceContext = getVoiceContextSnapshot();
    const auto signalQuality = getSignalQualitySnapshot();

    ensureAnalysisScratch(numChannels, numSamples);
    analysisScratch.makeCopyOf(buffer, true);

    if (!controlsPrimed) {
        smoothedClean = cleanTarget;
        smoothedGuard = guardTarget;
        controlsPrimed = true;
    } else {
        smoothedClean = vxsuite::smoothBlockValue(smoothedClean, cleanTarget, currentSampleRateHz, numSamples, 0.050f);
        smoothedGuard = vxsuite::smoothBlockValue(smoothedGuard, guardTarget, currentSampleRateHz, numSamples, 0.080f);
    }

    const auto variant = selectedModelVariant();
    const float wetMix = vxsuite::clamp01(smoothedClean);
    const float adaptiveGuard = vxsuite::clamp01(smoothedGuard * (0.55f + 0.90f * lastArtifactRisk));

    // Wire framework analysis for speech-aware suppression ceiling
    // Reduce model strength in risky signal conditions: low speech presence, high intelligibility risk
    const float speechPresence = juce::jlimit(0.0f, 1.0f, voiceContext.speechPresence);
    const float intelligibilityRisk = 1.0f - juce::jlimit(0.0f, 1.0f, voiceContext.intelligibility);
    const float separationConfidence = juce::jlimit(0.0f, 1.0f, signalQuality.separationConfidence);

    // Safety factor: back off model strength when speech is ambiguous or intelligibility is at risk
    const float speechSafetyFactor = juce::jlimit(0.45f, 1.0f,
        0.65f + 0.35f * speechPresence
              - 0.30f * intelligibilityRisk
              + 0.15f * separationConfidence);

    float effectiveClean = wetMix * speechSafetyFactor;
    if (variant == ModelVariant::dfn2) {
        // DFN2 reacts badly to post dry/wet recombination, so Guard should
        // mainly back off the model drive rather than reintroduce dry signal.
        effectiveClean = vxsuite::clamp01(effectiveClean * (1.0f - 0.30f * smoothedGuard - 0.30f * adaptiveGuard));
    }
    const bool processed = engine.processRealtime(buffer, currentSampleRateHz, effectiveClean, 0);

    if (processed) {
        lastArtifactRisk = estimateArtifactRisk(analysisScratch, buffer, numChannels, numSamples);
        const float confidenceGuard = vxsuite::clamp01(smoothedGuard * (0.25f + 0.90f * lastArtifactRisk));
        if (variant != ModelVariant::dfn2)
            effectiveClean = vxsuite::clamp01(effectiveClean * (1.0f - 0.34f * confidenceGuard));

        // ReadabilityGuard post-pass: protect articulation and body from over-suppression
        vxsuite::corrective::updateTonalAnalysis(tonalAnalysis, buffer, currentSampleRateHz, numSamples);
        const auto evidence = vxsuite::corrective::deriveAnalysisEvidence(tonalAnalysis, analysis, voiceContext);
        const auto readabilityGuard = vxsuite::corrective::deriveReadabilityGuard(
            evidence, analysis, voiceContext,
            0.5f,           // focus (neutral for ML safety pass)
            true,           // voice mode (assume speech)
            0.10f,          // persistentLowMidDensity
            0.06f,          // shortLowMidDensity
            0.14f,          // persistentPresenceDensity
            0.08f,          // shortPresenceDensity
            0.0f,           // cleanup drive (not applicable for ML model)
            0.0f,           // body (not applicable)
            0.0f, 0.0f, 0.0f, 0.0f, // deMud, deEss, breath, plosive disabled
            0.20f);         // troubleSmooth (gentle smoothing for articulation risk)

        // Scale wet mix down based on articulation and body loss risk
        const float articulationProt = juce::jlimit(0.0f, 1.0f, 0.60f * readabilityGuard.articulationRisk);
        const float bodyProt = juce::jlimit(0.0f, 1.0f, 0.40f * readabilityGuard.bodyLossRisk);
        const float readabilityGuardFactor = 1.0f - (0.55f * articulationProt + 0.45f * bodyProt);

        startupWetRamp = vxsuite::smoothBlockValue(startupWetRamp, 1.0f, currentSampleRateHz, numSamples, 0.040f);
        ensureLatencyAlignedListenDry(numSamples);
        const float restoreWet = variant == ModelVariant::dfn2
            ? vxsuite::clamp01(effectiveClean * (1.0f - 0.10f * confidenceGuard) * readabilityGuardFactor)
            : vxsuite::clamp01(effectiveClean * (1.0f - 0.32f * confidenceGuard) * readabilityGuardFactor);
        blendProcessedWithDry(buffer, restoreWet * startupWetRamp);
    } else {
        startupWetRamp = 0.0f;
        lastArtifactRisk = 0.0f;
    }
}

void VXDeepFilterNetAudioProcessor::blendProcessedWithDry(juce::AudioBuffer<float>& buffer, const float wetMix) {
    const auto& alignedDryScratch = getLatencyAlignedListenDryBuffer();
    const int channels = std::min(buffer.getNumChannels(), alignedDryScratch.getNumChannels());
    const int samples = std::min(buffer.getNumSamples(), alignedDryScratch.getNumSamples());
    const float wet = vxsuite::clamp01(wetMix);
    for (int ch = 0; ch < channels; ++ch) {
        auto* processed = buffer.getWritePointer(ch);
        const auto* dry = alignedDryScratch.getReadPointer(ch);
        for (int i = 0; i < samples; ++i)
            processed[i] = dry[i] + (processed[i] - dry[i]) * wet;
    }
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT) && !defined(VXSTUDIO_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXDeepFilterNetAudioProcessor();
}
#endif
