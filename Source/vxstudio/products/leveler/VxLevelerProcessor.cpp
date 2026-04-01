#include "VxLevelerProcessor.h"

#include "vxstudio/framework/VxStudioBlockSmoothing.h"
#include "vxstudio/framework/VxStudioHelpContent.h"
#include "vxstudio/framework/VxStudioParameters.h"
#include "VxStudioVersions.h"

#include <cmath>
#include <utility>

namespace {

constexpr std::string_view kProductName = "Leveler";
constexpr std::string_view kShortTag = "LVL";
constexpr std::string_view kLevelParam = "level";
constexpr std::string_view kControlParam = "control";
constexpr std::string_view kModeParam = "mode";
constexpr std::string_view kAnalysisModeParam = "analysisMode";
constexpr std::string_view kAnalyzeParam = "analyze";

} // namespace

VXLevelerAudioProcessor::VXLevelerAudioProcessor()
    : ProcessorBase(makeIdentity()) {}

vxsuite::ProductIdentity VXLevelerAudioProcessor::makeIdentity() {
    vxsuite::ProductIdentity identity {};
    identity.productName = kProductName;
    identity.shortTag = kShortTag;
    identity.primaryParamId = kLevelParam;
    identity.secondaryParamId = kControlParam;
    identity.modeParamId = kModeParam;
    identity.auxSelectorParamId = kAnalysisModeParam;
    identity.selectorChoiceLabels = { "Vocal Rider", "Mix Leveler" };
    identity.auxSelectorLabel = "Analysis";
    identity.auxSelectorChoiceLabels = { "Realtime", "Smart Realtime", "Offline" };
    identity.auxSelectorDefaultIndex = 1;
    identity.learnParamId = kAnalyzeParam;
    identity.learnButtonLabel = "Analyze";
    identity.defaultMode = vxsuite::Mode::general;
    identity.primaryLabel = "Level";
    identity.secondaryLabel = "Control";
    identity.primaryDefaultValue = 0.0f;
    identity.secondaryDefaultValue = 0.0f;
    identity.primaryHint = "Ride voice phrases or the full mix toward a more even perceived level.";
    identity.secondaryHint = "Set how firmly peaks and harsh bursts are contained without flattening the take.";
    identity.dspVersion = vxsuite::versions::plugins::leveler;
    identity.helpTitle = vxsuite::help::leveler.title;
    identity.helpHtml = vxsuite::help::leveler.html;
    identity.readmeSection = vxsuite::help::leveler.readmeSection;
    identity.showLevelTrace = true;
    identity.stageType = vxsuite::StageType::mixed;
    identity.theme.accentRgb = { 0.82f, 0.64f, 0.24f };
    identity.theme.accent2Rgb = { 0.12f, 0.10f, 0.06f };
    identity.theme.backgroundRgb = { 0.06f, 0.05f, 0.04f };
    identity.theme.panelRgb = { 0.12f, 0.10f, 0.08f };
    identity.theme.textRgb = { 0.97f, 0.94f, 0.88f };
    return identity;
}

juce::String VXLevelerAudioProcessor::getStatusText() const {
    if (analysisActive)
        return "Offline mix analysis - capturing programme map";

    if (vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal)
        return "Intelligent vocal rider for speech-led performance recordings";

    const int analysisMode = vxsuite::readChoiceIndex(parameters, productIdentity.auxSelectorParamId, 1);
    switch (analysisMode) {
        case 0:
            return "Realtime mix leveler for whole-track consistency";
        case 2:
            if (analysisReady && dsp.hasOfflineTargetMap())
                return "Offline mix leveler - analysis locked and active";
            return dsp.hasOfflineTargetMap()
                ? "Offline mix leveler - offline map active"
                : "Offline analysis unavailable - using Smart Realtime";
        case 1:
        default:
            if (dsp.getGlobalConfidence() < 0.20f)
                return "Smart mix leveler - learning track";
            if (dsp.getGlobalConfidence() < 0.70f)
                return "Smart mix leveler - adapting to track";
            return "Smart mix leveler - track understood";
    }
}

int VXLevelerAudioProcessor::getActivityLightCount() const noexcept { return 3; }

float VXLevelerAudioProcessor::getActivityLight(int index) const noexcept {
    switch (index) {
        case 0: return dsp.getLiftActivity();
        case 1: return dsp.getLevelActivity();
        case 2: return dsp.getTameActivity();
        default: return 0.0f;
    }
}

std::string_view VXLevelerAudioProcessor::getActivityLightLabel(int index) const noexcept {
    switch (index) {
        case 0: return "Ride";
        case 1: return "Level";
        case 2: return "Protect";
        default: return {};
    }
}

void VXLevelerAudioProcessor::setDebugTuning(const vxsuite::leveler::Dsp::Tuning& tuning) noexcept {
    dsp.setTuning(tuning);
}

void VXLevelerAudioProcessor::setOfflineAnalysis(vxsuite::leveler::OfflineAnalysisResult analysis) {
    dsp.setOfflineAnalysis(std::move(analysis));
    analysisReady = dsp.hasOfflineTargetMap();
    analysisConfidence = analysisReady ? 1.0f : 0.0f;
}

void VXLevelerAudioProcessor::clearOfflineAnalysis() noexcept {
    dsp.clearOfflineAnalysis();
    analysisReady = false;
    analysisConfidence = 0.0f;
}

vxsuite::leveler::Dsp::DebugSnapshot VXLevelerAudioProcessor::getDebugSnapshot() const noexcept {
    return dsp.getDebugSnapshot();
}

void VXLevelerAudioProcessor::prepareSuite(const double sampleRate, const int samplesPerBlock) {
    currentSampleRateHz = sampleRate > 1000.0 ? sampleRate : 48000.0;
    preparedBlockSize = std::max(1, samplesPerBlock);
    detector.prepare(currentSampleRateHz, samplesPerBlock);
    dsp.prepare(currentSampleRateHz, samplesPerBlock, getTotalNumOutputChannels());
    prepareAnalysisCapture(samplesPerBlock);
    setReportedLatencySamples(dsp.latencySamples());
    resetSuite();
}

void VXLevelerAudioProcessor::resetSuite() {
    detector.reset();
    dsp.reset();
    controls.reset(0.0f, 0.0f);
    analyzeToggleLatched = false;
    analysisActive = false;
    analysisReady = dsp.hasOfflineTargetMap();
    analysisProgress = 0.0f;
    analysisConfidence = analysisReady ? 1.0f : 0.0f;
    analysisObservedSeconds = 0.0f;
    resetAnalysisCapture(true);
}

void VXLevelerAudioProcessor::processProduct(juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const float levelTarget = vxsuite::readNormalized(parameters, productIdentity.primaryParamId, 0.0f);
    const float controlTarget = vxsuite::readNormalized(parameters, productIdentity.secondaryParamId, 0.0f);

    const auto [smoothedLevel, smoothedControl] = controls.process(
        levelTarget, controlTarget,
        currentSampleRateHz, numSamples,
        0.080f, 0.080f);

    vxsuite::leveler::Dsp::Params params {};
    params.level = juce::jlimit(0.0f, 1.0f, smoothedLevel);
    params.control = juce::jlimit(0.0f, 1.0f, smoothedControl);
    params.voiceMode = vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::vocal;
    params.analysisMode = static_cast<vxsuite::leveler::Dsp::MixAnalysisMode>(
        juce::jlimit(0, 2, vxsuite::readChoiceIndex(parameters, productIdentity.auxSelectorParamId, 1)));
    const auto signalQuality = getSignalQualitySnapshot();
    params.monoScore = signalQuality.monoScore;
    params.compressionScore = signalQuality.compressionScore;
    params.tiltScore = signalQuality.tiltScore;
    params.separationConfidence = signalQuality.separationConfidence;
    const bool analyzeEnabled = vxsuite::readBool(parameters, productIdentity.learnParamId, false);
    const bool allowAnalysis = !params.voiceMode
        && params.analysisMode == vxsuite::leveler::Dsp::MixAnalysisMode::offline;
    if (!allowAnalysis && analysisActive)
        stopAnalysisCapture();
    if (allowAnalysis) {
        if (analyzeEnabled && !analyzeToggleLatched)
            startAnalysisCapture();
        else if (!analyzeEnabled && analyzeToggleLatched)
            stopAnalysisCapture();
    } else {
        analyzeToggleLatched = analyzeEnabled;
    }
    if (analysisActive)
        captureAnalysisAudio(buffer);

    const auto detectorSnapshot = detector.analyse(buffer,
                                                   getVoiceAnalysisSnapshot(),
                                                   getVoiceContextSnapshot());
    dsp.setParams(params);
    dsp.process(buffer, detectorSnapshot);
}

bool VXLevelerAudioProcessor::shouldShowLearnUi() const noexcept {
    return vxsuite::readMode(parameters, productIdentity) == vxsuite::Mode::general
        && vxsuite::readChoiceIndex(parameters, productIdentity.auxSelectorParamId, 1) == 2;
}

void VXLevelerAudioProcessor::prepareAnalysisCapture(const int maxBlockSize) {
    const double safeRate = currentSampleRateHz > 1000.0 ? currentSampleRateHz : 48000.0;
    const int blockSize = std::max(1, maxBlockSize);
    analysisMaxBlocks = std::max(256, juce::roundToInt(static_cast<float>(safeRate * 7200.0 / static_cast<double>(blockSize))));
    analysisBlockDb.assign(static_cast<size_t>(analysisMaxBlocks), -100.0f);
    resetAnalysisCapture(true);
}

void VXLevelerAudioProcessor::resetAnalysisCapture(const bool keepOfflineMap) noexcept {
    analysisCapturedBlocks = 0;
    analysisFrameCursor = 0;
    analysisEnergy = 0.0;
    analysisProgress = 0.0f;
    analysisObservedSeconds = 0.0f;
    if (!keepOfflineMap) {
        analysisReady = false;
        analysisConfidence = 0.0f;
    }
}

void VXLevelerAudioProcessor::startAnalysisCapture() {
    dsp.clearOfflineAnalysis();
    analysisActive = true;
    analysisReady = false;
    analysisConfidence = 0.0f;
    resetAnalysisCapture(true);
    analyzeToggleLatched = true;
}

void VXLevelerAudioProcessor::stopAnalysisCapture() {
    analysisActive = false;
    analyzeToggleLatched = false;

    if (analysisCapturedBlocks <= 0) {
        analysisReady = false;
        analysisConfidence = 0.0f;
        dsp.clearOfflineAnalysis();
        return;
    }

    const auto result = vxsuite::leveler::OfflineAnalyzer::analyse(analysisBlockDb.data(),
                                                                    static_cast<size_t>(analysisCapturedBlocks),
                                                                    currentSampleRateHz,
                                                                    preparedBlockSize);
    if (result.isValid()) {
        dsp.setOfflineAnalysis(result);
        analysisReady = true;
        const float coverageConfidence = juce::jlimit(0.0f, 1.0f, analysisObservedSeconds / 18.0f);
        const float durationConfidence = juce::jlimit(0.0f, 1.0f, analysisObservedSeconds / 30.0f);
        const float rangeConfidence = juce::jlimit(0.0f, 1.0f, (result.globalDynamicRangeDb - 1.5f) / 8.5f);
        const float usableFloor = 0.58f + 0.22f * coverageConfidence;
        analysisConfidence = juce::jlimit(0.0f,
                                          1.0f,
                                          usableFloor
                                              + 0.14f * durationConfidence
                                              + 0.08f * rangeConfidence);
    } else {
        analysisReady = false;
        analysisConfidence = 0.0f;
        dsp.clearOfflineAnalysis();
    }
}

void VXLevelerAudioProcessor::captureAnalysisAudio(const juce::AudioBuffer<float>& buffer) noexcept {
    const int numChannels = std::max(1, buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    if (analysisCapturedBlocks >= analysisMaxBlocks || numSamples <= 0)
        return;

    for (int i = 0; i < numSamples; ++i) {
        for (int ch = 0; ch < numChannels; ++ch) {
            const float sample = buffer.getSample(ch, i);
            analysisEnergy += static_cast<double>(sample) * static_cast<double>(sample);
        }
        ++analysisFrameCursor;

        if (analysisFrameCursor >= preparedBlockSize && analysisCapturedBlocks < analysisMaxBlocks) {
            const double denom = static_cast<double>(numChannels * preparedBlockSize);
            const float rms = denom > 0.0 ? static_cast<float>(std::sqrt(analysisEnergy / denom)) : 0.0f;
            analysisBlockDb[static_cast<size_t>(analysisCapturedBlocks)] = juce::Decibels::gainToDecibels(std::max(rms, 1.0e-5f), -100.0f);
            ++analysisCapturedBlocks;
            analysisFrameCursor = 0;
            analysisEnergy = 0.0;
            analysisObservedSeconds = static_cast<float>(analysisCapturedBlocks * preparedBlockSize) / static_cast<float>(currentSampleRateHz);
            analysisProgress = juce::jlimit(0.0f, 1.0f, analysisObservedSeconds / 18.0f);
            analysisConfidence = juce::jlimit(0.0f, 1.0f, analysisObservedSeconds / 24.0f);
        }
    }
}

#if !defined(VXSUITE_DISABLE_PLUGIN_ENTRYPOINT)
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new VXLevelerAudioProcessor();
}
#endif
